// CLFSessionWriter.cpp — 会话落库订阅者实现（分册 2 §2.3）

#include "CLFCore/CLFSessionWriter.hpp"

#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFMessageCodec.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFTypes/CLFEventBus.hpp"
#include "CLFTypes/CLFTextUtil.hpp"

namespace CLF::CLFCore {

using CLF::CLFTypes::CLFAgentEvent;
using CLF::CLFTypes::CLFEventBus;
using CLF::CLFTypes::CLFEventType;
using CLF::CLFTypes::ChannelPolicy;

CLFSessionWriter::CLFSessionWriter(CLF::CLFTypes::CLFEventBus* bus,
                                   std::function<std::string()> activeFileGetter,
                                   bool startThread)
    : m_bus(bus), m_activeFileGetter(std::move(activeFileGetter)) {
    if (m_bus) {
        m_subId = m_bus->subscribe(
            [this](const CLFAgentEvent& ev) { onEvent(ev); },
            ChannelPolicy::Lossless).m_id;
    }
    if (startThread) {
        m_thread = std::thread([this]() { writerLoop(); });
    }
}

CLFSessionWriter::~CLFSessionWriter() {
    // ① 先退订：分发线程把订阅队列剩余事件冲进内部队列后才退出
    //   （CLFEventBus 停止语义：stopped 后继续 drain 至空）
    if (m_bus && m_subId >= 0) m_bus->unsubscribe(m_subId);
    // ② 再停落盘线程（其继续消费内部队列至空）
    {
        std::lock_guard<std::mutex> lk(m_queueMutex);
        m_stopped = true;
        m_queueCv.notify_all();
    }
    if (m_thread.joinable()) m_thread.join();
    // ③ 兜底 drain（startThread=false 路径的残留事件）
    pumpOnce();
}

void CLFSessionWriter::onEvent(const CLF::CLFTypes::CLFAgentEvent& ev) {
    // 订阅回调：只入队，立即返回（快操作永不阻塞——异步落盘目标）
    std::lock_guard<std::mutex> lk(m_queueMutex);
    if (m_stopped) return;
    m_queue.push_back(ev);
    m_queueCv.notify_one();
}

void CLFSessionWriter::writerLoop() {
    for (;;) {
        CLFAgentEvent ev;
        {
            std::unique_lock<std::mutex> lk(m_queueMutex);
            m_queueCv.wait(lk, [&] { return !m_queue.empty() || m_stopped; });
            if (m_queue.empty()) return;   // stopped 且队列空 → 退出
            ev = std::move(m_queue.front());
            m_queue.pop_front();
        }
        try {
            handleEvent(ev);
        } catch (...) {}   // 落盘异常隔离（订阅者纪律：不拖死总线）
    }
}

size_t CLFSessionWriter::pumpOnce() {
    std::vector<CLFAgentEvent> batch;
    {
        std::lock_guard<std::mutex> lk(m_queueMutex);
        while (!m_queue.empty()) {
            batch.push_back(std::move(m_queue.front()));
            m_queue.pop_front();
        }
    }
    for (const auto& ev : batch) {
        try {
            handleEvent(ev);
        } catch (...) {}
    }
    return batch.size();
}

void CLFSessionWriter::handleEvent(const CLF::CLFTypes::CLFAgentEvent& ev) {
    switch (ev.m_type) {
    case CLFEventType::TurnStarted:
        // 新回合开始：记 user 输入（上一轮状态已在 TurnFinished 清空）
        m_userInput = ev.m_text;
        break;

    case CLFEventType::ToolFinished:
        // 累积工具事件（event_log 素材——一行一工具，回合末随 turn 落盘）
        m_toolEvents.push_back(ev);
        break;

    case CLFEventType::TodoUpdated: {
        // ① 事件到达即写：清单变化立即落 todo_snapshot 行（不等轮末）
        const std::string path = m_activeFileGetter ? m_activeFileGetter() : "";
        if (path.empty()) return;
        const std::string line = CLFMessageCodec::serializeTodoSnapshot(
            ev.m_todos, CLFSessionManager::timestampNow());
        if (!CLFSessionManager::appendTodoSnapshot(path, line)) {
            CLFLogger::instance().warn("[SessionWriter] todo_snapshot append failed: " + path);
        }
        break;
    }

    case CLFEventType::TurnFinished:
        handleTurnFinished(ev);
        break;

    default:
        break;   // 其余语义帧不落库（ContentDelta 等增量帧不入本订阅）
    }
}

void CLFSessionWriter::handleTurnFinished(const CLF::CLFTypes::CLFAgentEvent& ev) {
    const std::string path = m_activeFileGetter ? m_activeFileGetter() : "";
    const std::string ts = CLFSessionManager::timestampNow();

    // 从事件流重建本轮消息：user（TurnStarted）+ assistant（TurnFinished.text）
    // + tool 消息（ToolFinished 累积——summary 即工具结果全文）
    std::vector<CLFMessage> messages;
    messages.reserve(2 + m_toolEvents.size());
    if (!m_userInput.empty()) {
        CLFMessage user;
        user.m_role = "user";
        user.m_content = m_userInput;
        messages.push_back(std::move(user));
    }
    if (!ev.m_text.empty()) {
        CLFMessage assistant;
        assistant.m_role = "assistant";
        assistant.m_content = ev.m_text;
        messages.push_back(std::move(assistant));
    }
    for (const auto& tool : m_toolEvents) {
        CLFMessage tmsg;
        tmsg.m_role = "tool";
        tmsg.m_toolCallId = tool.m_callId;
        tmsg.m_name = tool.m_name;
        // 截断口径与差集路径统一（truncateToolResult 头尾 8000×2，W-6）
        tmsg.m_content = CLFTextUtil::truncateToolResult(tool.m_summary);
        messages.push_back(std::move(tmsg));
    }

    if (!path.empty() && !messages.empty()) {
        const std::string line = CLFMessageCodec::serializeTurnLine(
            messages, ts, ev.m_todos.empty() ? nullptr : &ev.m_todos, ev.m_turnId);
        if (!CLFSessionManager::appendTurn(path, line)) {
            CLFLogger::instance().warn("[SessionWriter] turn append failed: " + path);
        }
        // event_log 明细：一行一工具（恢复时完整重建"当时执行了什么"；
        // summary 同截断口径——长输出不撑爆文件）
        for (const auto& tool : m_toolEvents) {
            const std::string el = CLFMessageCodec::serializeEventLog(
                ev.m_turnId, ts, tool.m_callId, tool.m_name,
                tool.m_ok, CLFTextUtil::truncateToolResult(tool.m_summary),
                tool.m_seq);
            if (!CLFSessionManager::appendEventLog(path, el)) {
                CLFLogger::instance().warn("[SessionWriter] event_log append failed: " + path);
            }
        }
    }

    // 回合边界：清本轮状态（下次 TurnStarted 重新累积）
    m_userInput.clear();
    m_toolEvents.clear();
}

} // namespace CLF::CLFCore
