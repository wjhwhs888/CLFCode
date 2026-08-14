// qa_CLFAgentLoop.cpp — Agent 主循环集成测试（L2，Mock HTTP）
// 覆盖：tool-calling 循环、安全策略阻断、流式累积

#include <boost/ut.hpp>
#include <chrono>
#include <deque>
#include <filesystem>
#include <thread>

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFNetwork/CLFHttpClient.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFAgentLoop;
using CLF::CLFCore::CLFAgentConfig;
using CLF::CLFCore::CLFMessage;
using CLF::CLFCore::CLFSessionManager;
using CLF::CLFCore::CLFTool;
using CLF::CLFCore::CLFSecurityMode;
using CLF::CLFNetwork::ICLFHttpClient;
using CLF::CLFNetwork::CLFHttpResponse;

namespace {

// Mock HTTP 客户端：按序消费预设响应
class MockHttpClient : public ICLFHttpClient {
public:
    void setTimeout(int) override {}
    void abort() override {}

    // 预设同步响应（顺序消费）
    void pushResponse(const std::string& body, const std::string& error = "") {
        m_syncResponses.push_back({200, body, error});
    }

    // 预设流式响应（SSE 行序列，含 data: 前缀）
    void pushStream(const std::vector<std::string>& sseLines) {
        m_streamResponses.push_back(sseLines);
    }

    int syncCallCount() const { return m_syncCalls; }
    int streamCallCount() const { return m_streamCalls; }

    CLFHttpResponse postJson(const std::string&, const std::string&) override {
        ++m_syncCalls;
        expect(!m_syncResponses.empty()); // 预设不足即失败
        auto resp = m_syncResponses.front();
        m_syncResponses.pop_front();
        return resp;
    }

    CLFHttpResponse postJsonStream(
        const std::string&, const std::string&,
        std::function<void(const std::string&)> onLine) override {
        ++m_streamCalls;
        expect(!m_streamResponses.empty());
        auto lines = m_streamResponses.front();
        m_streamResponses.pop_front();
        int idx = 0;
        for (const auto& line : lines) {
            onLine(line);
            // T6a: 喂到 hookAfterLine 行后触发注入的中断回调
            if (idx == hookAfterLine && onLineHook) onLineHook();
            ++idx;
        }
        return {200, "", ""};
    }

    // T6a 中断注入钩子
    int hookAfterLine = -1;
    std::function<void()> onLineHook;

private:
    std::deque<CLFHttpResponse> m_syncResponses;
    std::deque<std::vector<std::string>> m_streamResponses;
    int m_syncCalls = 0;
    int m_streamCalls = 0;
};

// 构造带 Mock 的 Agent + 注册 echo 工具
std::unique_ptr<CLFAgentLoop> makeAgent(std::shared_ptr<MockHttpClient> mock, const std::string& mode = "edit") {
    CLFAgentConfig config;
    config.m_apiKey    = "test-key";
    config.m_modelName = "test-model";
    config.m_stream    = false;
    config.m_securityMode = mode;
    config.m_maxToolCallIterations = 8;

    auto agent = std::make_unique<CLFAgentLoop>(config, mock);

    CLFTool echoTool;
    echoTool.m_name = "echo";
    echoTool.m_description = "echo test";
    echoTool.m_parametersSchema = R"({"type":"object","properties":{"msg":{"type":"string"}},"required":["msg"]})";
    echoTool.m_risk = CLF::CLFCore::CLFToolRisk::Read;
    echoTool.m_handler = [](const std::string& args) { return "Echo:" + args; };
    agent->registerTool(echoTool);

    return agent;
}

// T6: 记录输出调用的 Mock（中断消息计数 + StatusKind 序列）
class MockOutput : public CLF::CLFTypes::ICLFOutput {
public:
    std::vector<std::string> contents;
    std::vector<ICLFOutput::StatusKind> kinds;

    void emitContent(const std::string& t) override { contents.push_back(t); }
    void emitRaw(const std::string&) override {}
    void emitStyledLine(const std::string&, LineStyle) override {}
    void setStatus(const std::string&, int, int) override {}
    void setStatusTextOnly(const std::string&) override {}
    bool confirm(const std::string&) override { return false; }
    void onInterrupt(std::function<void()> cb) override { m_cb = std::move(cb); }
    void showProgress(const std::vector<std::string>&) override {}
    void finishProgress(const std::string&) override {}
    void emitError(const std::string&) override {}
    void appendThinking(const std::string&) override {}
    void clearThinking() override {}
    void setStatusKind(StatusKind k) override { kinds.push_back(k); }
    void showFoldedBlock(const std::string& summary,
                         const std::vector<std::string>& lines) override {
        foldedSummary = summary;
        foldedLines = lines;
    }
    std::string foldedSummary;
    std::vector<std::string> foldedLines;

    void fireInterrupt() { if (m_cb) m_cb(); }
    int interruptEmissions() const {
        int n = 0;
        for (const auto& c : contents)
            if (c.find("已中断") != std::string::npos) ++n;
        return n;
    }

private:
    std::function<void()> m_cb;
};

} // anonymous namespace

const boost::ut::suite<"CLFAgentLoop"> tests = [] {
    "tool-calling 循环：先 tool_calls 后 stop，两轮完成"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        mock->pushResponse(R"({
            "choices": [{
                "message": {
                    "role": "assistant",
                    "content": "让我查一下",
                    "tool_calls": [{
                        "id": "call_1",
                        "type": "function",
                        "function": {"name": "echo", "arguments": "{\"msg\":\"hi\"}"}
                    }]
                },
                "finish_reason": "tool_calls"
            }]
        })");
        mock->pushResponse(R"({
            "choices": [{
                "message": {"role": "assistant", "content": "查询完成：Echo:{\"msg\":\"hi\"}"},
                "finish_reason": "stop"
            }]
        })");

        auto agent = makeAgent(mock);
        std::string result = agent->runTurn("echo hi");
        expect(mock->syncCallCount() == 2);
        expect(result.find("查询完成") != std::string::npos);
    };

    "Analyze 模式：命令类工具被阻断（不执行 handler）"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        // 只有一轮响应：模型请求写工具 → 被阻断 → 结果回传 → 模型回复
        mock->pushResponse(R"({
            "choices": [{
                "message": {
                    "role": "assistant",
                    "content": null,
                    "tool_calls": [{
                        "id": "call_2",
                        "type": "function",
                        "function": {"name": "write_file", "arguments": "{\"path\":\"a.txt\"}"}
                    }]
                },
                "finish_reason": "tool_calls"
            }]
        })");
        mock->pushResponse(R"({
            "choices": [{
                "message": {"role": "assistant", "content": "工具被安全策略阻断"},
                "finish_reason": "stop"
            }]
        })");

        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_securityMode = "analyze";
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);

        // 注册写工具（handler 不应被调用）
        bool handlerCalled = false;
        CLFTool writeTool;
        writeTool.m_name = "write_file";
        writeTool.m_risk = CLF::CLFCore::CLFToolRisk::Write;
        writeTool.m_handler = [&](const std::string&) { handlerCalled = true; return "wrote"; };
        agent->registerTool(writeTool);

        std::string result = agent->runTurn("写文件");
        expect(!handlerCalled); // 阻断，handler 未执行
        expect(result.find("安全策略") != std::string::npos ||
               result.find("Blocked") != std::string::npos);
    };

    "流式路径：SSE 行累积出完整内容 + tool_calls"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = true;
        config.m_maxToolCallIterations = 4;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);

        CLFTool timeTool;
        timeTool.m_name = "get_time";
        timeTool.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        timeTool.m_handler = [](const std::string&) { return "12:00"; };
        agent->registerTool(timeTool);

        // 流式：文本 → tool_calls delta → finish_reason → [DONE]
        mock->pushStream({
            "data: {\"choices\":[{\"delta\":{\"content\":\"时间\"}}]}",
            "data: {\"choices\":[{\"delta\":{\"content\":\"查询中\"}}]}",
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_s\",\"function\":{\"name\":\"get_time\",\"arguments\":\"\"}}]}}]}",
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"{}\"}}]}}]}",
            "data: {\"choices\":[{\"finish_reason\":\"tool_calls\"}]}",
            "data: [DONE]"
        });
        // 第二轮：最终回答
        mock->pushStream({
            "data: {\"choices\":[{\"delta\":{\"content\":\"现在是12:00\"}}]}",
            "data: {\"choices\":[{\"finish_reason\":\"stop\"}]}",
            "data: [DONE]"
        });

        std::string result = agent->runTurn("几点了");
        expect(mock->streamCallCount() == 2);
        expect(result.empty()); // 流式返回空串（已实时输出）
    };

    // ========== T6: emitInterrupted 三时点中断（F17 裁决：不加 once 语义，测试兜底） ==========

    "T6a 中断于流式中：恰好一条中断消息 + Warn"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = true;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);
        MockOutput out;
        agent->setOutput(&out);

        mock->hookAfterLine = 1;  // 第 2 行之后触发中断
        mock->onLineHook = [&] { out.fireInterrupt(); };
        mock->pushStream({
            "data: {\"choices\":[{\"delta\":{\"content\":\"a\"}}]}",
            "data: {\"choices\":[{\"delta\":{\"content\":\"b\"}}]}",
            "data: [DONE]"
        });

        std::string result = agent->runTurn("hi");
        expect(result == "[Interrupted]");
        expect(out.interruptEmissions() == 1);
        expect(out.kinds.back() == CLF::CLFTypes::ICLFOutput::StatusKind::Warn);
    };

    "T6b 中断于重试等待中：恰好一条中断消息 + Warn"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = false;
        config.m_maxToolCallIterations = 4;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);
        MockOutput out;
        agent->setOutput(&out);

        mock->pushResponse("", "timeout");  // 非致命 → 进入可中断重试等待

        std::string result;
        std::thread runner([&] { result = agent->runTurn("hi"); });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));  // 已进入等待循环
        out.fireInterrupt();
        runner.join();

        expect(result == "[Interrupted]");
        expect(out.interruptEmissions() == 1);
        expect(out.kinds.back() == CLF::CLFTypes::ICLFOutput::StatusKind::Warn);
    };

    // ========== T4: 状态点状态机（P1-1 接线全表） ==========

    "T4a 正常完成：Running → Done 序列，Done 常亮不自动清"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = false;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);
        MockOutput out;
        agent->setOutput(&out);

        mock->pushResponse(R"({
            "choices": [{
                "message": {"role": "assistant", "content": "hi"},
                "finish_reason": "stop"
            }]
        })");

        std::string result = agent->runTurn("hello");
        expect(!result.empty());
        expect(out.kinds.size() >= 2);
        expect(out.kinds.front() == CLF::CLFTypes::ICLFOutput::StatusKind::Running);
        expect(out.kinds.back() == CLF::CLFTypes::ICLFOutput::StatusKind::Done);
        // F14: turn 结束后 Done 常亮（无自动清除）
    };

    "T4b 致命错误 return：Error 不被 TurnGuard 覆盖（F20）"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = false;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);
        MockOutput out;
        agent->setOutput(&out);

        mock->pushResponse("", "HTTP 401 unauthorized");

        std::string result = agent->runTurn("hi");
        expect(result.find("[Error]") != std::string::npos);
        expect(out.kinds.back() == CLF::CLFTypes::ICLFOutput::StatusKind::Error);
    };

    "T7 /resume 回显走折叠块：历史不进滚动区（P2-1）"_test = [] {
        auto dir = std::filesystem::temp_directory_path()
                 / ("clf_restore_test_" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);

        std::vector<CLFMessage> messages;
        messages.push_back({"user", "hello"});
        messages.push_back({"assistant", "world"});
        std::string path = CLFSessionManager::save(messages, dir.string(), false);

        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);
        MockOutput out;
        agent->setOutput(&out);

        bool ok = agent->restoreSession(path);
        expect(ok);

        // 历史内容不进滚动区（emitContent 直灌路径已废除）
        bool leaked = false;
        for (const auto& c : out.contents)
            if (c.find("hello") != std::string::npos
                || c.find("world") != std::string::npos)
                leaked = true;
        expect(!leaked);

        // 折叠块：摘要含消息计数、展开行含内容
        expect(out.foldedSummary.find("2 条消息") != std::string::npos);
        bool hasUser = false, hasAssistant = false;
        for (const auto& l : out.foldedLines) {
            if (l.find("> hello") != std::string::npos) hasUser = true;
            if (l.find("world") != std::string::npos) hasAssistant = true;
        }
        expect(hasUser);
        expect(hasAssistant);

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    };

    "T6c 中断于工具执行中：恰好一条中断消息 + Warn"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = false;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);
        MockOutput out;
        agent->setOutput(&out);

        CLFTool slowTool;
        slowTool.m_name = "slow_read";
        slowTool.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        slowTool.m_handler = [](const std::string&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            return "done";
        };
        agent->registerTool(slowTool);

        mock->pushResponse(R"({
            "choices": [{
                "message": {
                    "role": "assistant",
                    "content": null,
                    "tool_calls": [{
                        "id": "c1",
                        "type": "function",
                        "function": {"name": "slow_read", "arguments": "{}"}
                    }]
                },
                "finish_reason": "tool_calls"
            }]
        })");

        std::string result;
        std::thread runner([&] { result = agent->runTurn("go"); });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));  // handler 执行中
        out.fireInterrupt();
        runner.join();

        expect(result == "[Interrupted]");
        expect(out.interruptEmissions() == 1);
        expect(out.kinds.back() == CLF::CLFTypes::ICLFOutput::StatusKind::Warn);
    };
};

// Boost.UT：测试在静态初始化时注册，cfg 析构时自动运行并输出报告
int main() {
    return 0;
}
