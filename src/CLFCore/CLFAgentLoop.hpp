// CLFAgentLoop.hpp — Agent 主循环调度器
// 管理工具调用 → API 调用 → 响应处理的完整流程

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CLFTypes/CLFTypes.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFCore/CLFContext.hpp"
#include "CLFCore/CLFProtocolAdapter.hpp"
#include "CLFCore/CLFSystemPromptBuilder.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFCore/CLFSessionSummarizer.hpp"

namespace CLF::CLFNetwork { class ICLFHttpClient; }
namespace CLF::CLFCore {

class CLFAgentLoop {
public:
    explicit CLFAgentLoop(const CLFAgentConfig& config,
                          std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> httpClient = nullptr,
                          const CLFTimerLabels& labels = {});
    ~CLFAgentLoop();

    // 注册工具后，m_tools 中的 handler 可能捕获 *this 的引用
    // （如 todo_write 需读写 m_todos），构成自引用。
    // 拷贝/移动会让已注册 handler 指向旧对象 → 悬垂，故显式禁用。
    CLFAgentLoop(const CLFAgentLoop&)            = delete;
    CLFAgentLoop& operator=(const CLFAgentLoop&) = delete;
    CLFAgentLoop(CLFAgentLoop&&)                 = delete;
    CLFAgentLoop& operator=(CLFAgentLoop&&)      = delete;

    // 注入输出通道 + 注册中断回调.
    // 析构时自动清空回调.
    void setOutput(CLF::CLFTypes::ICLFOutput* output);

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
    const char* getSecurityModeName() const;

    // 设置高风险工具确认回调（main.cpp 注入，返回 true 表示用户允许）
    // 回调参数为提示文本（含工具名和参数）
    void setConfirmCallback(std::function<bool(const std::string&)> callback);

    // —— 会话持久化 ——

    // 保存当前会话
    // finalize=false: 存为 latest.json（每轮回合后调用，原子写入）
    // finalize=true:  归档 latest.json → 时间戳.json（/exit 和 /clear 调用）
    // 返回文件路径，失败返回空串
    std::string saveSession(const std::string& dirPath, bool finalize) const;

    // 从会话文件恢复（跳过 system 消息，回显历史，重新注入 skills）
    bool restoreSession(const std::string& filePath);

    //待办清单读写（S2-6：随会话持久化，不独立落盘）
    // 由 todo_write 工具 handler 通过捕获的 agent 引用调用
    // example:
    //   agent.setTodos(parsedTodos);
    //   for (const auto& t : agent.getTodos()) show(t);
    const std::vector<CLFTodoItem>& getTodos() const { return m_todos; }
    void setTodos(std::vector<CLFTodoItem> todos) { m_todos = std::move(todos); }

    // —— 查询 ——

    // 获取当前配置（/config 命令用）
    const CLFAgentConfig& getConfig() const { return m_config; }

    // 获取上下文（/context 命令用）
    const CLFContext& getContext() const { return m_context; }

    ToolStats getLastToolStats() const { return m_lastToolStats; }

    // P2-4: 本次会话累计 token（仅统计已落定的 usage）
    long long getTotalTokensUsed() const { return m_totalTokensUsed; }

    // 已注入上下文的 skill 名称列表（/skill 状态显示用）
    std::vector<std::string> getLoadedSkills() const;

    // 生成并缓存会话摘要（/exit 前由 REPL 调用）
    void generateAndCacheSummary();

private:
    // P0-5: turn 级中断消息单点收敛——统一文案 + clearThinking + Warn 状态点
    // （9 处内联收敛至此；工具层 "⎿ ⏹ 已中断" 保持层级区分不走此路径）
    void emitInterrupted();

    // 注入系统身份提示词（构造时 + /clear 后调用）
    void injectSystemPrompt();

    // 构建 Builder Context（injectSystemPrompt + rebuildSystemMessage 共用）
    CLFSystemPromptBuilder::Context buildSystemPromptContext() const;

    // 重建 system 消息（/skill 动态注入后调用）
    void rebuildSystemMessage();

    CLFAgentConfig                    m_config;
    CLFTimerLabels                    m_labels;
    CLFContext                        m_context;
    std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> m_httpClient;
    CLFProtocolAdapter                m_protocolAdapter;
    CLFSecurityPolicy                 m_securityPolicy;
    std::function<bool(const std::string&)> m_confirmCallback;
    std::vector<CLFTool>              m_tools;
    std::vector<std::string>          m_loadedSkills;
    std::unique_ptr<CLFSessionSummarizer> m_summarizer;
    CLFSessionSummary                 m_cachedSummary;    // /exit 时生成，saveSession 时消费
    std::vector<CLFTodoItem>          m_todos;            // S2-6: 待办清单，saveSession 时随会话写入
    ToolStats                         m_lastToolStats;
    long long                         m_totalTokensUsed = 0;  // P2-4 会话累计 token
    CLF::CLFTypes::ICLFOutput*        m_output = nullptr;
    std::atomic<bool>                 m_interrupted{false};
    size_t                           m_lastReasoningSize = 0;  // appendThinking 增量追踪
};

} // namespace CLF::CLFCore
