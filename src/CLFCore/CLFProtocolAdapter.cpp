// CLFProtocolAdapter.cpp — 协议适配器实现

#include "CLFCore/CLFProtocolAdapter.hpp"
#include "CLFCore/CLFAgentLoop.hpp" // CLFTool 定义

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace CLF::CLFCore {

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

    return body.dump();
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

    return result;
}

bool CLFProtocolAdapter::hasToolCalls(const CLFAssistantResponse& resp) {
    return !resp.m_toolCalls.empty();
}

bool CLFProtocolAdapter::isValidFinish(const CLFAssistantResponse& resp) {
    return resp.m_finishReason == "stop" || resp.m_finishReason == "tool_calls";
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
