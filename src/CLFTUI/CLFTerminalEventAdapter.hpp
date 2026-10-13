// CLFTerminalEventAdapter.hpp — 总线订阅者 → ICLFOutput 映射适配器
// （事件化重构步骤 2 + 分册 2 块 1，分册 1 §3.4，2026-10-13）
//
// 定位：总线订阅者 → ICLFOutput 映射（TUI 零破坏的关键）。事件协议见
// CLFEvents.hpp，映射表见分册 1 §3.4。
//
// 两阶段：
//   步骤 2（R1 审查修正）：只记录不渲染（renderToOutput=false，默认）——
//     事件计数/序列断言用，零 ICLFOutput 调用
//   分册 2 块 1：装饰渲染启用（renderToOutput=true）——TurnFinished →
//     📋/✻ 行、Interrupted → ⏹ 行 + 清推理 + Warn、状态行自计时
//     （TurnStarted 起 50ms / TurnFinished+Interrupted 停）。
//     流式内容/工具显示渲染留分册 3（双轨期 core 直调保留——避免双份显示）
//
// 线程：渲染映射在分发线程直接调 ICLFOutput——与现状 TurnTimerGuard 定时器
// 线程跨线程调 ICLFOutput 同模式（CLFTerminal 已容忍；requestRefresh 为
// PostEvent 线程安全投递）。时序偏差为分册 1 §六观察项，实机闪烁再上桥。
//
// example:
//   CLFEventBus bus;
//   CLFTerminalEventAdapter adapter(&bus, output, &labels, /*render=*/true);
//   // agent 跑回合：✻/📋/⏹/状态行由适配器渲染，core 只发事件

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

#include "CLFTypes/CLFEventBus.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFTypes/CLFTypes.hpp"   // CLFTimerLabels / formatDurationSeconds

namespace CLF::CLFTUI {

class CLFTerminalEventAdapter {
public:
    // bus：事件来源（订阅语义帧 Lossless + 增量帧 Coalescing）；
    // output：渲染目标（render 时使用；记录模式可 nullptr）；
    // labels：✻/状态行文案集（render 时必需——worked/working）；
    // renderToOutput：false = 步骤 2 记录版（零 ICLFOutput 调用）；
    //                true = 装饰渲染启用（分册 2 块 1）
    CLFTerminalEventAdapter(CLF::CLFTypes::CLFEventBus* bus,
                            CLF::CLFTypes::ICLFOutput* output,
                            const CLF::CLFCore::CLFTimerLabels* labels = nullptr,
                            bool renderToOutput = false);
    ~CLFTerminalEventAdapter();

    // —— 记录接口（qa 断言用；与渲染并存——记录总是启用）——
    const std::vector<CLF::CLFTypes::CLFAgentEvent>& received() const { return m_received; }
    size_t semanticCount() const { return m_semanticCount; }  // 语义帧计数
    size_t deltaCount() const    { return m_deltaCount; }     // 增量帧计数

private:
    void onEvent(const CLF::CLFTypes::CLFAgentEvent& ev);
    // 装饰渲染（renderToOutput 时调用）
    void renderTurnFinished(const CLF::CLFTypes::CLFAgentEvent& ev);
    void renderInterrupted();
    void stopStatusTimer();

    CLF::CLFTypes::CLFEventBus* m_bus = nullptr;
    CLF::CLFTypes::ICLFOutput*  m_output = nullptr;
    const CLF::CLFCore::CLFTimerLabels*       m_labels = nullptr;
    bool                        m_render = false;

    // 状态行自计时（用户定调 7：UI 自计时——TurnStarted 起 / 回合结束停）
    class StatusTimer;
    std::unique_ptr<StatusTimer> m_statusTimer;

    mutable std::mutex          m_mutex;
    std::vector<CLF::CLFTypes::CLFAgentEvent> m_received;
    size_t                      m_semanticCount = 0;
    size_t                      m_deltaCount = 0;
    // History 重放累积行（HistoryHeader 起收集 → ResumeDone 折叠块输出；
    // TurnStarted 清空——live 回合开始弃残留，防止历史/live 交错）
    std::vector<std::string>    m_historyLines;
    CLF::CLFTypes::CLFSubscription m_semanticSub;
    CLF::CLFTypes::CLFSubscription m_deltaSub;
};

} // namespace CLF::CLFTUI
