// CLFEvents.hpp — 事件协议头（事件化重构步骤 1，分册 1 §二，2026-10-13）
//
// 【协议头即规范】（用户定调）：新增 UI（Qt/HTML 等）以本头为合同接入；
// 第三批跨进程协议在此结构上补 JSON codec（序列化直写各字段）。
//
// 分层设计：
//   - 语义帧（主通道）：保序不丢——TurnStarted/ToolStarted/…/Interrupted
//   - 增量帧（旁路通道）：渲染加速，溢出可合并丢弃——ContentDelta/ReasoningDelta
//   - 请求-响应：ConfirmRequested ↔ ConfirmResponse（60s 超时默认拒绝，用户拍板）
//   - History 重放：resume 回显事件组（分册 2 定案，渲染由订阅者完成）
//
// 稳定关联键：
//   seq       —— 总线分配全局序号（单生产者 = AgentLoop 工作线程 → 保序天然成立）
//   turnId    —— 回合稳定 id（TurnStarted 起，同回合全部事件共用）
//   callId    —— 工具调用配对（沿用 tool_calls 的 m_id）
//   requestId —— confirm 请求-响应对
//
// 枚举值即协议合同：发布后不可重排/删除，只能追加（跨进程双方据此编解码）。
//
// example:
//   CLF::CLFTypes::CLFAgentEvent ev;
//   ev.m_type = CLFEventType::ContentDelta;
//   ev.m_text = "增量文本";
//   bus.publish(ev);

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CLFTypes/CLFTypes.hpp"   // CLFTodoItem（同层既有 POD）

namespace CLF::CLFTypes {

// 事件类型（协议 v1 全集 21 个，四类——分册 1 §2.2b 唯一权威总表）
enum class CLFEventType : uint8_t {
    // ① Live 输出（core → UI，语义帧主通道，保序不丢）
    TurnStarted,      // 回合开始（payload: turnId / text=输入）
    ToolStarted,      // 工具启动（payload: callId / name / argsPreview）
    ToolFinished,     // 工具完成（payload: callId / name / ok / summary / errorKind）——完成可乱序
    StatusChanged,    // 状态点（payload: statusKind 数值 = ICLFOutput::StatusKind 枚举序）
    TodoUpdated,      // 清单变化（payload: todos 全量快照）
    TurnFinished,     // 回合结束（payload: turnId / workedSec / todos / capReached /
                      //   text=assistant 最终全文——分册 2 补充：落库数据源，流式/同步均发）
    Interrupted,      // 回合被中断（无 payload）

    // ② Live 输出（core → UI，增量旁路通道，可合并丢弃——渲染只需最新画面）
    ContentDelta,     // 主内容增量（payload: text，纯文本无装饰）
    ReasoningDelta,   // 推理增量（payload: text）
    ThinkingCleared,  // 清空当前回合推理（无 payload）

    // ③ 请求-响应 + 输入（双向）
    ConfirmRequested, // core → UI：请求确认（payload: requestId / prompt）
    ConfirmResponse,  // UI → core：确认应答（payload: requestId / accepted；60s 超时默认拒绝）
    UserInput,        // UI → core：用户输入（payload: text）
    InterruptRequest, // UI → core：中断请求（无 payload）
    ResumeRequested,  // UI → core：恢复会话（payload: sessionFile）

    // ④ History 重放（core → UI，resume 回显——分册 2 定案）
    HistoryHeader,    // 会话头弱化行（payload: model / startedAt / skills）
    HistoryUser,      // "● 用户" 弱化行（payload: text）
    HistoryAssistant, // 内容（折叠块内；payload: text）
    HistoryToolTrace, // `↳ 工具名 摘要`（payload: callId / name / ok / summary）
    HistorySummary,   // 📦 摘要折叠行（payload: text）
    ResumeDone,       // 回显结束（无 payload）

    // ⑤ 渲染切轨补充（分册 4，2026-10-14——枚举追加，语义帧主通道）
    TurnError,        // 回合内错误/重试文案（payload: text 文案全文，core 拼好——
                      //   ✗ Exception retry / Stream error / 致命 HTTP / ⚠ retry；
                      //   m_isError = emitError 通道（错误样式）否则 emitContent）
    DiffPreview,      // Write 工具的 diff 预览渲染（payload: text=filePath +
                      //   m_lines 行列表 + diff 统计——新 UI 的写文件确认依赖）
};

// 增量帧判定（旁路通道：渲染加速，可合并丢弃）——总线据此路由：
// 增量帧只入 Coalescing 订阅、其余（语义帧/请求-响应/History）只入
// Lossless 订阅。全事件订阅者（如 SessionWriter）订两通道各一。
inline bool isDeltaEvent(CLFEventType type) {
    switch (type) {
    case CLFEventType::ContentDelta:
    case CLFEventType::ReasoningDelta:
    case CLFEventType::ThinkingCleared:
        return true;
    default:
        return false;
    }
}

// 事件载荷：单 struct + 按类型使用字段（tagged union 手工版——与项目 POD
// 惯例一致，JSON codec 直写；字段未用置默认。每字段注释钉死使用事件）
struct CLFAgentEvent {
    CLFEventType m_type{CLFEventType::TurnStarted};

    // —— 稳定关联键 ——
    uint64_t    m_seq{0};     // 总线分配全局序号（单生产者保序；0 = 未分配）
    std::string m_turnId;     // 回合稳定 id（TurnStarted 起同回合事件共用）

    // —— 内容 / 输入 ——
    std::string m_text;       // ContentDelta / ReasoningDelta / UserInput /
                              // TurnStarted.input / TurnFinished（assistant 最终全文）/
                              // HistoryUser / HistoryAssistant / HistorySummary

    // —— 工具 ——
    std::string m_callId;     // ToolStarted / ToolFinished / HistoryToolTrace
    std::string m_name;       // 同上（工具名）
    std::string m_argsPreview;// ToolStarted（extractKeyParam 结果）
    bool        m_ok{true};   // ToolFinished / HistoryToolTrace
    std::string m_summary;    // ToolFinished / HistoryToolTrace（summary 行文本，UI 自行呈现）
    std::string m_errorKind;  // ToolFinished（interrupted / timeout / ...）
    // 分册 4 渲染切轨补字段（core 算好显示条件/文案，UI 照做——协议零推断）：
    bool        m_showHeader{false};  // ToolStarted（prepare 的 header 行条件：
                                      //   非渐进 || Write——core 计算）
    std::string m_warningText;        // ToolFinished（prepare 中段的 ⚠ 提示行文案——
                                      //   危险命令/POST 升级；空 = 无警告。⚠ 行需在
                                      //   confirm 前显示但 ToolStarted 发射时条件未知
                                      //   （危险判定在发射后）——随 ToolFinished，
                                      //   渲染序：⚠ → ✗ → ✓/✗）
    std::string m_arguments;          // ToolFinished（工具原始参数 JSON——writer
                                      //   重建 assistant tool_calls 声明用；resume 400
                                      //   实抓修根：事件流此前无声明载体——
                                      //   2026-10-14）
    std::string m_displayResult;      // ToolFinished（finalize 的 ✓/✗ 行**全文**——
                                      //   含前缀换行，core 拼好直显；空 = 不显示
                                      //   （渐进读工具成功仅进度条）。core 拼行文案
                                      //   为工具行的唯一权威——适配器零重算）
    std::string m_displayError;       // ToolFinished（prepare 阶段的 ✗ 行**全文**
                                      //   （含 "  ⎿ ✗ " 前缀与换行——unknown/blocked/
                                      //   denied/预览失败/File too large/File modified）——
                                      //   errorKind 不足以区分行文案，core 拼好直显）

    // —— 状态 / 回合 ——
    bool        m_isError{false};  // TurnError（true = emitError 错误样式通道）
    int         m_statusKind{0};   // StatusChanged（数值 = ICLFOutput::StatusKind 枚举序）
    long long   m_workedSec{0};    // TurnFinished
    bool        m_capReached{false}; // TurnFinished（触顶标志）
    bool        m_modelDeclaredEnd{false}; // TurnFinished（模型自发宣告结束 →
                                           // 适配器不拼 ✻ worked 行——分册 2 块 1）
    bool        m_emitText{false}; // TurnFinished（wrapUp 内容渲染条件——
                                   //   capReached && stream；无条件渲染会与流式双份）
    // CLFTodoItem 位于 CLF::CLFCore（CLFTypes.hpp 历史命名布局）——限定名引用
    std::vector<CLF::CLFCore::CLFTodoItem> m_todos; // TodoUpdated / TurnFinished（全量快照）

    // —— 确认 ——
    std::string m_requestId;  // ConfirmRequested / ConfirmResponse
    std::string m_prompt;     // ConfirmRequested
    bool        m_accepted{false}; // ConfirmResponse

    // —— 会话恢复 ——
    std::string m_sessionFile; // ResumeRequested

    // —— History 重放（分册 2 定案）——
    std::string m_model;      // HistoryHeader
    std::string m_startedAt;  // HistoryHeader
    std::vector<std::string> m_skills; // HistoryHeader

    // —— DiffPreview（分册 4：diff 渲染行列表 + 统计）——
    // 行列表 = renderDiff 的 head/tail 截断后最终渲染条目（≤34 行）；
    // style 数值 = ICLFOutput::LineStyle 枚举序（协议用数值，双方各自映射）
    std::vector<std::pair<std::string, int>> m_lines;
    int         m_diffAdded{0};
    int         m_diffRemoved{0};
    int         m_diffHunks{0};
    bool        m_diffTruncated{false};
};

} // namespace CLF::CLFTypes
