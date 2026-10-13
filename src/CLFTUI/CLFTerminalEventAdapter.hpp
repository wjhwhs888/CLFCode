// CLFTerminalEventAdapter.hpp — 总线订阅者 → ICLFOutput 映射适配器
// （事件化重构步骤 2，分册 1 §3.4，2026-10-13）
//
// 定位：总线订阅者 → ICLFOutput 映射（TUI 零破坏的关键）。事件协议见
// CLFEvents.hpp，映射表见分册 1 §3.4。
//
// ⚠ 步骤 2（审查修正 R1，2026-10-12）：只记录不渲染——事件计数/序列断言
// 用（qa_CLFTerminalEventAdapter），零 ICLFOutput 调用。若此时接线渲染，
// ContentDelta 等事件会与 core 直调路径双份显示。步骤 3 切轨时同步
// "移除 core 直调 + 启用渲染映射"（输出逐字对比作为切轨验收）。
//
// example:
//   CLFEventBus bus;
//   CLFTerminalEventAdapter adapter(&bus, output);   // 订阅语义帧+增量帧
//   // ... agent 跑回合（双轨：直调 + 事件） ...
//   adapter.received();  // 记录的事件（qa 断言用）

#pragma once

#include <mutex>
#include <vector>

#include "CLFTypes/CLFEventBus.hpp"
#include "CLFTypes/ICLFOutput.hpp"

namespace CLF::CLFTUI {

class CLFTerminalEventAdapter {
public:
    // bus：事件来源（订阅语义帧 Lossless + 增量帧 Coalescing）；
    // output：ICLFOutput 目标（步骤 3 渲染映射启用时使用，本步仅持有）
    CLFTerminalEventAdapter(CLF::CLFTypes::CLFEventBus* bus,
                            CLF::CLFTypes::ICLFOutput* output);

    // —— 步骤 2 记录接口（qa 断言用；步骤 3 切轨后由渲染路径替代）——
    const std::vector<CLF::CLFTypes::CLFAgentEvent>& received() const { return m_received; }
    size_t semanticCount() const { return m_semanticCount; }  // 语义帧计数
    size_t deltaCount() const    { return m_deltaCount; }     // 增量帧计数

private:
    void onEvent(const CLF::CLFTypes::CLFAgentEvent& ev);

    CLF::CLFTypes::CLFEventBus* m_bus = nullptr;
    CLF::CLFTypes::ICLFOutput*  m_output = nullptr;  // 步骤 3 渲染目标
    mutable std::mutex          m_mutex;
    std::vector<CLF::CLFTypes::CLFAgentEvent> m_received;
    size_t                      m_semanticCount = 0;
    size_t                      m_deltaCount = 0;
    CLF::CLFTypes::CLFSubscription m_semanticSub;
    CLF::CLFTypes::CLFSubscription m_deltaSub;
};

} // namespace CLF::CLFTUI
