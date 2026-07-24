// CLFAgentLoop.cpp — Agent 主循环实现

#include "CLFCore/CLFAgentLoop.hpp"


#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace CLF::CLFCore {

CLFAgentLoop::CLFAgentLoop(const CLFAgentConfig& config)
    : m_config(config)
    , m_context(config.m_maxTokens)
    , m_httpClient(config.m_apiBaseUrl, config.m_apiKey) {
}

std::string CLFAgentLoop::runTurn(const std::string& userInput) {
    // 将用户输入加入上下文
    m_context.addMessage("user", userInput);

    // 构建请求
    std::string body = buildRequestBody(m_context.getMessages(), false);

    // 发送 API 请求
    CLF::CLFNetwork::CLFHttpResponse response = m_httpClient.postJson("/v1/chat/completions", body);

    if (!response.m_error.empty()) {
        return std::string("[Error] ") + response.m_error;
    }

    // 解析响应
    json respJson = json::parse(response.m_body);
    std::string content = respJson["choices"][0]["message"]["content"];

    // 加入上下文
    m_context.addMessage("assistant", content);

    return content;
}

void CLFAgentLoop::registerTool(const CLFTool& tool) {
    m_tools.push_back(tool);
}

void CLFAgentLoop::clearContext() {
    m_context.clear();
}

std::string CLFAgentLoop::buildRequestBody(const std::vector<CLFMessage>& messages, bool stream) const {
    json body;
    body["model"]       = m_config.m_modelName;
    body["max_tokens"]  = m_config.m_maxTokens;
    body["temperature"] = m_config.m_temperature;
    body["stream"]      = stream;

    json msgs = json::array();
    for (const auto& msg : messages) {
        json m;
        m["role"]    = msg.m_role;
        m["content"] = msg.m_content;
        msgs.push_back(m);
    }
    body["messages"] = msgs;

    return body.dump();
}

std::vector<CLFToolCall> CLFAgentLoop::parseToolCalls(const std::string& responseBody) const {
    // TODO: 解析 JSON 中的 tool_calls 字段
    return {};
}

std::vector<CLFToolResult> CLFAgentLoop::executeTools(const std::vector<CLFToolCall>& calls) {
    // TODO: 匹配工具名、调用 handler、收集结果
    return {};
}

} // namespace CLF::CLFCore
