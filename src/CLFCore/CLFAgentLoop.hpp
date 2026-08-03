// CLFAgentLoop.hpp — Agent 主循环调度器
// 管理工具调用 → API 调用 → 响应处理的完整流程

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CLFCore/CLFTypes.hpp"
#include "CLFCore/CLFContext.hpp"
#include "CLFCore/CLFProtocolAdapter.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"

namespace CLF::CLFNetwork { class ICLFHttpClient; }
namespace CLF::CLFCore { class CLFEventQueue; }
namespace CLF::CLFCore {

class CLFAgentLoop {
public:
    explicit CLFAgentLoop(const CLFAgentConfig& config,
                          std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> httpClient = nullptr);

    // 设置事件队列 (CLFRepl 注入, 用于 scrollPrint 等渲染事件)
    void setEventQueue(CLFEventQueue* q) { m_eventQueue = q; }

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

    // 设置工作状态回调（main.cpp 注入，更新状态区显示）
    // 参数：标题（时间+任务名）、内容（正在执行的操作）
    void setStatusCallback(std::function<void(const std::string&, const std::string&)> callback);

    // —— 会话持久化 ——

    // 保存当前会话到文件（incomplete=true 存为未完成状态）
    // 返回文件路径，失败返回空串
    std::string saveSession(const std::string& dirPath, bool incomplete) const;

    // 从会话文件恢复（跳过 system 消息，身份重新注入）
    bool restoreSession(const std::string& filePath);

    // —— 查询 ——

    // 获取当前配置（/config 命令用）
    const CLFAgentConfig& getConfig() const { return m_config; }

    // 获取上下文（/context 命令用）
    const CLFContext& getContext() const { return m_context; }

    ToolStats getLastToolStats() const { return m_lastToolStats; }

    // 已注入上下文的 skill 名称列表（/skill 状态显示用）
    std::vector<std::string> getLoadedSkills() const;

private:
    // 注入系统身份提示词（构造时 + /clear 后调用）
    void injectSystemPrompt();

    CLFAgentConfig                    m_config;
    CLFContext                        m_context;
    std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> m_httpClient;
    CLFProtocolAdapter                m_protocolAdapter;
    CLFSecurityPolicy                 m_securityPolicy;
    std::function<bool(const std::string&)> m_confirmCallback;
    std::function<void(const std::string&, const std::string&)> m_statusCallback;
    std::vector<CLFTool>              m_tools;
    std::vector<std::string>          m_loadedSkills;
    ToolStats                         m_lastToolStats;
    CLFEventQueue*                    m_eventQueue = nullptr;
};

} // namespace CLF::CLFCore
