// CLFAgentLoop.hpp — Agent 主循环调度器
// 管理工具调用 → API 调用 → 响应处理的完整流程

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "CLFCore/CLFContext.hpp"
#include "CLFNetwork/CLFHttpClient.hpp"

namespace CLF::CLFCore {

struct CLFAgentConfig {
    std::string m_apiBaseUrl;
    std::string m_apiKey;
    std::string m_modelName   = "deepseek-chat";
    int         m_maxTokens   = 8192;
    float       m_temperature = 0.0f;
    bool        m_enableThinking = true;
};

struct CLFTool {
    std::string m_name;
    std::string m_description;
    std::function<std::string(const std::string&)> m_handler;
};

class CLFAgentLoop {
public:
    explicit CLFAgentLoop(const CLFAgentConfig& config);

    // 执行一轮对话
    std::string runTurn(const std::string& userInput);

    // 注册工具
    void registerTool(const CLFTool& tool);

    // 清空对话上下文
    void clearContext();

private:
    // 构建 LLM API 请求体
    std::string buildRequestBody(const std::vector<CLFMessage>& messages, bool stream) const;

    // 解析 LLM 响应中的 tool_calls
    std::vector<CLFToolCall> parseToolCalls(const std::string& responseBody) const;

    // 执行工具调用并收集结果
    std::vector<CLFToolResult> executeTools(const std::vector<CLFToolCall>& calls);

    CLFAgentConfig               m_config;
    CLFContext                   m_context;
    CLF::CLFNetwork::CLFHttpClient  m_httpClient;
    std::vector<CLFTool>         m_tools;
};

} // namespace CLF::CLFCore
