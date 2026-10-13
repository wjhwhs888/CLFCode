// CLFTerminalEventAdapter.cpp — 总线订阅者 → ICLFOutput 映射适配器实现
// 步骤 2（R1 审查修正）：只记录不渲染——零 ICLFOutput 调用；步骤 3 切轨
// 时启用渲染映射（分册 1 §3.4 映射表）。

#include "CLFTUI/CLFTerminalEventAdapter.hpp"

#include "CLFTypes/CLFEvents.hpp"

namespace CLF::CLFTUI {

using CLF::CLFTypes::CLFAgentEvent;
using CLF::CLFTypes::CLFEventBus;
using CLF::CLFTypes::CLFEventType;
using CLF::CLFTypes::ChannelPolicy;

CLFTerminalEventAdapter::CLFTerminalEventAdapter(CLF::CLFTypes::CLFEventBus* bus,
                                                 CLF::CLFTypes::ICLFOutput* output)
    : m_bus(bus), m_output(output) {
    if (!m_bus) return;
    // 语义帧（Lossless 保序不丢）+ 增量帧（Coalescing 丢旧留新）双订阅
    m_semanticSub = m_bus->subscribe(
        [this](const CLFAgentEvent& ev) { onEvent(ev); },
        ChannelPolicy::Lossless);
    m_deltaSub = m_bus->subscribe(
        [this](const CLFAgentEvent& ev) { onEvent(ev); },
        ChannelPolicy::Coalescing);
}

void CLFTerminalEventAdapter::onEvent(const CLF::CLFTypes::CLFAgentEvent& ev) {
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
    // 步骤 3 渲染映射（分册 1 §3.4 映射表）：
    //   StatusChanged → setStatusKind / ContentDelta → emitContent /
    //   ReasoningDelta → appendThinking / ThinkingCleared → clearThinking /
    //   ToolStarted → showProgress / ToolFinished → finishProgress /
    //   ConfirmRequested → confirm → ConfirmResponse（经总线 reply 通道）/
    //   TodoUpdated / TurnFinished / Interrupted → 分册 2 迁移
}

} // namespace CLF::CLFTUI
