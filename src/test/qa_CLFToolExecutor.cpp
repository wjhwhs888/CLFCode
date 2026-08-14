// qa_CLFToolExecutor.cpp — 工具执行器展示行为测试（M2）
// T5:  summary 增强（P1-2：总工具数 + search 计数）
// F10: 读工具失败可见性（渐进模式失败 → 永久 ✗ 行）

#include <boost/ut.hpp>

#include <atomic>
#include <string>
#include <vector>

#include "CLFCore/CLFToolExecutor.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"

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
    return CLFToolExecutor(tools, policy, nullptr, stats, &out, &interruptFlag,
                           &labels, &thinkingSec);
}

} // anonymous namespace

const boost::ut::suite<"CLFToolExecutor"> tests = [] {
    "T5 summary 增强：总工具数 + read/search 计数"_test = [] {
        std::vector<CLFTool> tools;
        CLFTool readTool;
        readTool.m_name = "read_file";
        readTool.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        readTool.m_handler = [](const std::string&) { return "{\"success\":true,\"content\":\"data\"}"; };
        tools.push_back(readTool);

        CLFTool searchTool;
        searchTool.m_name = "search_content";
        searchTool.m_risk = CLF::CLFCore::CLFToolRisk::Read;
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
};

int main() {}
