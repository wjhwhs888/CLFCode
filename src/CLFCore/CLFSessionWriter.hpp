// CLFSessionWriter.hpp — 会话落库订阅者（事件化重构分册 2 §2.3，2026-10-13）
//
// 定位：总线订阅者 → jsonl 落盘（"产生就存盘"，用户定调）。订阅语义帧
// （Lossless），消费 TurnStarted/ToolFinished/TodoUpdated/TurnFinished，
// 从事件流重建本轮消息落 turn 行 + event_log 明细（一行一工具）。
//
// 内部生产者-消费者（两层队列各司其职）：
//   AgentLoop 发事件 → 总线(writer 订阅队列) → 订阅回调【只入队，立即返回】
//                      → writer 内部队列 → 独立落盘线程（事件到达即写）
// 必要性（设计评审实抓）：订阅线程直接写盘时磁盘慢 → 订阅队列积压 →
// Lossless 满 → 反压阻塞 AgentLoop 发事件——异步落盘目标被破坏。
//
// 纪律（用户定调）：① 事件到达即写（不攒批——每回合事件 <100 条）② 回合
// 边界语义自动满足（TurnFinished 一到即写，崩溃丢失窗口 ≤ 一个回合）③ 析构
// drain（退出前冲干净队列）④ 落库失败仅 warn（现语义）⑤ 队列常态接近空。
//
// startThread=false：qa 手动模式（pumpOnce 手动处理内部队列——静态期线程
// 纪律，同 CLFEventBus startDispatcher 惯例）。
//
// example:
//   CLFSessionWriter writer(&bus, &fileCtx);   // 构造即订阅 + 落盘线程
//   // ... agent 跑回合（bus 已注入）——turn 行/event_log 自动落库 ...
//   // writer 析构 drain：退出前冲干净队列

#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "CLFTypes/CLFEvents.hpp"

namespace CLF::CLFTypes { class CLFEventBus; }
namespace CLF::CLFCore {

class CLFSessionWriter {
public:
    // bus：事件来源（订阅语义帧 Lossless；nullptr = 无事件源，仅测试占位）；
    // activeFileGetter：活动会话文件查询器（借用门面——装配方经 AgentLoop
    // 门面注入；返回空串 = 无活动文件，落库跳过）
    CLFSessionWriter(CLF::CLFTypes::CLFEventBus* bus,
                     std::function<std::string()> activeFileGetter,
                     bool startThread = true);
    ~CLFSessionWriter();

    CLFSessionWriter(const CLFSessionWriter&)            = delete;
    CLFSessionWriter& operator=(const CLFSessionWriter&) = delete;

    // 测试用：手动泵一轮——处理内部队列中全部事件（startThread=false 时使用）
    size_t pumpOnce();

private:
    void onEvent(const CLF::CLFTypes::CLFAgentEvent& ev);  // 订阅回调：只入队
    void writerLoop();                                     // 落盘线程循环
    // 单事件落盘处理（落盘线程 / pumpOnce 共用）
    void handleEvent(const CLF::CLFTypes::CLFAgentEvent& ev);
    // TurnFinished → 组装 turn 行（user/assistant/tool 消息）+ event_log 明细
    void handleTurnFinished(const CLF::CLFTypes::CLFAgentEvent& ev);

    CLF::CLFTypes::CLFEventBus* m_bus = nullptr;
    std::function<std::string()> m_activeFileGetter;  // 活动文件查询器（借用门面）
    int                         m_subId = -1;   // 总线订阅 id（析构退订）

    // 内部队列（订阅回调入队 → 落盘线程消费）
    std::mutex              m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<CLF::CLFTypes::CLFAgentEvent> m_queue;
    bool                    m_stopped = false;
    std::thread             m_thread;

    // 本轮累积状态（落盘线程独占，无锁）
    std::string m_userInput;                              // TurnStarted.text
    std::vector<CLF::CLFTypes::CLFAgentEvent> m_toolEvents;  // ToolFinished 累积
};

} // namespace CLF::CLFCore
