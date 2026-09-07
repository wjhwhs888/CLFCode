// CLFSessionFileCtx.hpp — 会话文件上下文（C2：AgentLoop 拆角色，2026-09-07）
// jsonl 追加式会话文件的状态与操作（设计-会话追加式保存.jsonl §3.9）。
// 线程模型：所有访问全在 asyncSubmit 工作线程串行（§3.8），锁为防御性。
// 序列化经 CLFMessageCodec / CLFSessionManager（core 静态工具）。
// 不持 AgentLoop 引用（modelName/loadedSkills 由调用方参数传入）。
//
// example:
//   CLFSessionFileCtx fileCtx;
//   fileCtx.setHistoryDir(dir);
//   std::string path = fileCtx.beginSessionFile(input, model, skills);

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "CLFTypes/CLFTypes.hpp"

namespace CLF::CLFCore {

class CLFSessionFileCtx {
public:
    // 注入历史目录（CLFRepl 构造时调用；beginSessionFile 建文件用）
    void setHistoryDir(const std::string& dir) { m_historyDir = dir; }

    // 当前活动会话文件（空串 = 无活动文件）；锁内拷贝（防御性互斥）
    void        setActiveSessionFile(const std::string& jsonlPath);
    std::string getActiveSessionFile() const;

    // resume 续写态（restoreSession 置位；/clear 与 beginSessionFile 清除）
    void              setResumedFrom(const std::string& p) { m_resumedFrom = p; }
    const std::string& getResumedFrom() const { return m_resumedFrom; }
    void              clearResumedFrom() { m_resumedFrom.clear(); }

    // 懒创建会话文件（原 CLFAgentLoop::beginSessionFile 搬入）：
    //   resumedFrom 非空 → 复制源文件全部行（header 原样，session_id 延续语义）
    //                       为"时间戳_标题续.jsonl"，随后清 resumedFrom
    //   为空            → 全新文件"时间戳_标题.jsonl"（header 含 skills 快照）
    // modelName/loadedSkills 用于 header 序列化（调用方传入，不持引用）
    // 返回新文件路径（失败返回空串）
    std::string beginSessionFile(const std::string& firstInput,
                                 const std::string& modelName,
                                 const std::vector<std::string>& loadedSkills);

    // 轮初消息数（appendTurn 的差集基准；user 消息计入本轮新增）
    void setTurnStartMsgCount(size_t count) { m_turnStartMsgCount = count; }

    // 追加 turn 行：差集（msgs 相对轮初消息数）→ 序列化 → 追加文件。
    // todosPtr 非空时随行带 todos 快照（调用期间有效）。
    // 无活动文件 / 本轮无新消息 → 跳过；返回活动文件路径（跳过返回空串）
    std::string appendTurn(const std::vector<CLFMessage>& msgs,
                           const std::vector<CLFTodoItem>* todosPtr);

    // 追加 summary 行（summary 无效或无活动文件 → 跳过；失败 warn 不抛）
    void appendSummaryLine(const CLFSessionSummary& summary);

    // restoreSession 回显行收集（B4：core 不拼 UI 文案，UI 消费结构化行）：
    // jsonl → 逐行解析（turn → User/Assistant + 尾随 TodoRound；complete →
    // TodoComplete；其余行不回显）；非 jsonl → 由 messages 投影
    // （tool/system 跳过）。isJsonl 按扩展名判定。
    void collectEchoLines(const std::string& filePath,
                          const std::vector<CLFMessage>& messages,
                          std::vector<CLFSessionEchoLine>& outEcho) const;

private:
    std::string       m_activeSessionFile;   // 活动会话文件（防御性互斥）
    mutable std::mutex m_sessionCtxMutex;    // m_activeSessionFile 读写互斥（防御性）
    std::string       m_resumedFrom;         // 非空 = resume 续写态（工作线程串行，无锁）
    std::string       m_historyDir;          // 会话历史目录（CLFRepl 构造时注入）
    size_t            m_turnStartMsgCount = 0; // 轮初消息数（appendTurn 差集基准）
};

} // namespace CLF::CLFCore
