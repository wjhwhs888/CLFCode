// CLFProtocolAdapter.cpp — 协议适配器实现

#include "CLFCore/CLFProtocolAdapter.hpp"
// CLFTool / CLFAgentConfig / CLFMessage 定义在 CLFTypes.hpp（经 CLFProtocolAdapter.hpp 包含）

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace CLF::CLFCore {

namespace {

// S3-2: usage stream_options 的 provider 判定（按 base_url host 后缀，防误发——
// 不支持的 provider 收到未知 stream_options 可能报错；usage 缺失由 R3"保持 0"
// 预期降级兜底。将来如需其他 provider 可配置显式声明或扩表）
bool isUsageStreamSupported(const std::string& baseUrl) {
    std::string host = baseUrl;
    const auto scheme = host.find("://");
    if (scheme != std::string::npos) host = host.substr(scheme + 3);
    const auto slash = host.find('/');
    if (slash != std::string::npos) host = host.substr(0, slash);
    const auto port = host.rfind(':');
    if (port != std::string::npos) host = host.substr(0, port);
    constexpr const char* kSuffix = "deepseek.com";
    const size_t suffixLen = 12;   // strlen("deepseek.com")
    return host.size() >= suffixLen
        && host.compare(host.size() - suffixLen, suffixLen, kSuffix) == 0;
}

} // anonymous namespace

// ============================================================================
// 公开方法
// ============================================================================

std::string CLFProtocolAdapter::buildChatRequest(
    const std::vector<CLFMessage>& messages,
    const std::vector<CLFTool>&    tools,
    const CLFAgentConfig&          config
) const {
    json body;
    body["model"]       = config.m_modelName;
    body["max_tokens"]  = config.m_maxTokens;
    body["temperature"] = config.m_temperature;
    body["top_p"]       = config.m_topP;
    body["stream"]      = config.m_stream;

    // P2-4: 流式请求显式请求 usage（DeepSeek 默认流式不返回 usage）
    // S3-2: 仅对确认支持的 provider 发送（host 判定），避免多模型切换后误发
    if (config.m_stream && isUsageStreamSupported(config.m_apiBaseUrl)) {
        body["stream_options"]["include_usage"] = true;
    }

    // 可选参数（非默认值时才发送，减少请求体大小）
    if (config.m_frequencyPenalty != 0.0f) {
        body["frequency_penalty"] = config.m_frequencyPenalty;
    }
    if (config.m_presencePenalty != 0.0f) {
        body["presence_penalty"] = config.m_presencePenalty;
    }
    if (config.m_responseFormat != "text") {
        body["response_format"] = config.m_responseFormat;
    }
    if (!config.m_stop.empty()) {
        body["stop"] = config.m_stop;
    }

    // messages 数组
    json msgs = json::array();
    for (const auto& msg : messages) {
        msgs.push_back(serializeMessage(msg));
    }
    body["messages"] = msgs;

    // tools 数组（仅在注册了工具时携带）
    if (!tools.empty()) {
        json toolsArr = json::array();
        for (const auto& tool : tools) {
            toolsArr.push_back(serializeToolDefinition(tool));
        }
        body["tools"]       = toolsArr;
        body["tool_choice"] = "auto";
    }

    // error_handler_t::replace: 非法 UTF-8 字节 → �, 不抛异常
    // 兜底保护: Windows 命令输出(GBK)/二进制文件内容不会导致整个请求崩溃
    return body.dump(-1, ' ', false, json::error_handler_t::replace);
}

CLFAssistantResponse CLFProtocolAdapter::parseAssistantResponse(
    const std::string& responseBody) const {
    CLFAssistantResponse result;

    json respJson;
    try {
        respJson = json::parse(responseBody);
    } catch (const json::parse_error& e) {
        result.m_finishReason = "error";
        result.m_content      = std::string("[Error] JSON parse failed: ") + e.what();
        return result;
    }

    // 安全检查：choices 数组非空
    if (!respJson.contains("choices") || !respJson["choices"].is_array()
        || respJson["choices"].empty()) {
        result.m_finishReason = "error";
        result.m_content      = "[Error] Empty or invalid response choices";
        return result;
    }

    const auto& choice = respJson["choices"][0];

    // 安全检查：message 字段存在
    if (!choice.contains("message") || !choice["message"].is_object()) {
        result.m_finishReason = "error";
        result.m_content      = "[Error] Missing message in response";
        return result;
    }

    const auto& message = choice["message"];

    // 提取文本内容（可能为 null）
    if (message.contains("content") && !message["content"].is_null()) {
        result.m_content = message["content"].get<std::string>();
    }

    // 提取 tool_calls 数组
    if (message.contains("tool_calls") && !message["tool_calls"].is_null()) {
        for (const auto& tc : message["tool_calls"]) {
            CLFToolCall call;
            call.m_id        = tc.value("id", "");
            call.m_name      = tc.value("function", json::object()).value("name", "");
            call.m_arguments = tc.value("function", json::object()).value("arguments", "");
            if (!call.m_id.empty() && !call.m_name.empty()) {
                result.m_toolCalls.push_back(std::move(call));
            }
        }
    }

    // 提取 finish_reason
    if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
        result.m_finishReason = choice["finish_reason"].get<std::string>();
    }

    // P2-4: usage 提取（同步响应默认携带；缺失保持 0——不估猜）
    if (respJson.contains("usage") && respJson["usage"].is_object()) {
        const auto& u = respJson["usage"];
        result.m_usagePrompt     = u.value("prompt_tokens", 0);
        result.m_usageCompletion = u.value("completion_tokens", 0);
        result.m_usageTotal      = u.value("total_tokens", 0);
    }

    return result;
}

bool CLFProtocolAdapter::hasToolCalls(const CLFAssistantResponse& resp) {
    return !resp.m_toolCalls.empty();
}

bool CLFProtocolAdapter::isValidFinish(const CLFAssistantResponse& resp) {
    // stop  = 模型自然结束
    // tool_calls = 模型请求工具调用
    // length = 达到 max_tokens 限制被截断（内容有效但不完整）
    return resp.m_finishReason == "stop"
        || resp.m_finishReason == "tool_calls"
        || resp.m_finishReason == "length";
}

// ============================================================================
// 私有方法
// ============================================================================

json CLFProtocolAdapter::serializeMessage(const CLFMessage& msg) const {
    json m;
    m["role"] = msg.m_role;

    if (msg.m_role == "tool") {
        // tool 角色：tool_call_id + name + content
        m["tool_call_id"] = msg.m_toolCallId;
        if (!msg.m_name.empty()) {
            m["name"] = msg.m_name;
        }
        m["content"] = msg.m_content;
    } else if (msg.m_role == "assistant" && !msg.m_toolCalls.empty()) {
        // assistant 含 tool_calls
        if (msg.m_content.empty()) {
            m["content"] = nullptr;
        } else {
            m["content"] = msg.m_content;
        }
        json tcs = json::array();
        for (const auto& tc : msg.m_toolCalls) {
            json tcJson;
            tcJson["id"]   = tc.m_id;
            tcJson["type"] = "function";
            tcJson["function"]["name"]      = tc.m_name;
            tcJson["function"]["arguments"] = tc.m_arguments;
            tcs.push_back(std::move(tcJson));
        }
        m["tool_calls"] = std::move(tcs);
    } else {
        // system / user / assistant（纯文本）
        m["content"] = msg.m_content;
    }

    return m;
}

json CLFProtocolAdapter::serializeToolDefinition(const CLFTool& tool) const {
    json def;
    def["type"] = "function";

    def["function"]["name"]        = tool.m_name;
    def["function"]["description"] = tool.m_description;

    // 解析 JSON Schema 字符串为 JSON 对象
    if (!tool.m_parametersSchema.empty()) {
        try {
            def["function"]["parameters"] = json::parse(tool.m_parametersSchema);
        } catch (const json::parse_error&) {
            def["function"]["parameters"] = json::object();
        }
    } else {
        def["function"]["parameters"] = json::object();
    }

    return def;
}

} // namespace CLF::CLFCore
