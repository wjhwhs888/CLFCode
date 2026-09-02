// CLFAgentLoop.hpp — Agent 主循环调度器
// 管理工具调用 → API 调用 → 响应处理的完整流程

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
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
    // 线程安全（2026-09-02，设计-任务清单UI显示 §3.9）：handler 在 asyncSubmit
    // 工作线程写、UI 主线程渲染读——getTodos 锁内拷贝返回副本，setTodos 锁内替换。
    // ⚠️ 返回副本：禁止对结果元素取引用/指针后跨语句使用（临时即亡）；
    // range-for（const auto& t : agent.getTodos()）安全（生命周期延长）
    // example:
    //   agent.setTodos(parsedTodos);
    //   for (const auto& t : agent.getTodos()) show(t);
    std::vector<CLFTodoItem> getTodos() const {
        std::lock_guard<std::mutex> lock(m_todosMutex);
        return m_todos;
    }
    void setTodos(std::vector<CLFTodoItem> todos) {
        std::lock_guard<std::mutex> lock(m_todosMutex);
        m_todos = std::move(todos);
    }

    // —— 会话文件上下文（jsonl 追加式保存，设计-会话追加式保存.jsonl §3.9，2026-09-02）——
    // 所有访问全在 asyncSubmit 工作线程串行（§3.8），锁为防御性

    // 注入历史目录（CLFRepl 构造时调用；beginSessionFile 建文件用）
    void setHistoryDir(const std::string& dir) { m_historyDir = dir; }

    // 当前活动会话文件（空串 = 无活动文件）
    void        setActiveSessionFile(const std::string& jsonlPath);
    std::string getActiveSessionFile() const;   // 锁内拷贝（m_sessionCtxMutex）

    // 懒创建会话文件（CLFRepl::submit 在第一条新对话输入时调用）：
    //   m_resumedFrom 非空 → 复制源文件全部行（header 原样，session_id 延续语义）
    //                        为"时间戳_标题续.jsonl"，随后清 m_resumedFrom
    //   为空            → 全新文件"时间戳_标题.jsonl"（header 含 skills 快照）
    // 返回新文件路径（失败返回空串）
    std::string beginSessionFile(const std::string& firstInput);

    // todo_write handler 在 create/update/clear 成功 setTodos 后立即调用：
    // 锁内取快照 → 追加 todo_snapshot 行 + flush（防崩溃丢进度）；失败 warn 不抛
    void appendTodoSnapshotNow();

    // 轮末由 CLFRepl::submit 调用（替换原 saveSession(false) 覆盖写）：
    // 追加 turn 行（本轮消息差集 + m_todoDirty 时的 todos 快照）→ 清 m_todoDirty
    // 无活动文件 / 本轮无消息 → 跳过；返回活动文件路径（跳过返回空串）
    std::string appendTurnLine();

    // m_todoDirty：仅 create/update/clear 置位（list 不调）；决定 turn 行是否带 todos 快照
    void markTodosDirty() { m_todoDirty.store(true); }

    // 关闭当前会话文件（/clear 用）：生成摘要 → 追加 summary 行 → 关闭（文件保留为
    // 独立会话）。摘要开关关/无效时跳过行写入。幂等（无活动文件时仅生成缓存）
    void closeSessionFileWithSummary();

    // resume 续写态（restoreSession 内部置位；/clear 与 beginSessionFile 清除）
    void              setResumedFrom(const std::string& p) { m_resumedFrom = p; }
    const std::string& getResumedFrom() const { return m_resumedFrom; }

    // todo 面板显示开关（设计-任务清单UI显示 §3.9）：置位后面板隐藏；
    // create 时清除、全完成收尾时置位、resume 恢复非全完成快照时清除、新回合（submit）时置位
    void setTodoPanelDone(bool done) { m_todoPanelDone.store(done); }
    bool isTodoPanelDone() const      { return m_todoPanelDone.load(); }

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
    mutable std::mutex                m_todosMutex;       // 2026-09-02: 工作线程写 ↔ 主线程渲染读（设计 §3.9）
    // —— jsonl 会话上下文（设计 §3.9，2026-09-02）——
    std::atomic<bool>                 m_todoPanelDone{false};  // 面板显示开关（工作线程置位 ↔ 渲染读）
    std::atomic<bool>                 m_todoDirty{false};      // 本轮操作过 create/update/clear（list 不置）
    std::string                       m_activeSessionFile;     // 活动会话文件（防御性互斥见下）
    mutable std::mutex                m_sessionCtxMutex;       // m_activeSessionFile 读写互斥（防御性）
    std::string                       m_resumedFrom;           // 非空 = resume 续写态（工作线程串行，无锁）
    std::string                       m_historyDir;            // 会话历史目录（CLFRepl 构造时注入）
    size_t                            m_turnStartMsgCount = 0; // runTurn 入口轮初消息数（appendTurnLine 差集）
    ToolStats                         m_lastToolStats;
    long long                         m_totalTokensUsed = 0;  // P2-4 会话累计 token
    CLF::CLFTypes::ICLFOutput*        m_output = nullptr;
    std::atomic<bool>                 m_interrupted{false};
    size_t                           m_lastReasoningSize = 0;  // appendThinking 增量追踪
};

} // namespace CLF::CLFCore
