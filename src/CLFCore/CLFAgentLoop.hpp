// CLFAgentLoop.hpp — Agent 主循环调度器
// 管理工具调用 → API 调用 → 响应处理的完整流程

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "CLFCore/CLFContext.hpp"
#include "CLFCore/CLFProtocolAdapter.hpp"
#include "CLFNetwork/CLFHttpClient.hpp"

namespace CLF::CLFCore {

struct CLFAgentConfig {
    // —— connection（连接认证）——
    std::string m_apiBaseUrl = "https://api.deepseek.com";
    std::string m_apiKey;

    // —— chat_completions（对齐 DeepSeek API 参数）——
    std::string m_modelName    = "deepseek-v4-pro";   // 主模型
    std::string m_subModel     = "deepseek-v4-flash"; // 副模型（轻量任务）
    int         m_maxTokens    = 8192;
    float       m_temperature  = 0.0f;
    float       m_topP         = 1.0f;
    float       m_frequencyPenalty = 0.0f;          // -2.0~2.0，正值降低重复
    float       m_presencePenalty  = 0.0f;          // -2.0~2.0，正值鼓励新话题
    std::string m_responseFormat   = "text";        // "text" | "json_object"
    std::vector<std::string> m_stop;                 // 停止序列（最多16个），空 = 不发送
    bool        m_stream       = false;             // 流式输出（默认关，待实现后改为 true）              // 流式输出（默认开）
    std::string m_thinkingLevel = "max";            // 思考模式等级: off|low|medium|high|max

    // —— agent（Agent 行为参数）——
    int         m_maxContextWindow      = 1048576;  // 1M tokens
    int         m_maxToolCallIterations = 16;
    bool        m_contextCompression    = false;     // 上下文压缩
    int         m_maxResponseDelaySec   = 300;       // 回复最大延迟（秒）
    std::string m_interactionLanguage   = "zh-CN";   // 默认交互语言
};

struct CLFTool {
    std::string m_name;
    std::string m_description;
    std::string m_parametersSchema; // JSON Schema 字符串，描述 function parameters
    std::function<std::string(const std::string&)> m_handler; // 参数为 JSON string
};

class CLFAgentLoop {
public:
    explicit CLFAgentLoop(const CLFAgentConfig& config);

    // 执行一轮对话（含 tool-calling 循环）
    std::string runTurn(const std::string& userInput);

    // 注册工具
    void registerTool(const CLFTool& tool);

    // 清空对话上下文
    void clearContext();

private:
    // 执行工具调用并收集结果
    std::vector<CLFToolResult> executeTools(const std::vector<CLFToolCall>& calls);

    // 注入系统身份提示词（构造时 + /clear 后调用）
    void injectSystemPrompt();

    CLFAgentConfig                    m_config;
    CLFContext                        m_context;
    CLF::CLFNetwork::CLFHttpClient    m_httpClient;
    CLFProtocolAdapter                m_protocolAdapter;
    std::vector<CLFTool>              m_tools;
};

} // namespace CLF::CLFCore
