// qa_CLFAgentLoop.cpp — Agent 主循环集成测试（L2，Mock HTTP）
// 覆盖：tool-calling 循环、安全策略阻断、流式累积

#include <boost/ut.hpp>
#include <deque>

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFNetwork/CLFHttpClient.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFAgentLoop;
using CLF::CLFCore::CLFAgentConfig;
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
        for (const auto& line : lines) {
            onLine(line);
        }
        return {200, "", ""};
    }

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
};

// Boost.UT：测试在静态初始化时注册，cfg 析构时自动运行并输出报告
int main() {
    return 0;
}
