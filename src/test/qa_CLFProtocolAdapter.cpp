// qa_CLFProtocolAdapter.cpp — 协议适配器单元测试
// 覆盖：4 种角色消息序列化、tools 构建、响应解析、畸形 JSON 防御

#include <boost/ut.hpp>
#include "CLFCore/CLFProtocolAdapter.hpp"
#include "CLFTypes/CLFTypes.hpp" // CLFTool / CLFAgentConfig

#include <nlohmann/json.hpp>

using namespace boost::ut;
using json = nlohmann::json;
using CLF::CLFCore::CLFProtocolAdapter;
using CLF::CLFCore::CLFMessage;
using CLF::CLFCore::CLFTool;
using CLF::CLFCore::CLFToolCall;
using CLF::CLFCore::CLFAgentConfig;

const boost::ut::suite<"CLFProtocolAdapter"> tests = [] {
    "T10 同步响应 usage 解析"_test = [] {
        CLFProtocolAdapter adapter;
        auto parsed = adapter.parseAssistantResponse(R"({
            "choices": [{
                "message": {"role": "assistant", "content": "hi"},
                "finish_reason": "stop"
            }],
            "usage": {"prompt_tokens": 100, "completion_tokens": 50, "total_tokens": 150}
        })");
        expect(parsed.m_usagePrompt == 100);
        expect(parsed.m_usageCompletion == 50);
        expect(parsed.m_usageTotal == 150);
    };

    "T10 usage 缺失：保持 0（不估猜）"_test = [] {
        CLFProtocolAdapter adapter;
        auto parsed = adapter.parseAssistantResponse(R"({
            "choices": [{
                "message": {"role": "assistant", "content": "hi"},
                "finish_reason": "stop"
            }]
        })");
        expect(parsed.m_usageTotal == 0);
    };

    "T10 流式请求携带 stream_options.include_usage"_test = [] {
        CLFProtocolAdapter adapter;
        CLFAgentConfig config;
        config.m_stream = true;
        std::vector<CLFMessage> msgs;
        msgs.push_back({"user", "hi"});
        auto body = json::parse(adapter.buildChatRequest(msgs, {}, config));
        expect(body["stream_options"]["include_usage"] == true);
    };

    "请求体包含 model/messages/stream 核心字段"_test = [] {
        CLFProtocolAdapter adapter;
        CLFAgentConfig config;
        std::vector<CLFMessage> msgs;
        msgs.push_back({"user", "hello"});

        auto body = json::parse(adapter.buildChatRequest(msgs, {}, config));
        expect(body["model"] == config.m_modelName);
        expect(body["stream"] == config.m_stream);
        expect(body["messages"].size() == 1);
        expect(body["messages"][0]["role"] == "user");
        expect(body["messages"][0]["content"] == "hello");
    };

    "注册工具时请求体带 tools 数组 + tool_choice=auto"_test = [] {
        CLFProtocolAdapter adapter;
        CLFAgentConfig config;
        std::vector<CLFMessage> msgs;
        msgs.push_back({"user", "hi"});

        CLFTool tool;
        tool.m_name        = "get_time";
        tool.m_description = "get current time";
        tool.m_parametersSchema = R"({"type":"object","properties":{},"required":[]})";
        tool.m_handler = [](const std::string&) { return std::string(); };

        auto body = json::parse(adapter.buildChatRequest(msgs, {tool}, config));
        expect(body.contains("tools"));
        expect(body["tools"][0]["type"] == "function");
        expect(body["tools"][0]["function"]["name"] == "get_time");
        expect(body["tool_choice"] == "auto");
    };

    "assistant 含 tool_calls 消息序列化（content=null）"_test = [] {
        CLFProtocolAdapter adapter;
        CLFAgentConfig config;
        std::vector<CLFMessage> msgs;
        CLFMessage assistant;
        assistant.m_role = "assistant";
        CLFToolCall tc;
        tc.m_id        = "call_1";
        tc.m_name      = "get_time";
        tc.m_arguments = R"({})";
        assistant.m_toolCalls.push_back(tc);
        msgs.push_back(assistant);

        auto body = json::parse(adapter.buildChatRequest(msgs, {}, config));
        auto& m = body["messages"][0];
        expect(m["role"] == "assistant");
        expect(m["content"].is_null());
        expect(m["tool_calls"][0]["id"] == "call_1");
        expect(m["tool_calls"][0]["function"]["name"] == "get_time");
        expect(m["tool_calls"][0]["function"]["arguments"] == "{}");
    };

    "tool 角色消息序列化（tool_call_id + name + content）"_test = [] {
        CLFProtocolAdapter adapter;
        CLFAgentConfig config;
        std::vector<CLFMessage> msgs;
        CLFMessage tool;
        tool.m_role       = "tool";
        tool.m_toolCallId = "call_1";
        tool.m_name       = "get_time";
        tool.m_content    = "12:00";
        msgs.push_back(tool);

        auto body = json::parse(adapter.buildChatRequest(msgs, {}, config));
        auto& m = body["messages"][0];
        expect(m["role"] == "tool");
        expect(m["tool_call_id"] == "call_1");
        expect(m["name"] == "get_time");
        expect(m["content"] == "12:00");
    };

    "响应解析：纯文本 + finish_reason=stop"_test = [] {
        CLFProtocolAdapter adapter;
        std::string response = R"({
            "choices": [{
                "message": {"role": "assistant", "content": "你好"},
                "finish_reason": "stop"
            }]
        })";
        auto parsed = adapter.parseAssistantResponse(response);
        expect(parsed.m_content == "你好");
        expect(parsed.m_finishReason == "stop");
        expect(!CLFProtocolAdapter::hasToolCalls(parsed));
        expect(CLFProtocolAdapter::isValidFinish(parsed));
    };

    "响应解析：tool_calls 数组提取"_test = [] {
        CLFProtocolAdapter adapter;
        std::string response = R"({
            "choices": [{
                "message": {
                    "role": "assistant",
                    "content": null,
                    "tool_calls": [{
                        "id": "call_x",
                        "type": "function",
                        "function": {"name": "read_file", "arguments": "{\"path\":\"a.txt\"}"}
                    }]
                },
                "finish_reason": "tool_calls"
            }]
        })";
        auto parsed = adapter.parseAssistantResponse(response);
        expect(CLFProtocolAdapter::hasToolCalls(parsed));
        expect(parsed.m_toolCalls.size() == 1);
        expect(parsed.m_toolCalls[0].m_id == "call_x");
        expect(parsed.m_toolCalls[0].m_name == "read_file");
        expect(parsed.m_toolCalls[0].m_arguments == "{\"path\":\"a.txt\"}");
        expect(CLFProtocolAdapter::isValidFinish(parsed)); // tool_calls 也是有效结束
    };

    "响应解析：畸形 JSON 不崩溃，返回 error 标记"_test = [] {
        CLFProtocolAdapter adapter;
        auto parsed = adapter.parseAssistantResponse("<<<not json>>>");
        expect(parsed.m_finishReason == "error");
        expect(parsed.m_content.find("[Error]") != std::string::npos);
    };

    "响应解析：空 choices 数组防御"_test = [] {
        CLFProtocolAdapter adapter;
        auto parsed = adapter.parseAssistantResponse(R"({"choices": []})");
        expect(parsed.m_finishReason == "error");
    };

    "非法 Schema 不崩溃（降级为空对象）"_test = [] {
        CLFProtocolAdapter adapter;
        CLFAgentConfig config;
        std::vector<CLFMessage> msgs;
        msgs.push_back({"user", "hi"});

        CLFTool tool;
        tool.m_name = "bad";
        tool.m_parametersSchema = "not-a-json{{{";
        tool.m_handler = [](const std::string&) { return std::string(); };

        auto body = json::parse(adapter.buildChatRequest(msgs, {tool}, config));
        expect(body["tools"][0]["function"]["parameters"].is_object());
    };

    "S3-2 include_usage 仅 deepseek provider 发送（2026-09-02）"_test = [] {
        CLFProtocolAdapter adapter;

        // deepseek host（含子域与端口）→ 发送 include_usage
        CLFAgentConfig deepseekConfig;
        deepseekConfig.m_apiBaseUrl = "https://api.deepseek.com/v1";
        deepseekConfig.m_stream     = true;
        auto body1 = json::parse(adapter.buildChatRequest({}, {}, deepseekConfig));
        expect(body1.contains("stream_options"));
        expect(body1["stream_options"]["include_usage"] == true);

        // 其他 provider → 不发送（防误发；usage 缺失由 R3 保持 0 预期降级）
        CLFAgentConfig otherConfig;
        otherConfig.m_apiBaseUrl = "https://api.example.com/v1";
        otherConfig.m_stream     = true;
        auto body2 = json::parse(adapter.buildChatRequest({}, {}, otherConfig));
        expect(!body2.contains("stream_options"));

        // 非流式 → 从不发送
        CLFAgentConfig nonStreamConfig;
        nonStreamConfig.m_apiBaseUrl = "https://api.deepseek.com";
        nonStreamConfig.m_stream     = false;
        auto body3 = json::parse(adapter.buildChatRequest({}, {}, nonStreamConfig));
        expect(!body3.contains("stream_options"));
    };
};

// Boost.UT：测试在静态初始化时注册，cfg 析构时自动运行并输出报告
int main() {
    return 0;
}
