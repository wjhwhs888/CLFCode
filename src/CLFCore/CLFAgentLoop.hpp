// CLFAgentLoop.hpp — Agent 主循环调度器
// 管理工具调用 → API 调用 → 响应处理的完整流程

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CLFCore/CLFContext.hpp"
#include "CLFCore/CLFProtocolAdapter.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFNetwork/CLFHttpClient.hpp"

namespace CLF::CLFCore {

struct CLFAgentConfig {
    // —— connection（连接认证）——
    std::string m_apiBaseUrl = "https://api.deepseek.com";
    std::string m_apiKey;

    // —— chat_completions（对齐 DeepSeek API 参数）——
    std::string m_modelName    = "deepseek-v4-flash";  // 主模型（V4 Flash 正式版）
    std::string m_subModel     = "deepseek-v4-pro";   // 副模型（Pro 正式版未出，暂作备选）
    int         m_maxTokens    = 8192;
    float       m_temperature  = 0.0f;
    float       m_topP         = 1.0f;
    float       m_frequencyPenalty = 0.0f;          // -2.0~2.0，正值降低重复
    float       m_presencePenalty  = 0.0f;          // -2.0~2.0，正值鼓励新话题
    std::string m_responseFormat   = "text";        // "text" | "json_object"
    std::vector<std::string> m_stop;                 // 停止序列（最多16个），空 = 不发送
    bool        m_stream       = false;             // 流式输出（配置驱动）
    std::string m_thinkingLevel = "max";            // 思考模式等级: off|low|medium|high|max

    // —— agent（Agent 行为参数）——
    int         m_maxContextWindow      = 1048576;  // 1M tokens
    int         m_maxToolCallIterations = 16;
    bool        m_contextCompression    = false;     // 上下文压缩
    int         m_maxResponseDelaySec   = 300;       // 回复最大延迟（秒）
    std::string m_interactionLanguage   = "zh-CN";   // 默认交互语言
    std::string m_securityMode          = "edit";    // auto|analyze|edit|manual

    // —— logging（日志配置）——
    std::string m_logLevel   = "info";              // debug|info|warn|error
    std::string m_logFile    = "clf_agent.log";     // 日志文件路径（相对项目根）
    bool        m_logConsole = false;               // 同时输出到控制台
};

struct CLFTool {
    std::string m_name;
    std::string m_description;
    std::string m_parametersSchema; // JSON Schema 字符串，描述 function parameters
    CLFToolRisk m_risk = CLFToolRisk::Read; // 工具风险等级（安全策略用）
    std::function<std::string(const std::string&)> m_handler; // 参数为 JSON string
};

class CLFAgentLoop {
public:
    // httpClient 可注入 Mock（测试用）；nullptr 时创建真实实例
    explicit CLFAgentLoop(const CLFAgentConfig& config,
                          std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> httpClient = nullptr);

    // 执行一轮对话（含 tool-calling 循环）
    std::string runTurn(const std::string& userInput);

    // 注册工具
    void registerTool(const CLFTool& tool);

    // 清空对话上下文
    void clearContext();

    // 向当前会话注入知识库内容（系统消息级别）
    void injectSkillToContext(const std::string& skillName, const std::string& content);

    // 安全模式切换/查询
    void setSecurityMode(CLFSecurityMode mode);
    CLFSecurityMode getSecurityMode() const;
    const char* getSecurityModeName() const;

    // 设置高风险工具确认回调（main.cpp 注入，返回 true 表示用户允许）
    // 回调参数为提示文本（含工具名和参数）
    void setConfirmCallback(std::function<bool(const std::string&)> callback);

    // —— 会话持久化 ——

    // 保存当前会话到文件（incomplete=true 存为未完成状态）
    // 返回文件路径，失败返回空串
    std::string saveSession(const std::string& dirPath, bool incomplete) const;

    // 从会话文件恢复（跳过 system 消息，身份重新注入）
    bool restoreSession(const std::string& filePath);

    // —— 查询 ——

    // 获取当前配置（/config 命令用）
    const CLFAgentConfig& getConfig() const { return m_config; }

    // 已注入上下文的 skill 名称列表（/skill 状态显示用）
    std::vector<std::string> getLoadedSkills() const;

private:
    // 执行工具调用并收集结果
    std::vector<CLFToolResult> executeTools(const std::vector<CLFToolCall>& calls);

    // 注入系统身份提示词（构造时 + /clear 后调用）
    void injectSystemPrompt();

    CLFAgentConfig                    m_config;
    CLFContext                        m_context;
    std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> m_httpClient;
    CLFProtocolAdapter                m_protocolAdapter;
    CLFSecurityPolicy                 m_securityPolicy;
    std::function<bool(const std::string&)> m_confirmCallback;
    std::vector<CLFTool>              m_tools;
    std::vector<std::string>          m_loadedSkills;
};

} // namespace CLF::CLFCore
