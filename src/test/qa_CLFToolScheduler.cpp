// qa_CLFToolScheduler.cpp — 并发工具调度器测试（事件化重构分册 3 §五）
// 第一批：C 组分类测试（C1-C5）。
// 第二批（2026-10-14）：G/O/R/P/A/F/S/E 组——调度器主体全组。
//
// ⚠ 环境约束（重要，2026-10-14 取证钉死）：本套件**不用 boost::ut 静态
// 期执行**，改为 main 期顺序执行 + 轻量断言（QA_EXPECT）。根因：动态
// 初始化阶段（boost::ut suite 在 main 前执行的惯例）主线程与调度器
// dispatch 线程并发触发 CRT 设施初始化（logger 单例/time/string 堆锁）
// → 非确定性死锁（复现矩阵取证：ut 静态期挂、main 期 30 轮并发零复现；
// 与 std::async/std::thread 选择无关）。生产路径（AgentLoop 在 main 后
// 调度）不受影响——这是 qa 执行环境约束，不是调度器缺陷。
//
// 并发断言纪律：全部用"门闩/到达计数"确定性时序（无时间戳竞态）——
// 每个断言在测试代码注释里说明时序链。

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "CLFCore/CLFToolScheduler.hpp"
#include "CLFCore/CLFToolExecutor.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFCapabilities/FileOps/CLFFileServiceImpl.hpp"
#include "CLFTypes/CLFEventBus.hpp"

using CLF::CLFCore::CLFTool;
using CLF::CLFCore::CLFToolExecutor;
using CLF::CLFCore::CLFToolScheduler;
using CLF::CLFCore::CLFSecurityPolicy;
using CLF::CLFCore::CLFSecurityMode;
using CLF::CLFCore::ToolExecMode;
using CLF::CLFCore::ToolStats;
using CLF::CLFCore::classifyMode;
using CLF::CLFTypes::CLFEventBus;
using CLF::CLFTypes::ChannelPolicy;
using CLF::CLFTypes::ICLFOutput;

namespace {

constexpr const char* kOkJson = "{\"success\":true,\"content\":\"x\"}";
constexpr const char* kFailJson = "{\"success\":false,\"error\":\"boom\"}";

// ---- 轻量断言（main 期执行；boost::ut 静态期与本套件并发测试冲突——见头注释） ----
int g_checks = 0;
int g_failures = 0;

#define QA_EXPECT(cond)                                                        \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

// 记录输出调用的 Mock（qa_CLFToolExecutor 同款）
class MockOutput : public ICLFOutput {
public:
    std::vector<std::string> contents;
    std::string progressSummary;
    int refreshCount = 0;

    void emitContent(const std::string& t) override { contents.push_back(t); }
    void emitRaw(const std::string&) override {}
    void emitStyledLine(const std::string&, LineStyle) override {}
    void setStatus(const std::string&, int, int) override {}
    void setStatusTextOnly(const std::string&) override {}
    bool confirm(const std::string&) override { return false; }
    void onInterrupt(std::function<void()>) override {}
    void showProgress(const std::vector<std::string>&) override {}
    void finishProgress(const std::string& summary) override { progressSummary = summary; }
    void emitError(const std::string&) override {}
    void appendThinking(const std::string&) override {}
    void clearThinking() override {}
    void requestRefresh() override { ++refreshCount; }
};

CLFTool makeTool(const std::string& name) {
    CLFTool t;
    t.m_name = name;
    t.m_risk = CLF::CLFCore::CLFToolRisk::Read;
    return t;
}

// 声明并发安全的工具（dsh §2：严格 true 才并行）
CLFTool makeParallelTool(const std::string& name) {
    CLFTool t = makeTool(name);
    t.m_isConcurrencySafe = [](const std::string&) { return true; };
    return t;
}

// 调度器测试执行器装配（policy/confirm/interrupt/bus 可注入；thinkingSec
// 非空启用渐进模式——S1 用，其余 nullptr 简化输出）
CLFToolExecutor makeSchedExecutor(
    std::vector<CLFTool>& tools, MockOutput& out, CLFSecurityPolicy& policy,
    ToolStats& stats, std::atomic<bool>& interruptFlag, CLFEventBus* bus,
    std::function<bool(const std::string&)> confirmCb = nullptr,
    std::atomic<int>* thinkingSec = nullptr) {
    static CLF::CLFCore::CLFTimerLabels labels;
    static CLF::CLFCapabilities::CLFFileServiceImpl fileService;
    return CLFToolExecutor(tools, policy, std::move(confirmCb), stats,
                           &fileService, &out, &out, &interruptFlag, &labels,
                           thinkingSec, bus);
}

// 短睡眠（并发时序控制用；测试总时长控制在百毫秒级）
void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ==================== C 组：分类（第一批，2026-10-13） ====================

void testC1() {
    CLFTool t = makeTool("plain");
    QA_EXPECT(classifyMode(t, "{}") == ToolExecMode::Exclusive);
}

void testC2() {
    CLFTool t = makeTool("parallel");
    t.m_isConcurrencySafe = [](const std::string&) { return true; };
    QA_EXPECT(classifyMode(t, "{}") == ToolExecMode::Parallel);
}

void testC3() {
    CLFTool t1 = makeTool("false-decl");
    t1.m_isConcurrencySafe = [](const std::string&) { return false; };
    QA_EXPECT(classifyMode(t1, "{}") == ToolExecMode::Exclusive);

    CLFTool t2 = makeTool("throw-decl");
    // 判定内抛异常（违反 dsh §16.2 约束的第三方声明）→ catch 兜底独占
    t2.m_isConcurrencySafe = [](const std::string&) -> bool {
        throw std::runtime_error("boom");
    };
    QA_EXPECT(classifyMode(t2, "{}") == ToolExecMode::Exclusive);
}

void testC4() {
    CLFTool t = makeTool("param-sensitive");
    t.m_isConcurrencySafe = [](const std::string& argsJson) {
        // 参数含 "write" 特征 → 独占
        return argsJson.find("\"mode\":\"write\"") == std::string::npos;
    };
    QA_EXPECT(classifyMode(t, R"({"mode":"read","path":"a.txt"})")
              == ToolExecMode::Parallel);
    QA_EXPECT(classifyMode(t, R"({"mode":"write","path":"a.txt"})")
              == ToolExecMode::Exclusive);
}

void testC5() {
    CLFTool t = makeTool("meta");
    t.m_description = "读取文件内容";
    t.m_parametersSchema = R"({"type":"object","properties":{"path":{"type":"string"}}})";
    t.m_isConcurrencySafe = [](const std::string&) { return true; };
    // 并发声明不进 m_description/m_parametersSchema
    QA_EXPECT(t.m_description.find("并发") == std::string::npos);
    QA_EXPECT(t.m_description.find("concurren") == std::string::npos);
    QA_EXPECT(t.m_parametersSchema.find("concurren") == std::string::npos);
}

// ==================== G 组：分组屏障 ====================

void testG1() {
    std::vector<CLFTool> tools;
    std::atomic<int> waiting{0};
    std::atomic<bool> overlapped{false};
    std::atomic<bool> release{false};
    CLFTool t1 = makeParallelTool("g1_a");
    CLFTool t2 = makeParallelTool("g1_b");
    auto handler = [&](const std::string&) {
        // 重叠窗口：第二个到达者观察到两个同时在等 → overlapped + 放行。
        // 串行实现（bug）：第一个等 1s 超时离开（--waiting）后才轮到
        // 第二个 → overlapped 恒 false → 断言失败（兜底防挂死）
        if (++waiting == 2) { overlapped = true; release = true; }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!release.load() && std::chrono::steady_clock::now() < deadline)
            sleepMs(1);
        --waiting;
        return std::string(kOkJson);
    };
    t1.m_handler = handler;
    t2.m_handler = handler;
    tools.push_back(t1);
    tools.push_back(t2);

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, nullptr);
    CLFToolScheduler sched(executor, 2);

    CLF::CLFCore::CLFToolCall c1; c1.m_id = "a"; c1.m_name = "g1_a";
    CLF::CLFCore::CLFToolCall c2; c2.m_id = "b"; c2.m_name = "g1_b";
    auto r = sched.schedule({c1, c2});
    QA_EXPECT(overlapped.load() == true);   // 真并发铁证
    QA_EXPECT(r.m_results.size() == 2u);
}

void testG2() {
    std::vector<CLFTool> tools;
    std::mutex m;
    std::vector<std::string> order;
    auto makeH = [&](const std::string& n) {
        return [&, n](const std::string&) {
            { std::lock_guard<std::mutex> lk(m); order.push_back(n); }
            return std::string(kOkJson);
        };
    };
    CLFTool p1 = makeParallelTool("g2_p1"); p1.m_handler = makeH("p1");
    CLFTool x  = makeTool("g2_x");           x.m_handler  = makeH("x");
    CLFTool p2 = makeParallelTool("g2_p2"); p2.m_handler = makeH("p2");
    tools.push_back(p1); tools.push_back(x); tools.push_back(p2);

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, nullptr);
    CLFToolScheduler sched(executor, 2);

    CLF::CLFCore::CLFToolCall c1; c1.m_name = "g2_p1";
    CLF::CLFCore::CLFToolCall c2; c2.m_name = "g2_x";
    CLF::CLFCore::CLFToolCall c3; c3.m_name = "g2_p2";
    auto r = sched.schedule({c1, c2, c3});
    QA_EXPECT(r.m_results.size() == 3u);
    // 独占屏障：X 在 P1 完成后才启动；P2 在 X 提交后启动（组间屏障）
    QA_EXPECT(order.size() == 3u);
    QA_EXPECT(order[0] == "p1");
    QA_EXPECT(order[1] == "x");
    QA_EXPECT(order[2] == "p2");
}

void testG3() {
    std::vector<CLFTool> tools;
    std::atomic<bool> h1Done{false};
    std::atomic<bool> h2AfterH1{false};
    CLFTool t1 = makeParallelTool("g3_a");
    CLFTool t2 = makeParallelTool("g3_b");
    t1.m_handler = [&](const std::string&) { sleepMs(10); h1Done = true; return std::string(kOkJson); };
    t2.m_handler = [&](const std::string&) { h2AfterH1 = h1Done.load(); return std::string(kOkJson); };
    tools.push_back(t1);
    tools.push_back(t2);

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, nullptr);
    CLFToolScheduler sched(executor, 1);   // 安全阀：完全串行

    CLF::CLFCore::CLFToolCall c1; c1.m_name = "g3_a";
    CLF::CLFCore::CLFToolCall c2; c2.m_name = "g3_b";
    auto r = sched.schedule({c1, c2});
    QA_EXPECT(h2AfterH1.load() == true);   // 串行铁证
    QA_EXPECT(r.m_results.size() == 2u);
}

void testG4() {
    std::vector<CLFTool> tools;
    std::atomic<bool> p2Done{false};
    std::atomic<bool> p3AfterP2{false};
    CLFTool t1 = makeParallelTool("g4_p1");
    CLFTool t2 = makeParallelTool("g4_p2");
    CLFTool t3 = makeParallelTool("g4_p3");
    tools.push_back(t1);
    tools.push_back(t2);
    tools.push_back(t3);
    // h1 执行中把注册表内 t3 的声明撤回（模拟插件热卸载语义）→ t3
    // 补位时重判为 Exclusive → 组截断 → t3 独立组（t2 完成后才启动）。
    // 注意改 tools[2]（注册表元素）而非栈上 t3——findTool 查注册表
    tools[0].m_handler = [&tools](const std::string&) {
        tools[2].m_isConcurrencySafe = nullptr;   // 撤回声明
        return std::string(kOkJson);
    };
    tools[1].m_handler = [&](const std::string&) { sleepMs(50); p2Done = true; return std::string(kOkJson); };
    tools[2].m_handler = [&](const std::string&) { p3AfterP2 = p2Done.load(); return std::string(kOkJson); };

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, nullptr);
    CLFToolScheduler sched(executor, 2);
    // 时序链：cap=2 → fillPool 只启动 t1/t2；t1 快完成 → 撤回声明已生效；
    // 补位 classify(t3) → Exclusive → 组截断（consumed=2）→ t3 独立组。
    // 若实现为"分组时快照"（bug）：t3 与 t2 并发 → p3AfterP2 == false 断言失败
    CLF::CLFCore::CLFToolCall c1; c1.m_name = "g4_p1";
    CLF::CLFCore::CLFToolCall c2; c2.m_name = "g4_p2";
    CLF::CLFCore::CLFToolCall c3; c3.m_name = "g4_p3";
    auto r = sched.schedule({c1, c2, c3});
    QA_EXPECT(p3AfterP2.load() == true);   // 惰性重分类铁证
    QA_EXPECT(r.m_results.size() == 3u);
}

// ==================== O 组：模型序 ====================

void testO1() {
    std::vector<CLFTool> tools;
    std::atomic<bool> h2Done{false};
    std::atomic<bool> h1AfterH2{false};
    CLFTool t1 = makeParallelTool("o1_slow");
    CLFTool t2 = makeParallelTool("o1_fast");
    t1.m_handler = [&](const std::string&) {
        sleepMs(50);
        h1AfterH2 = h2Done.load();   // h1 完成时 h2 早已完成（后发先至）
        return std::string("{\"success\":true,\"content\":\"slow\"}");
    };
    t2.m_handler = [&](const std::string&) {
        h2Done = true;
        return std::string("{\"success\":true,\"content\":\"fast\"}");
    };
    tools.push_back(t1);
    tools.push_back(t2);

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, nullptr);
    CLFToolScheduler sched(executor, 2);

    CLF::CLFCore::CLFToolCall c1; c1.m_id = "slow"; c1.m_name = "o1_slow";
    CLF::CLFCore::CLFToolCall c2; c2.m_id = "fast"; c2.m_name = "o1_fast";
    auto r = sched.schedule({c1, c2});
    QA_EXPECT(h1AfterH2.load() == true);   // 完成乱序实证
    // 结果序 = 模型序（慢的队首在 results[0]）
    QA_EXPECT(r.m_results.size() == 2u);
    QA_EXPECT(r.m_results[0].m_name == "o1_slow");
    QA_EXPECT(r.m_results[1].m_name == "o1_fast");
    QA_EXPECT(r.m_results[0].m_content.find("slow") != std::string::npos);
}

void testO2() {
    std::vector<CLFTool> tools;
    std::atomic<bool> h2Done{false};
    CLFTool t1 = makeParallelTool("o2_slow");
    CLFTool t2 = makeParallelTool("o2_fast");
    t1.m_handler = [&](const std::string&) { sleepMs(50); return std::string(kOkJson); };
    t2.m_handler = [&](const std::string&) { h2Done = true; return std::string(kOkJson); };
    tools.push_back(t1);
    tools.push_back(t2);

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, nullptr);
    CLFToolScheduler sched(executor, 2);

    CLF::CLFCore::CLFToolCall c1; c1.m_name = "o2_slow";
    CLF::CLFCore::CLFToolCall c2; c2.m_name = "o2_fast";
    auto r = sched.schedule({c1, c2});
    QA_EXPECT(h2Done.load() == true);      // 快者确实先完成
    QA_EXPECT(r.m_results[0].m_name == "o2_slow");   // 但提交仍队首先行
}

// ==================== R 组：滚动池 ====================

void testR1() {
    std::vector<CLFTool> tools;
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    std::atomic<int> arrived{0};
    std::atomic<bool> release{false};
    CLFTool t1 = makeParallelTool("r1_a");
    CLFTool t2 = makeParallelTool("r1_b");
    CLFTool t3 = makeParallelTool("r1_c");
    auto handler = [&](const std::string&) {
        int a = ++active;
        int old = peak.load();
        while (a > old && !peak.compare_exchange_weak(old, a)) {}
        if (++arrived == 2) release = true;   // 前两个到达即放行
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!release.load() && std::chrono::steady_clock::now() < deadline)
            sleepMs(1);
        --active;   // 返回前减计数——补位启动的 t3 不会与 t1/t2 重叠
        return std::string(kOkJson);
    };
    t1.m_handler = handler;
    t2.m_handler = handler;
    t3.m_handler = handler;
    tools.push_back(t1);
    tools.push_back(t2);
    tools.push_back(t3);

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, nullptr);
    CLFToolScheduler sched(executor, 2);

    CLF::CLFCore::CLFToolCall c1; c1.m_name = "r1_a";
    CLF::CLFCore::CLFToolCall c2; c2.m_name = "r1_b";
    CLF::CLFCore::CLFToolCall c3; c3.m_name = "r1_c";
    auto r = sched.schedule({c1, c2, c3});
    QA_EXPECT(peak.load() == 2);   // 严格 = 2：前两个重叠在途，第三个补位
    QA_EXPECT(r.m_results.size() == 3u);
}

void testR2() {
    std::vector<CLFTool> tools;
    std::atomic<bool> h1Done{false};
    std::atomic<bool> h2Done{false};
    std::atomic<bool> h3AfterH1{false};
    CLFTool t1 = makeParallelTool("r2_a");
    CLFTool t2 = makeParallelTool("r2_b");
    CLFTool t3 = makeParallelTool("r2_c");
    t1.m_handler = [&](const std::string&) { sleepMs(10); h1Done = true; return std::string(kOkJson); };
    t2.m_handler = [&](const std::string&) { sleepMs(60); h2Done = true; return std::string(kOkJson); };
    t3.m_handler = [&](const std::string&) { h3AfterH1 = h1Done.load(); return std::string(kOkJson); };
    tools.push_back(t1);
    tools.push_back(t2);
    tools.push_back(t3);

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, nullptr);
    CLFToolScheduler sched(executor, 2);
    // 时序链：t1/t2 在途（cap 满）→ t1 完成（10ms）→ 提交 → 补位启动
    // t3（t2 仍在途）→ h3 启动时 h1Done 必为 true
    CLF::CLFCore::CLFToolCall c1; c1.m_name = "r2_a";
    CLF::CLFCore::CLFToolCall c2; c2.m_name = "r2_b";
    CLF::CLFCore::CLFToolCall c3; c3.m_name = "r2_c";
    auto r = sched.schedule({c1, c2, c3});
    QA_EXPECT(h3AfterH1.load() == true);   // 补位铁证
    QA_EXPECT(h2Done.load() == true);
    QA_EXPECT(r.m_results.size() == 3u);
}

// ==================== P 组：确认串行化（安全关键，新增） ====================

void testP1() {
    std::vector<CLFTool> tools;
    CLFTool t1 = makeParallelTool("p1_a");
    CLFTool t2 = makeParallelTool("p1_b");
    t1.m_risk = CLF::CLFCore::CLFToolRisk::Command;   // Edit 模式触发确认
    t2.m_risk = CLF::CLFCore::CLFToolRisk::Command;
    t1.m_handler = [](const std::string&) { return std::string(kOkJson); };
    t2.m_handler = [](const std::string&) { return std::string(kOkJson); };
    tools.push_back(t1);
    tools.push_back(t2);

    // 串行检测：t1 的 confirm 内停留 50ms——若实现并行 prepare（bug），
    // t2 的 confirm 会在此窗口内被调用（c1StillInConfirm 仍 true）
    std::atomic<bool> c1StillInConfirm{true};
    std::atomic<bool> c2DuringC1{false};
    auto confirmCb = [&](const std::string& prompt) {
        if (prompt.find("p1_a") != std::string::npos) {
            sleepMs(50);
            c1StillInConfirm = false;   // 返回前清标志
            return true;
        }
        if (prompt.find("p1_b") != std::string::npos) {
            c2DuringC1 = c1StillInConfirm.load();
            return true;
        }
        return true;
    };

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Edit);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag,
                                      nullptr, confirmCb);
    CLFToolScheduler sched(executor, 2);

    CLF::CLFCore::CLFToolCall c1; c1.m_name = "p1_a";
    CLF::CLFCore::CLFToolCall c2; c2.m_name = "p1_b";
    auto r = sched.schedule({c1, c2});
    QA_EXPECT(c2DuringC1.load() == false);   // 确认串行化铁证
    QA_EXPECT(r.m_results.size() == 2u);
    QA_EXPECT(r.m_results[0].m_content.find("Denied") == std::string::npos);
}

void testP2() {
    std::vector<CLFTool> tools;
    CLFTool t1 = makeParallelTool("p2_a");
    CLFTool t2 = makeParallelTool("p2_b");
    t1.m_risk = CLF::CLFCore::CLFToolRisk::Command;
    t2.m_risk = CLF::CLFCore::CLFToolRisk::Command;
    t1.m_handler = [](const std::string&) { return std::string(kOkJson); };
    std::atomic<bool> h2Called{false};
    t2.m_handler = [&](const std::string&) { h2Called = true; return std::string(kOkJson); };
    tools.push_back(t1);
    tools.push_back(t2);

    auto confirmCb = [&](const std::string& prompt) {
        return prompt.find("p2_a") == std::string::npos;   // 只拒绝第一个
    };

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Edit);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag,
                                      nullptr, confirmCb);
    CLFToolScheduler sched(executor, 2);

    CLF::CLFCore::CLFToolCall c1; c1.m_id = "deny"; c1.m_name = "p2_a";
    CLF::CLFCore::CLFToolCall c2; c2.m_id = "ok";   c2.m_name = "p2_b";
    auto r = sched.schedule({c1, c2});
    QA_EXPECT(r.m_results.size() == 2u);
    QA_EXPECT(r.m_results[0].m_content.find("[Denied by user]") != std::string::npos);
    QA_EXPECT(h2Called.load() == true);   // 拒绝不阻断后续
    QA_EXPECT(r.m_results[1].m_content == std::string(kOkJson));
    QA_EXPECT(!r.m_firstFailure);   // denied 非 handler 失败——不触发失败静默
}

// ==================== A 组：取消 ====================

void testA1() {
    std::vector<CLFTool> tools;
    CLFTool t = makeParallelTool("a1_t");
    std::atomic<bool> hCalled{false};
    t.m_handler = [&](const std::string&) { hCalled = true; return std::string(kOkJson); };
    tools.push_back(t);

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{true};   // 入口即中断
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, nullptr);
    CLFToolScheduler sched(executor, 2);

    CLF::CLFCore::CLFToolCall c1; c1.m_id = "x"; c1.m_name = "a1_t";
    CLF::CLFCore::CLFToolCall c2; c2.m_id = "y"; c2.m_name = "a1_t";
    auto r = sched.schedule({c1, c2});
    QA_EXPECT(r.m_aborted == true);
    QA_EXPECT(hCalled.load() == false);
    QA_EXPECT(r.m_results.size() == 2u);
    QA_EXPECT(r.m_results[0].m_content.find("[interrupted]") != std::string::npos);
    QA_EXPECT(r.m_results[1].m_content.find("[interrupted]") != std::string::npos);
}

void testA2() {
    std::vector<CLFTool> tools;
    std::atomic<int> arrived{0};
    std::atomic<bool> interruptFlag{false};
    std::atomic<bool> release{false};
    CLFTool t = makeParallelTool("a2_t");
    auto handler = [&](const std::string&) {
        // 确定性时序（门闩版）：第二个到达者置中断 + 放行；第一个等
        // release——**两个都在途时中断必已置位**。关键修正：worker 在
        // release 前不返回 → 补位（fillPool 第三轮）发生在两个都到达
        // 之后 → 中断检查必拦下第三个（初版"后到者置位"在 worker2 慢
        // 启动时补位早于置位——并行负载实抓）
        if (++arrived == 2) { interruptFlag = true; release = true; }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!release.load() && std::chrono::steady_clock::now() < deadline)
            sleepMs(1);
        return std::string(kOkJson);
    };
    t.m_handler = handler;
    tools.push_back(t);

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, nullptr);
    CLFToolScheduler sched(executor, 2);

    CLF::CLFCore::CLFToolCall c1; c1.m_id = "a"; c1.m_name = "a2_t";
    CLF::CLFCore::CLFToolCall c2; c2.m_id = "b"; c2.m_name = "a2_t";
    CLF::CLFCore::CLFToolCall c3; c3.m_id = "c"; c3.m_name = "a2_t";
    auto r = sched.schedule({c1, c2, c3});
    QA_EXPECT(r.m_aborted == true);
    QA_EXPECT(arrived.load() == 2);   // 只有前两个在途
    QA_EXPECT(r.m_results.size() == 3u);
    // 已启动排空照常提交（真实结果保留——I1b 语义）
    QA_EXPECT(r.m_results[0].m_content == std::string(kOkJson));
    QA_EXPECT(r.m_results[1].m_content == std::string(kOkJson));
    QA_EXPECT(r.m_results[2].m_content.find("[interrupted]") != std::string::npos);
}

void testA3() {
    std::vector<CLFTool> tools;
    std::atomic<bool> interruptFlag{false};
    CLFTool p = makeParallelTool("a3_p");
    p.m_handler = [&](const std::string&) { interruptFlag = true; return std::string(kOkJson); };
    CLFTool x = makeTool("a3_x");   // 独占（未声明）
    std::atomic<bool> xCalled{false};
    x.m_handler = [&](const std::string&) { xCalled = true; return std::string(kOkJson); };
    tools.push_back(p);
    tools.push_back(x);

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, nullptr);
    CLFToolScheduler sched(executor, 2);

    CLF::CLFCore::CLFToolCall c1; c1.m_name = "a3_p";
    CLF::CLFCore::CLFToolCall c2; c2.m_name = "a3_x";
    auto r = sched.schedule({c1, c2});
    QA_EXPECT(r.m_aborted == true);
    QA_EXPECT(xCalled.load() == false);   // 屏障不执行
    QA_EXPECT(r.m_results.size() == 2u);
    QA_EXPECT(r.m_results[0].m_content == std::string(kOkJson));
    QA_EXPECT(r.m_results[1].m_content.find("[interrupted]") != std::string::npos);
}

// ==================== F 组：失败静默 ====================

void testF1() {
    std::vector<CLFTool> tools;
    CLFTool t1 = makeParallelTool("f1_a");
    CLFTool t2 = makeParallelTool("f1_b");
    CLFTool t3 = makeParallelTool("f1_c");
    std::atomic<bool> h3Called{false};
    t1.m_handler = [](const std::string&) { return std::string(kFailJson); };
    // t2 慢：保证 t1 先 settle（失败信号先于任何补位——调度器 fail-fast
    // 扫描在 fillPool 头拦截；若 t2 先 settle 成功则补位 t3 属合法滚动池
    // 行为——dsh 语义"失败前已派发的不撤销"）
    t2.m_handler = [](const std::string&) { sleepMs(40); return std::string(kOkJson); };
    t3.m_handler = [&](const std::string&) { h3Called = true; return std::string(kOkJson); };
    tools.push_back(t1);
    tools.push_back(t2);
    tools.push_back(t3);

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, nullptr);
    CLFToolScheduler sched(executor, 2);
    // 时序链：t1/t2 并发在途 → t1 先 settle 失败（t2 仍在途）→
    // 失败信号（settle 扫描）→ 停止派发（t3 未启动）→ t2 排空提交 →
    // 主循环补 skipped(t3)
    CLF::CLFCore::CLFToolCall c1; c1.m_name = "f1_a";
    CLF::CLFCore::CLFToolCall c2; c2.m_name = "f1_b";
    CLF::CLFCore::CLFToolCall c3; c3.m_name = "f1_c";
    auto r = sched.schedule({c1, c2, c3});
    QA_EXPECT(h3Called.load() == false);   // 停止派发铁证
    QA_EXPECT(r.m_results.size() == 3u);
    QA_EXPECT(r.m_results[0].m_content == std::string(kFailJson));
    QA_EXPECT(r.m_results[1].m_content == std::string(kOkJson));   // 在途排空
    QA_EXPECT(r.m_results[2].m_content.find("[skipped]") != std::string::npos);
    QA_EXPECT(r.m_firstFailure.has_value());
    QA_EXPECT(r.m_firstFailure->m_content.find("boom") != std::string::npos);
    QA_EXPECT(!r.m_aborted);   // 失败静默 ≠ 中断
}

void testF2() {
    std::vector<CLFTool> tools;
    CLFTool t1 = makeParallelTool("f2_a");
    CLFTool t2 = makeParallelTool("f2_b");
    CLFTool t3 = makeParallelTool("f2_c");
    t1.m_handler = [](const std::string&) { return std::string(kFailJson); };
    // t2 慢：t1 失败时 t2 仍在途——排空语义的实证窗口
    t2.m_handler = [](const std::string&) { sleepMs(40); return std::string(kOkJson); };
    std::atomic<bool> h3Called{false};
    t3.m_handler = [&](const std::string&) { h3Called = true; return std::string(kOkJson); };
    tools.push_back(t1);
    tools.push_back(t2);
    tools.push_back(t3);

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, nullptr);
    CLFToolScheduler sched(executor, 2);

    CLF::CLFCore::CLFToolCall c1; c1.m_name = "f2_a";
    CLF::CLFCore::CLFToolCall c2; c2.m_name = "f2_b";
    CLF::CLFCore::CLFToolCall c3; c3.m_name = "f2_c";
    auto r = sched.schedule({c1, c2, c3});
    QA_EXPECT(h3Called.load() == false);
    QA_EXPECT(r.m_results.size() == 3u);
    QA_EXPECT(r.m_results[0].m_content == std::string(kFailJson));  // 模型序队首
    QA_EXPECT(r.m_results[1].m_content == std::string(kOkJson));    // 慢在途排空后照常提交
    QA_EXPECT(r.m_results[2].m_content.find("[skipped]") != std::string::npos);
    QA_EXPECT(r.m_firstFailure.has_value());
}

// ==================== S 组：统计 ====================

void testS1() {
    std::vector<CLFTool> tools;
    CLFTool r1 = makeParallelTool("s1_r1");
    CLFTool r2 = makeParallelTool("s1_r2");
    CLFTool s1 = makeParallelTool("s1_s");
    r1.m_isRead = true;
    r2.m_isRead = true;
    s1.m_isSearch = true;
    r1.m_handler = [](const std::string&) { return std::string(kOkJson); };
    r2.m_handler = [](const std::string&) { return std::string(kOkJson); };
    s1.m_handler = [](const std::string&) { return std::string(kOkJson); };
    tools.push_back(r1);
    tools.push_back(r2);
    tools.push_back(s1);

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    std::atomic<int> thinkingSec{3};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag,
                                      nullptr, nullptr, &thinkingSec);
    CLFToolScheduler sched(executor, 3);

    CLF::CLFCore::CLFToolCall c1; c1.m_name = "s1_r1";
    CLF::CLFCore::CLFToolCall c2; c2.m_name = "s1_r2";
    CLF::CLFCore::CLFToolCall c3; c3.m_name = "s1_s";
    auto r = sched.schedule({c1, c2, c3});
    QA_EXPECT(r.m_results.size() == 3u);
    QA_EXPECT(out.progressSummary.find("3 工具") != std::string::npos);
    QA_EXPECT(out.progressSummary.find("read 2") != std::string::npos);
    QA_EXPECT(out.progressSummary.find("search 1") != std::string::npos);
    // m_stats 会话累计写回（finishBatch）
    QA_EXPECT(stats.readCount == 2);
    QA_EXPECT(stats.searchCount == 1);
    QA_EXPECT(stats.totalCalls == 3);
}

// ==================== E 组：事件 ====================

void testE1() {
    std::vector<CLFTool> tools;
    CLFTool t = makeParallelTool("e1_t");
    t.m_handler = [](const std::string&) { return std::string(kOkJson); };
    tools.push_back(t);

    CLFEventBus bus(false);   // qa 纪律 R2：startDispatcher=false 手动模式
    std::vector<CLF::CLFTypes::CLFAgentEvent> events;
    bus.subscribe([&](const CLF::CLFTypes::CLFAgentEvent& ev) {
        events.push_back(ev);
    }, ChannelPolicy::Lossless);   // 订阅句柄随 bus 析构（测试期有效）

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, &bus);
    CLFToolScheduler sched(executor, 2);

    CLF::CLFCore::CLFToolCall c1; c1.m_id = "call_a"; c1.m_name = "e1_t";
    CLF::CLFCore::CLFToolCall c2; c2.m_id = "call_b"; c2.m_name = "e1_t";
    auto r = sched.schedule({c1, c2});
    QA_EXPECT(r.m_results.size() == 2u);
    bus.drainAll();

    std::vector<std::string> startedIds, finishedIds;
    for (const auto& ev : events) {
        if (ev.m_type == CLF::CLFTypes::CLFEventType::ToolStarted)
            startedIds.push_back(ev.m_callId);
        if (ev.m_type == CLF::CLFTypes::CLFEventType::ToolFinished)
            finishedIds.push_back(ev.m_callId);
    }
    QA_EXPECT(startedIds.size() == 2u);
    QA_EXPECT(finishedIds.size() == 2u);
    // 配对：两个集合一致（Started/Finished 一一对应）
    for (const auto& id : startedIds)
        QA_EXPECT(std::find(finishedIds.begin(), finishedIds.end(), id)
                  != finishedIds.end());
    // Started 在 prepare（模型序）——[call_a, call_b]
    QA_EXPECT(startedIds[0] == "call_a");
    QA_EXPECT(startedIds[1] == "call_b");
}

void testE2() {
    std::vector<CLFTool> tools;
    CLFTool t1 = makeParallelTool("e2_slow");
    CLFTool t2 = makeParallelTool("e2_fast");
    t1.m_handler = [](const std::string&) { sleepMs(50); return std::string(kOkJson); };
    t2.m_handler = [](const std::string&) { return std::string(kOkJson); };
    tools.push_back(t1);
    tools.push_back(t2);

    CLFEventBus bus(false);
    std::vector<CLF::CLFTypes::CLFAgentEvent> finished;
    bus.subscribe([&](const CLF::CLFTypes::CLFAgentEvent& ev) {
        if (ev.m_type == CLF::CLFTypes::CLFEventType::ToolFinished)
            finished.push_back(ev);
    }, ChannelPolicy::Lossless);   // 订阅句柄随 bus 析构（测试期有效）

    MockOutput out;
    CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    ToolStats stats;
    std::atomic<bool> interruptFlag{false};
    auto executor = makeSchedExecutor(tools, out, policy, stats, interruptFlag, &bus);
    CLFToolScheduler sched(executor, 2);

    CLF::CLFCore::CLFToolCall c1; c1.m_id = "slow"; c1.m_name = "e2_slow";
    CLF::CLFCore::CLFToolCall c2; c2.m_id = "fast"; c2.m_name = "e2_fast";
    auto r = sched.schedule({c1, c2});
    bus.drainAll();

    QA_EXPECT(finished.size() == 2u);
    // 队首慢（50ms）——fast 先完成，但 ToolFinished 仍按模型序发射
    QA_EXPECT(finished[0].m_callId == "slow");
    QA_EXPECT(finished[1].m_callId == "fast");
}

} // anonymous namespace

int main() {
    struct { const char* name; void (*fn)(); } tests[] = {
        {"C1 未声明 = 独占（fail-closed 默认）", testC1},
        {"C2 声明 true = 并行", testC2},
        {"C3 非 true 返回（含异常）= 独占", testC3},
        {"C4 逐参数判定：读模式并行、写模式独占", testC4},
        {"C5 判定不进工具描述/schema", testC5},
        {"G1 并行兄弟真并发（重叠窗口门闩）", testG1},
        {"G2 独占屏障形成 3 组", testG2},
        {"G3 maxParallel=1 完全串行", testG3},
        {"G4 惰性重分类（执行中声明变更）", testG4},
        {"O1 后发先至仍按模型序提交", testO1},
        {"O2 队首慢阻塞后续提交（head-of-line）", testO2},
        {"R1 并发上限（cap=2 峰值并发）", testR1},
        {"R2 完成补位（settle 后新启动）", testR2},
        {"P1 并行组内 confirm 串行", testP1},
        {"P2 confirm 拒绝照常提交", testP2},
        {"A1 入口即中断（协议闭合 I1）", testA1},
        {"A2 执行中中断：已启动排空", testA2},
        {"A3 独占屏障不执行", testA3},
        {"F1 首个失败停止派发", testF1},
        {"F2 排空在途后上报首个失败", testF2},
        {"S1 并行执行统计累计正确", testS1},
        {"E1 ToolStarted/ToolFinished callId 配对", testE1},
        {"E2 完成乱序但事件序 = 模型序", testE2},
    };

    for (const auto& t : tests) {
        int before = g_failures;
        t.fn();
        std::printf("[%s] %s\n", g_failures == before ? "PASS" : "FAIL", t.name);
    }
    std::printf("qa_CLFToolScheduler: %d tests, %d checks, %d failures\n",
                static_cast<int>(sizeof(tests) / sizeof(tests[0])), g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
