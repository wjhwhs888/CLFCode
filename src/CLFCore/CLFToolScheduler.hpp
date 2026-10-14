// CLFToolScheduler.hpp — 并发工具调度器（事件化重构分册 3，2026-10-13）
//
// 定位（设计 §3.1）：只做调度（分组/并发/归序）——工具执行体复用
// CLFToolExecutor 的单工具三段（prepare/run/finalize）。混合调度定案
// （dsh §3）：连续并行安全调用并发执行；独占调用串行且为屏障（屏障
// 持续到提交完成）。
//
// 四阶段（dsh §4）：prepare（串行闸门：确认/守卫/中断检查——执行器内）→
// dispatch（唯一并发段，std::async，上限 maxParallelToolCalls）→ finalize
// （串行：归一化/摘要/提交游标）。
//
// 结果顺序：提交游标沿模型序连续推进（队首未完成，后面已完成的必须等——
// dsh §4.3 head-of-line）。
//
// 分类（fail-closed）：tool.m_isConcurrencySafe 为空 / 异常 / 非严格 true
// → Exclusive（所有现工具初始全部独占——声明一个放开一个）。
//
// 失败静默（dsh §8 / 设计 §3.6）：首个 handler 失败（ok=false）停止新
// 派发 → 排空在途（照常提交）→ 剩余未启动补 [skipped] 合成结果 →
// 以首个失败上报（Result.m_firstFailure——AgentLoop 层处理）。
//
// 取消（dsh §7 / 设计 §3.5）：停止补充 → 排空已启动（照常提交）→
// 未启动补 [interrupted] 合成结果（appendNotExecutedResults 协议闭合）。
//
// example:
//   CLFToolExecutor executor(...);
//   CLFToolScheduler scheduler(executor, 10);
//   auto r = scheduler.schedule(toolCalls);

#pragma once

#include <optional>
#include <vector>

#include "CLFTypes/CLFTypes.hpp"

namespace CLF::CLFCore {

class CLFToolExecutor;

// 调度分类（对齐 dsh executionMode）
enum class ToolExecMode { Parallel, Exclusive };

// 分类函数：m_isConcurrencySafe 为空 → Exclusive（fail-closed）；
// 调用返回非严格 true（含抛异常，catch 吞）→ Exclusive；严格 true → Parallel
ToolExecMode classifyMode(const CLFTool& tool, const std::string& argsJson);

// 调度器（分册 3 主体，2026-10-14）：单线程驱动假设——schedule 由
// AgentLoop 工作线程调用，内部以 std::async 派生受控并发（≤ maxParallel）。
class CLFToolScheduler {
public:
    // executor：执行体引用（只调其三段接口，不持有所有权）；
    // maxParallel：并发上限（≥1；1 = 完全串行——安全阀/串行等价）
    CLFToolScheduler(CLFToolExecutor& executor, int maxParallel);

    struct Result {
        std::vector<CLFToolResult> m_results;      // 模型序
        bool m_concluded{false};                   // A5：任一结果 concludesTurn
        bool m_aborted{false};                     // 中断
        // §3.6 失败静默：首个 handler 失败上报（AgentLoop 层处理）；
        // 置位时未启动调用已补 [skipped] 合成结果（协议闭合保持）
        std::optional<CLFToolResult> m_firstFailure;
    };

    // 主循环（§3.2）：分组 + 屏障 + 协议闭合 + 失败静默
    Result schedule(const std::vector<CLFToolCall>& calls);

private:
    // 组内结果（§3.3 runGroup）
    struct GroupOutcome {
        std::vector<CLFToolResult> m_results;   // 组内模型序
        size_t m_consumed = 0;                  // 实际消耗调用数（惰性重分类截断/aborted 时 < 组容量）
        bool m_concluded = false;
        bool m_aborted = false;
        std::optional<CLFToolResult> m_firstFailure;
    };

    GroupOutcome runGroup(const std::vector<CLFToolCall>& calls,
                          size_t from, ToolExecMode mode);

    CLFToolExecutor& m_executor;
    int m_maxParallel;
};

} // namespace CLF::CLFCore
