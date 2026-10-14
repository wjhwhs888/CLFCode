// CLFToolScheduler.cpp — 并发工具调度器实现（事件化重构分册 3）
// 第二批主体（2026-10-14）：schedule 主循环 + runGroup 四阶段
// （fillPool 串行闸门启动 / std::async 并发 dispatch / commitReady
// 提交游标）/ 协议闭合 / 失败静默。

#include "CLFCore/CLFToolScheduler.hpp"

#include <condition_variable>
#include <mutex>
#include <thread>

#include "CLFCore/CLFToolExecutor.hpp"

namespace CLF::CLFCore {

ToolExecMode classifyMode(const CLFTool& tool, const std::string& argsJson) {
    if (!tool.m_isConcurrencySafe) return ToolExecMode::Exclusive;  // fail-closed
    try {
        // 约束（dsh §16.2）：判定内不抛异常表达"不安全"——直接返回 false；
        // 此处 catch 兜底（第三方声明异常 → 独占）
        return tool.m_isConcurrencySafe(argsJson) ? ToolExecMode::Parallel
                                                  : ToolExecMode::Exclusive;
    } catch (...) {
        return ToolExecMode::Exclusive;
    }
}

namespace {

// ============================================================================
// 协议闭合（设计-中断时效性 §十一，2026-09-23；随调度循环自
// CLFToolExecutor 迁入）：中断后为剩余未执行调用补合成结果——不变量 I1：
// tool_calls 声明数 == tool 结果数（否则下一次请求被 API 拒绝——dsh
// repair.ts "providers reject dangling assistant calls"）。文案前缀
// [interrupted] 为 qa 断言锚点（不得改用 [Denied by user]——那是
// "用户否决操作"的另一语义，§11.9）。
void appendNotExecutedResults(const std::vector<CLFToolCall>& calls,
                              size_t fromIndex,
                              std::vector<CLFToolResult>& results) {
    for (size_t i = fromIndex; i < calls.size(); ++i) {
        CLFToolResult skipped;
        skipped.m_toolCallId = calls[i].m_id;
        skipped.m_name       = calls[i].m_name;
        skipped.m_content =
            "[interrupted] 该工具调用因用户中断而未执行，未产生任何副作用。"
            "如仍需要，请重新调用。";
        results.push_back(std::move(skipped));
    }
}

// 失败静默合成（设计 §3.6）：前序工具失败后未启动的调用补"未执行"结果。
// 文案前缀 [skipped] 区分于 [interrupted]（中断是用户行为，失败静默是
// 调度语义）；均为 qa 断言锚点。
void appendSkippedResults(const std::vector<CLFToolCall>& calls,
                          size_t fromIndex,
                          std::vector<CLFToolResult>& results) {
    for (size_t i = fromIndex; i < calls.size(); ++i) {
        CLFToolResult skipped;
        skipped.m_toolCallId = calls[i].m_id;
        skipped.m_name       = calls[i].m_name;
        skipped.m_content =
            "[skipped] 该工具调用因前序工具执行失败而未执行，未产生任何副作用。"
            "如仍需要，请重新调用。";
        results.push_back(std::move(skipped));
    }
}

} // anonymous namespace

// ============================================================================
// schedule 主循环（§3.2）：分组 + 屏障 + 协议闭合 + 失败静默
// ============================================================================

CLFToolScheduler::CLFToolScheduler(CLFToolExecutor& executor, int maxParallel)
    : m_executor(executor)
    , m_maxParallel(maxParallel > 0 ? maxParallel : 1) {   // 防御 clamp（配置层已校验 ≥1）
}

CLFToolScheduler::Result CLFToolScheduler::schedule(
    const std::vector<CLFToolCall>& calls) {
    Result r;
    r.m_results.reserve(calls.size());
    m_executor.beginBatch();

    size_t next = 0;
    while (next < calls.size()) {
        // §3.5 中断检查点：fillPool 启动前（组间）
        if (m_executor.isInterrupted()) {
            appendNotExecutedResults(calls, next, r.m_results);
            r.m_aborted = true;
            break;
        }
        // 分组：队首分类决定组形态——Parallel = 剩余全部（fillPool 内
        // 惰性重分类收敛实际边界，dsh §4.5）；Exclusive = 单调用屏障。
        // 未知工具 → 独占（prepare 自然落 not_found fallback）
        const CLFTool* head = m_executor.findTool(calls[next].m_name);
        ToolExecMode mode =
            (head && classifyMode(*head, calls[next].m_arguments)
                     == ToolExecMode::Parallel)
            ? ToolExecMode::Parallel : ToolExecMode::Exclusive;
        GroupOutcome go = runGroup(calls, next, mode);

        r.m_results.insert(r.m_results.end(),
                           std::make_move_iterator(go.m_results.begin()),
                           std::make_move_iterator(go.m_results.end()));
        next += go.m_consumed;
        r.m_concluded |= go.m_concluded;

        if (go.m_aborted) {
            // 已启动排空照常提交（runGroup 内完成）；剩余补"未执行"
            appendNotExecutedResults(calls, next, r.m_results);
            r.m_aborted = true;
            break;
        }
        if (go.m_firstFailure) {
            // 失败静默：在途排空已提交（runGroup 内完成）；未启动补合成
            appendSkippedResults(calls, next, r.m_results);
            r.m_firstFailure = go.m_firstFailure;
            break;
        }
    }

    m_executor.finishBatch(static_cast<int>(calls.size()));
    return r;
}

// ============================================================================
// runGroup（§3.3）：四阶段 + 提交游标 + 滚动池
// ============================================================================
// 组内状态：slots[i] 对应组内第 i 个调用（模型序）；fillPool 在调度线程
// 串行推进 nextToStart（prepare 逐个串行——确认串行化，用户定调安全
// 优先）；dispatch 完成回调只落 slots + 通知（提交游标在调度线程）。
CLFToolScheduler::GroupOutcome CLFToolScheduler::runGroup(
    const std::vector<CLFToolCall>& calls, size_t from, ToolExecMode mode) {
    const size_t groupCap = (mode == ToolExecMode::Parallel)
                          ? calls.size() - from : 1u;

    struct Slot {
        CLFToolExecutor::PreparedCall pc;
        CLFToolExecutor::PrepareOutcome::Kind prepareKind =
            CLFToolExecutor::PrepareOutcome::Kind::Ready;
        CLFToolResult fallback;                 // prepare Fallback 合成结果
        std::string errorKind;
        std::optional<CLFToolExecutor::RunOutcome> raw;   // dispatch 产物
        bool finished = false;
    };
    std::vector<Slot> slots(groupCap);
    size_t nextToStart = 0;
    size_t committed = 0;
    std::atomic<int> inFlight{0};
    std::mutex mtx;                       // 保护 slots[idx].raw/finished + committed 读
    std::condition_variable cv;
    // 实现偏差记录（2026-10-14）：设计 §3.4 定 std::async——qa 全组
    // 偶发挂死取证（复现矩阵 + 打点）：挂点 = 动态初始化期（boost::ut
    // 静态期执行惯例）新线程内 CRT 设施（logger 单例/time/string）与
    // 主线程初始化竞态——非确定性死锁，与 std::async/std::thread 选择
    // 无关（两者同挂）；main 期 30 轮并发零复现，生产路径（AgentLoop
    // 在 main 后）不受影响。定案：qa_CLFToolScheduler 套件改 main 期
    // 执行（环境约束钉在套件头注释）；实现保留 std::thread 直建——
    // 语义等价（受控上限保证线程数 ≤ maxParallel）且无 std::async
    // future 析构阻塞怪癖；收敛后统一 join（inFlight==0 保证不阻塞）
    std::vector<std::thread> workers;
    GroupOutcome outcome;

    // 提交游标（调度线程）：模型序连续推进——队首未完成，后面已完成的
    // 必须等（head-of-line，dsh §4.3）
    auto commitReady = [&] {
        while (committed < slots.size() && slots[committed].finished) {
            Slot& s = slots[committed];
            const CLFToolCall& call = calls[from + committed];
            CLFToolExecutor::FinalizeOutcome fin =
                (s.prepareKind == CLFToolExecutor::PrepareOutcome::Kind::Fallback)
                    ? m_executor.finalize(call, s.pc,
                                          CLFToolExecutor::RunOutcome{std::move(s.fallback), false, ""},
                                          true, s.errorKind)
                    : m_executor.finalize(call, s.pc, std::move(*s.raw), false, "");
            outcome.m_results.push_back(std::move(fin.result));
            outcome.m_concluded |= outcome.m_results.back().m_concludesTurn;
            // 失败静默只记首个（§3.6：后续失败不覆盖）
            if (!fin.ok && !outcome.m_firstFailure)
                outcome.m_firstFailure = outcome.m_results.back();
            ++committed;
        }
    };

    // 滚动池（调度线程）：并发上限内逐个启动；确认/守卫在 prepare 内
    // 串行执行（弹确认时后续调用不启动）
    auto fillPool = [&] {
        while (!outcome.m_aborted && !outcome.m_firstFailure
               && nextToStart < slots.size()
               && inFlight.load() < m_maxParallel) {
            // §3.5 中断检查点：每个启动前
            if (m_executor.isInterrupted()) { outcome.m_aborted = true; break; }
            // 惰性重分类（dsh §4.5 / §5）：Parallel 组内每个启动点按
            // 当前注册表状态重判——执行中声明变更（插件热卸载）在此收敛
            if (mode == ToolExecMode::Parallel && nextToStart > 0) {
                const CLFTool* t =
                    m_executor.findTool(calls[from + nextToStart].m_name);
                if (!t || classifyMode(*t, calls[from + nextToStart].m_arguments)
                         != ToolExecMode::Parallel)
                    break;   // 组截断——剩余由主循环重新分组
            }
            const size_t idx = nextToStart;
            Slot& s = slots[idx];
            auto po = m_executor.prepare(calls[from + idx], s.pc);
            s.prepareKind = po.kind;
            if (po.kind == CLFToolExecutor::PrepareOutcome::Kind::Aborted) {
                outcome.m_aborted = true;
                break;   // 当前调用未启动——consumed 不含它
            }
            if (po.kind == CLFToolExecutor::PrepareOutcome::Kind::Fallback) {
                s.fallback = std::move(po.fallback);
                s.errorKind = std::move(po.errorKind);
                s.finished = true;   // 同步完成，走提交游标
            } else if (m_maxParallel == 1) {
                // cap=1：同步执行（串行语义等价 + 免线程开销；qa 静态期
                // 环境无 dispatch 线程——规避动态初始化期 CRT 竞态死锁，
                // 取证见文件头注释与调度器偏差记录）
                slots[idx].raw = m_executor.run(calls[from + idx], slots[idx].pc);
                slots[idx].finished = true;
            } else {
                ++inFlight;
                workers.emplace_back([&, idx] {
                    auto raw = m_executor.run(calls[from + idx], slots[idx].pc);
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        slots[idx].raw = std::move(raw);
                        slots[idx].finished = true;
                        --inFlight;
                    }
                    cv.notify_all();
                });
            }
            ++nextToStart;
        }
    };

    fillPool();
    // 收敛：等 inFlight 排空（aborted/失败静默时同样排空——已启动照常
    // 提交）；每次唤醒先推进提交游标，再按需补位
    while (inFlight.load() > 0) {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [&] {
            return inFlight.load() == 0
                || (committed < slots.size() && slots[committed].finished);
        });
        lk.unlock();
        commitReady();
        if (!outcome.m_aborted && !outcome.m_firstFailure) fillPool();
    }
    commitReady();   // 同步 fallback 清尾

    // 收敛完成（inFlight==0）——join 全部 dispatch 线程（不阻塞）
    for (auto& w : workers) w.join();

    outcome.m_consumed = nextToStart;
    return outcome;
}

} // namespace CLF::CLFCore
