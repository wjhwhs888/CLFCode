// CLFTerminalEventAdapter.cpp — 总线订阅者 → ICLFOutput 映射适配器实现
// 步骤 2：只记录不渲染；分册 2 块 1：装饰渲染启用（✻/📋/⏹/状态行自计时）。

#include "CLFTUI/CLFTerminalEventAdapter.hpp"

#include "CLFTypes/CLFEvents.hpp"
#include "CLFTypes/CLFPeriodicTimer.hpp"
#include "CLFTypes/CLFTextUtil.hpp"   // splitLines（HistoryAssistant 多行拆分）

namespace CLF::CLFTUI {

// 触顶提示文案（分册 2 块 1 迁入——原 AgentLoop 匿名命名空间常量逐字：
// 用户可见；非 [Error] 开头——触顶是阶段完成点，不是错误）
constexpr const char* kTurnCapNotice =
    "\n(已达工具调用上限，任务可能未完成——输入「继续」可接着做)\n";

using CLF::CLFTypes::CLFAgentEvent;
using CLF::CLFTypes::CLFEventBus;
using CLF::CLFTypes::CLFEventType;
using CLF::CLFTypes::ChannelPolicy;
using CLF::CLFTypes::CLFPeriodicTimer;
using CLF::CLFTypes::ICLFOutput;

// 状态行自计时器（原 AgentLoop turnTimer 的状态行职责迁移——设计 4.2：
// 50ms 驱动照旧；转圈 requestRefresh 保留在 TurnTimerGuard，职责一分为二）
class CLFTerminalEventAdapter::StatusTimer {
public:
    StatusTimer(ICLFOutput* out, const CLF::CLFCore::CLFTimerLabels& labels)
        : m_out(out)
        , m_labels(labels)
        , m_timer(std::chrono::milliseconds(50), [this]() {
              if (!m_out) return;
              const auto s = std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::steady_clock::now() - m_start).count();
              // P1-1 原语义：≥15s 才显示 + 按秒去重（dsh 规则）
              if (s >= 15 && s != m_lastShownSec) {
                  m_lastShownSec = s;
                  m_out->setStatusTextOnly(
                      m_labels.working + " for "
                      + std::to_string(static_cast<int>(s)) + "s…");
              }
              m_out->requestRefresh();
          }) {}

    ~StatusTimer() {
        m_timer.stop();
        if (m_out) m_out->setStatus("");
    }

private:
    ICLFOutput* m_out;
    CLF::CLFCore::CLFTimerLabels m_labels;   // 文案副本（UI 侧持有——设计 1.4）
    std::chrono::steady_clock::time_point m_start{std::chrono::steady_clock::now()};
    int m_lastShownSec = -1;   // 声明先于 m_timer（析构逆序：定时器先停）
    CLFPeriodicTimer m_timer;
};

CLFTerminalEventAdapter::CLFTerminalEventAdapter(CLF::CLFTypes::CLFEventBus* bus,
                                                 CLF::CLFTypes::ICLFOutput* output,
                                                 const CLF::CLFCore::CLFTimerLabels* labels,
                                                 bool renderToOutput)
    : m_bus(bus), m_output(output), m_labels(labels), m_render(renderToOutput) {
    if (!m_bus) return;
    // 语义帧（Lossless 保序不丢）+ 增量帧（Coalescing 丢旧留新）双订阅
    m_semanticSub = m_bus->subscribe(
        [this](const CLFAgentEvent& ev) { onEvent(ev); },
        ChannelPolicy::Lossless);
    m_deltaSub = m_bus->subscribe(
        [this](const CLFAgentEvent& ev) { onEvent(ev); },
        ChannelPolicy::Coalescing);
}

CLFTerminalEventAdapter::~CLFTerminalEventAdapter() {
    if (m_semanticSub.m_bus) m_semanticSub.unsubscribe();
    if (m_deltaSub.m_bus) m_deltaSub.unsubscribe();
}

void CLFTerminalEventAdapter::onEvent(const CLF::CLFTypes::CLFAgentEvent& ev) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_received.push_back(ev);
        switch (ev.m_type) {
        case CLFEventType::ContentDelta:
        case CLFEventType::ReasoningDelta:
        case CLFEventType::ThinkingCleared:
            ++m_deltaCount;
            break;
        default:
            ++m_semanticCount;
            break;
        }
    }
    if (!m_render) return;   // 步骤 2 记录版：零 ICLFOutput 调用

    // —— 分册 2 块 1：装饰渲染（流式/工具渲染留分册 3）——
    switch (ev.m_type) {
    case CLFEventType::TurnStarted:
        // 状态行自计时启动（用户定调 7：UI 自计时）
        stopStatusTimer();
        if (m_output && m_labels) {
            m_statusTimer = std::make_unique<StatusTimer>(m_output, *m_labels);
        }
        // live 回合开始：弃 History 残留（防历史/live 交错）
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_historyLines.clear();
        }
        break;

    case CLFEventType::TurnFinished:
        stopStatusTimer();
        renderTurnFinished(ev);
        break;

    case CLFEventType::Interrupted:
        stopStatusTimer();
        renderInterrupted();
        break;

    // —— 分册 2 块 3：History 重放渲染（resume 回显——与正常对话同一渲染路径）——
    case CLFEventType::HistoryHeader: {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_historyLines.clear();
        std::string head = "● 会话回显";
        if (!ev.m_startedAt.empty()) head += " · " + ev.m_startedAt;
        if (!ev.m_model.empty()) head += " · " + ev.m_model;
        m_historyLines.push_back(head);
        break;
    }
    case CLFEventType::HistoryUser: {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_historyLines.push_back("● 用户 " + ev.m_text);
        break;
    }
    case CLFEventType::HistoryAssistant: {
        std::lock_guard<std::mutex> lk(m_mutex);
        // 多行文本拆分（splitLines——折叠块逐行渲染）
        auto ls = CLF::CLFCore::CLFTextUtil::splitLines(ev.m_text, /*keepEmpty=*/true);
        m_historyLines.insert(m_historyLines.end(), ls.begin(), ls.end());
        break;
    }
    case CLFEventType::HistoryToolTrace: {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::string line = "  ↳ " + ev.m_name;
        if (!ev.m_ok) line += " ✗";
        if (!ev.m_summary.empty()) line += " " + ev.m_summary;
        m_historyLines.push_back(line);
        break;
    }
    case CLFEventType::HistorySummary: {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_historyLines.push_back("📦 摘要：" + ev.m_text);
        break;
    }
    case CLFEventType::ResumeDone: {
        // 折叠块输出（复用 showFoldedBlock——Ctrl+R 展开语义不变）
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_output && !m_historyLines.empty()) {
            const std::string summary = "● 已恢复会话（ctrl+r 展开）";
            m_output->showFoldedBlock(summary, m_historyLines);
        }
        m_historyLines.clear();
        break;
    }

    case CLFEventType::ConfirmRequested:
        // 分册 2 块 1（设计 5.2）：confirm 切总线路径——Terminal::confirm
        // 内部 mutex+cv 等待 UI 线程处理确认条（跨线程安全）；应答经
        // publish 路由回等待表（publishRequestAndWait 在 AgentLoop 线程唤醒）
        if (m_output) {
            const bool accepted = m_output->confirm(ev.m_prompt);
            CLFAgentEvent reply;
            reply.m_type = CLFEventType::ConfirmResponse;
            reply.m_requestId = ev.m_requestId;
            reply.m_accepted = accepted;
            m_bus->publish(reply);
        }
        break;

    default:
        break;
    }
}

// 收尾装饰：① 📋 清单行（全完成）② ✻ worked 行——现 finishTurn :562 与
// appendWorked 格式逐字搬移（core 不再拼，观感不变）
void CLFTerminalEventAdapter::renderTurnFinished(const CLF::CLFTypes::CLFAgentEvent& ev) {
    if (!m_output) return;
    if (!ev.m_todos.empty()) {
        bool allDone = true;
        for (const auto& t : ev.m_todos) {
            if (t.m_status != "completed") { allDone = false; break; }
        }
        if (allDone) {
            std::string summary = "\n📋 任务清单（全部完成）:";
            for (const auto& t : ev.m_todos)
                summary += "\n  ✓ " + t.m_content;
            m_output->emitContent(summary + "\n");
        }
    }
    // 触顶提示文案（分册 2 块 1 迁入——原 runWrapUp 顺序：总结文本 →
    // 提示文案 → worked）
    if (ev.m_capReached) {
        m_output->emitContent(kTurnCapNotice);
    }
    // ✻ worked 行（模型自发宣告结束时不拼——与现 finishTurn 语义一致）
    if (ev.m_modelDeclaredEnd) return;
    const std::string worked = "\n \n✻ "
        + (m_labels ? m_labels->worked : std::string("Worked"))
        + " for " + CLF::CLFCore::formatDurationSeconds(ev.m_workedSec) + "\n \n";
    m_output->emitContent(worked);
}

// ⏹ 中断行 + 清推理 + Warn——现 emitInterrupted 整体迁适配器（设计 1.1）
void CLFTerminalEventAdapter::renderInterrupted() {
    if (!m_output) return;
    m_output->emitContent("\n⏹ 已中断\n");
    m_output->clearThinking();
    m_output->setStatusKind(ICLFOutput::StatusKind::Warn);
}

void CLFTerminalEventAdapter::stopStatusTimer() {
    m_statusTimer.reset();   // 析构停表 + setStatus("")
}

} // namespace CLF::CLFTUI
