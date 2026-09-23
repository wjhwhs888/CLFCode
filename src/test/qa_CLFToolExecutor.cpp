// qa_CLFToolExecutor.cpp — 工具执行器展示行为测试（M2）
// T5:  summary 增强（P1-2：总工具数 + search 计数）
// F10: 读工具失败可见性（渐进模式失败 → 永久 ✗ 行）

#include <boost/ut.hpp>

#include <atomic>
#include <string>
#include <vector>

#include "CLFCore/CLFToolExecutor.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFCapabilities/FileOps/CLFFileServiceImpl.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFTool;
using CLF::CLFCore::CLFToolExecutor;
using CLF::CLFCore::CLFSecurityPolicy;
using CLF::CLFCore::CLFSecurityMode;
using CLF::CLFTypes::ICLFOutput;

namespace {

// 记录输出调用的 Mock（重点捕获 emitContent 与 finishProgress）
class MockOutput : public ICLFOutput {
public:
    std::vector<std::string> contents;
    std::string progressSummary;

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
    void requestRefresh() override { ++refreshCount; }   // T3 计数

    int refreshCount = 0;

    bool anyContentContains(const std::string& needle) const {
        for (const auto& c : contents)
            if (c.find(needle) != std::string::npos) return true;
        return false;
    }
};

// 构造 executor：auto 模式（全放行）、渐进模式开启
CLFToolExecutor makeExecutor(std::vector<CLFTool>& tools, MockOutput& out,
                             std::atomic<int>& thinkingSec,
                             CLF::CLFCore::ToolStats& stats) {
    static CLF::CLFCore::CLFTimerLabels labels;  // 默认 "thought"/"thinking" 标签
    static CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    static std::atomic<bool> interruptFlag{false};
    // C1：无状态转调层（平凡构造，测试进程生命周期；与上 static 同模式）
    static CLF::CLFCapabilities::CLFFileServiceImpl fileService;
    return CLFToolExecutor(tools, policy, nullptr, stats, &fileService,
                           &out, &out, &interruptFlag, &labels, &thinkingSec);
}

// 构造带可注入中断标志的 executor（协议闭合用例 I1a/I1b 需可控 flag）
CLFToolExecutor makeInterruptibleExecutor(std::vector<CLFTool>& tools,
                                          MockOutput& out,
                                          std::atomic<int>& thinkingSec,
                                          CLF::CLFCore::ToolStats& stats,
                                          std::atomic<bool>& interruptFlag) {
    static CLF::CLFCore::CLFTimerLabels labels;
    static CLFSecurityPolicy policy(CLFSecurityMode::Auto);
    static CLF::CLFCapabilities::CLFFileServiceImpl fileService;
    return CLFToolExecutor(tools, policy, nullptr, stats, &fileService,
                           &out, &out, &interruptFlag, &labels, &thinkingSec);
}

} // anonymous namespace

const boost::ut::suite<"CLFToolExecutor"> tests = [] {
    "T5 summary 增强：总工具数 + read/search 计数"_test = [] {
        std::vector<CLFTool> tools;
        CLFTool readTool;
        readTool.m_name = "read_file";
        readTool.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        readTool.m_isRead = true;   // B1：能力标签（统计桶随标签，测试打标）
        readTool.m_handler = [](const std::string&) { return "{\"success\":true,\"content\":\"data\"}"; };
        tools.push_back(readTool);

        CLFTool searchTool;
        searchTool.m_name = "search_content";
        searchTool.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        searchTool.m_isSearch = true;   // B1：能力标签
        searchTool.m_handler = [](const std::string&) { return "{\"success\":true,\"content\":\"found\"}"; };
        tools.push_back(searchTool);

        MockOutput out;
        std::atomic<int> thinkingSec{5};
        CLF::CLFCore::ToolStats stats;
        auto executor = makeExecutor(tools, out, thinkingSec, stats);

        CLF::CLFCore::CLFToolCall c1; c1.m_name = "read_file";
        CLF::CLFCore::CLFToolCall c2; c2.m_name = "search_content";
        executor.execute({c1, c2});

        expect(out.progressSummary.find("2 工具") != std::string::npos);
        expect(out.progressSummary.find("read 1") != std::string::npos);
        expect(out.progressSummary.find("search 1") != std::string::npos);
    };

    "F10 读工具失败：渐进模式保留永久 ✗ 行"_test = [] {
        std::vector<CLFTool> tools;
        CLFTool failRead;
        failRead.m_name = "read_file";
        failRead.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        failRead.m_handler = [](const std::string&) { return R"({"success":false,"error":"boom"})"; };
        tools.push_back(failRead);

        MockOutput out;
        std::atomic<int> thinkingSec{3};
        CLF::CLFCore::ToolStats stats;
        auto executor = makeExecutor(tools, out, thinkingSec, stats);

        CLF::CLFCore::CLFToolCall c1; c1.m_name = "read_file";
        executor.execute({c1});

        // 失败行含 ✗ 与错误原因（"scroll for full detail" 模式）
        expect(out.anyContentContains("✗"));
        expect(out.anyContentContains("boom"));
    };

    "渐进模式读工具成功：无永久 ✓ 行（降噪保持）"_test = [] {
        std::vector<CLFTool> tools;
        CLFTool okRead;
        okRead.m_name = "read_file";
        okRead.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        okRead.m_handler = [](const std::string&) { return "{\"success\":true,\"content\":\"data\"}"; };
        tools.push_back(okRead);

        MockOutput out;
        std::atomic<int> thinkingSec{2};
        CLF::CLFCore::ToolStats stats;
        auto executor = makeExecutor(tools, out, thinkingSec, stats);

        CLF::CLFCore::CLFToolCall c1; c1.m_name = "read_file";
        executor.execute({c1});

        expect(!out.anyContentContains("✓"));
    };

    "T11 summary token 字段：有 usage 追加 12.3k tok / 无 usage 省略"_test = [] {
        std::vector<CLFTool> tools;
        CLFTool okRead;
        okRead.m_name = "read_file";
        okRead.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        okRead.m_handler = [](const std::string&) { return "{\"success\":true,\"content\":\"data\"}"; };
        tools.push_back(okRead);

        // 有 usage：预置 totalTokens=12345 → "12.3k tok"
        {
            MockOutput out;
            std::atomic<int> thinkingSec{2};
            CLF::CLFCore::ToolStats stats;
            stats.totalTokens = 12345;
            auto executor = makeExecutor(tools, out, thinkingSec, stats);
            CLF::CLFCore::CLFToolCall c1; c1.m_name = "read_file";
            executor.execute({c1});
            expect(out.progressSummary.find("12.3k tok") != std::string::npos);
        }
        // 无 usage：totalTokens=0 → 字段整体省略
        {
            MockOutput out;
            std::atomic<int> thinkingSec{2};
            CLF::CLFCore::ToolStats stats;
            auto executor = makeExecutor(tools, out, thinkingSec, stats);
            CLF::CLFCore::CLFToolCall c1; c1.m_name = "read_file";
            executor.execute({c1});
            expect(out.progressSummary.find(" tok") == std::string::npos);
        }
    };

    "T12 工具循环每次迭代末触发 requestRefresh（T3，2026-09-02）"_test = [] {
        std::vector<CLFTool> tools;
        CLFTool okRead;
        okRead.m_name = "read_file";
        okRead.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        okRead.m_handler = [](const std::string&) { return "{\"success\":true,\"content\":\"data\"}"; };
        tools.push_back(okRead);

        MockOutput out;
        std::atomic<int> thinkingSec{2};
        CLF::CLFCore::ToolStats stats;
        auto executor = makeExecutor(tools, out, thinkingSec, stats);

        CLF::CLFCore::CLFToolCall c1; c1.m_name = "read_file";
        CLF::CLFCore::CLFToolCall c2; c2.m_name = "read_file";
        executor.execute({c1, c2});
        // 每次迭代末一次（todo_write 等状态类工具返回即重绘，§3.4）
        expect(out.refreshCount == 2_i);

        // 单个工具也刷新一次（含 continue 提前退出分支——RAII 覆盖）
        MockOutput out2;
        CLF::CLFCore::ToolStats stats2;
        auto executor2 = makeExecutor(tools, out2, thinkingSec, stats2);
        CLF::CLFCore::CLFToolCall c3; c3.m_name = "not_found_tool";
        executor2.execute({c3});
        expect(out2.refreshCount == 1_i);
    };

    "A5-1 concludesTurn 静态声明：成功路径 result 带标志"_test = [] {
        std::vector<CLFTool> tools;
        CLFTool done;
        done.m_name = "finish_here";
        done.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        done.m_concludesTurn = true;   // 注册时静态声明
        done.m_handler = [](const std::string&) { return "done"; };
        tools.push_back(done);

        MockOutput out;
        std::atomic<int> thinkingSec{2};
        CLF::CLFCore::ToolStats stats;
        auto executor = makeExecutor(tools, out, thinkingSec, stats);

        CLF::CLFCore::CLFToolCall c1; c1.m_name = "finish_here";
        auto results = executor.execute({c1});
        expect(results.size() == 1_ul);
        expect(results[0].m_concludesTurn);   // 成功路径复制声明
    };

    "A5-2 concludesTurn 静态声明：未声明工具 result 标志 false"_test = [] {
        std::vector<CLFTool> tools;
        CLFTool plain;
        plain.m_name = "read_file";
        plain.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        plain.m_handler = [](const std::string&) { return "ok"; };
        tools.push_back(plain);

        MockOutput out;
        std::atomic<int> thinkingSec{2};
        CLF::CLFCore::ToolStats stats;
        auto executor = makeExecutor(tools, out, thinkingSec, stats);

        CLF::CLFCore::CLFToolCall c1; c1.m_name = "read_file";
        auto results = executor.execute({c1});
        expect(results.size() == 1_ul);
        expect(!results[0].m_concludesTurn);
    };

    "A5-3 concludesTurn：失败路径不复制（工具未找到保持 false）"_test = [] {
        std::vector<CLFTool> tools;   // 空注册表 → 查找失败
        MockOutput out;
        std::atomic<int> thinkingSec{2};
        CLF::CLFCore::ToolStats stats;
        auto executor = makeExecutor(tools, out, thinkingSec, stats);

        CLF::CLFCore::CLFToolCall c1; c1.m_name = "not_found_tool";
        auto results = executor.execute({c1});
        expect(results.size() == 1_ul);
        expect(!results[0].m_concludesTurn);   // 失败不提前结束回合（§四 T2）
    };

    // ========== 协议闭合（设计-中断时效性 §十一，2026-09-23） ==========

    "I1a 中断协议闭合：入口即中断——所有调用补未执行结果（声明数 == 结果数）"_test = [] {
        std::vector<CLFTool> tools;
        CLFTool t;
        t.m_name = "read_file";
        t.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        t.m_handler = [](const std::string&) { return "{\"success\":true,\"content\":\"data\"}"; };
        tools.push_back(t);

        MockOutput out;
        std::atomic<int> thinkingSec{2};
        CLF::CLFCore::ToolStats stats;
        std::atomic<bool> interrupted{true};   // 入口即中断
        auto executor = makeInterruptibleExecutor(tools, out, thinkingSec, stats, interrupted);

        CLF::CLFCore::CLFToolCall c1; c1.m_id = "call_a"; c1.m_name = "read_file";
        CLF::CLFCore::CLFToolCall c2; c2.m_id = "call_b"; c2.m_name = "read_file";
        auto results = executor.execute({c1, c2});
        // 不变量 I1：任何时刻 tool_calls 声明数 == tool 结果数（否则下一次
        // 请求被 API 拒绝——"providers reject dangling assistant calls"）
        expect(results.size() == 2_ul);
        expect(results[0].m_toolCallId == "call_a");
        expect(results[1].m_toolCallId == "call_b");
        // [interrupted] 稳定前缀为 qa 锚点；不得是 [Denied by user]（另一语义）
        expect(results[0].m_content.find("[interrupted]") != std::string::npos);
        expect(results[1].m_content.find("[interrupted]") != std::string::npos);
        expect(results[0].m_content.find("未执行") != std::string::npos);
    };

    "I1b 中断协议闭合：执行中途中断——已执行保留真实结果、剩余补未执行"_test = [] {
        std::vector<CLFTool> tools;
        CLFTool t;
        t.m_name = "read_file";
        t.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        std::atomic<bool> interrupted{false};
        // 第一个工具执行时置位中断（模拟执行中途按 ESC）→ 第二个被批内检查拦下
        t.m_handler = [&interrupted](const std::string&) {
            interrupted = true;
            return "{\"success\":true,\"content\":\"data\"}";
        };
        tools.push_back(t);

        MockOutput out;
        std::atomic<int> thinkingSec{2};
        CLF::CLFCore::ToolStats stats;
        auto executor = makeInterruptibleExecutor(tools, out, thinkingSec, stats, interrupted);

        CLF::CLFCore::CLFToolCall c1; c1.m_id = "call_a"; c1.m_name = "read_file";
        CLF::CLFCore::CLFToolCall c2; c2.m_id = "call_b"; c2.m_name = "read_file";
        auto results = executor.execute({c1, c2});
        expect(results.size() == 2_ul);
        // 已执行的真实结果保留；被跳过的补"未执行"文案
        expect(results[0].m_toolCallId == "call_a");
        expect(results[0].m_content.find("data") != std::string::npos);
        expect(results[1].m_toolCallId == "call_b");
        expect(results[1].m_content.find("[interrupted]") != std::string::npos);
    };
};

int main() {}
