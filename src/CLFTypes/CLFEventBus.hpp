// CLFEventBus.hpp — 事件总线（事件化重构步骤 2，分册 1 §三，2026-10-13）
//
// 定位：事件总线唯一通道——AgentLoop（生产者）→ 总线（分发）→ 前端订阅者；
// SessionWriter（消费者，"产生就存盘"）。协议与载荷见 CLFEvents.hpp。
//
// 线程模型（定案）：
//   - 生产者：AgentLoop 工作线程（单生产者 → 全局 seq 保序天然成立）
//   - 每订阅者一线程：订阅时创建消费线程 drain 自己的队列 → 调 handler
//     （try/catch 隔离——订阅者异常不拖死总线/生产者）
//   - 请求-响应：publishRequestAndWait 在生产者线程阻塞等（cv + timeout）
//   - 分发线程开关 startDispatcher=false（qa 静态期禁用——审查修正 R2）：
//     手动 publish + drainAll 同步消费
//
// 溢出与背压（定案）：
//   - Lossless（语义帧）：队列满 → 阻塞生产者（保序不丢是硬约束，宁慢不错；
//     容量默认 1024，单回合事件量级 < 100 实际不会满）
//   - Coalescing（增量帧）：满时丢弃队内最旧、保留最新（渲染只需最新画面）
//   - 事件历史环：有界 4096、满丢最旧（仅 qa 断言与未来跨进程重连用）
//
// example:
//   CLFEventBus bus;                    // 构造即启动分发线程
//   auto sub = bus.subscribe([](const CLFAgentEvent& ev) { ... },
//                            ChannelPolicy::Lossless);
//   CLFAgentEvent ev; ev.m_type = CLFEventType::ContentDelta;
//   ev.m_text = "hello"; bus.publish(ev);
//   sub.unsubscribe();

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "CLFTypes/CLFEvents.hpp"

namespace CLF::CLFTypes {

// 通道策略（订阅时声明，逐订阅生效）
enum class ChannelPolicy {
    Lossless,   // 语义帧：有界队列满 → 阻塞生产者；容量默认 1024
    Coalescing, // 增量帧：满时丢队内最旧、保留最新；容量默认 256
};

// 订阅句柄（退订用；析构不自动退订——生命周期由订阅方管理）
class CLFEventBus;
struct CLFSubscription {
    int          m_id = -1;   // -1 = 无效订阅
    CLFEventBus* m_bus = nullptr;

    // 退订：置该订阅停止（分发线程退出；队列保留可 drain——不丢弃语义）
    void unsubscribe();
};

class CLFEventBus {
public:
    using Handler = std::function<void(const CLFAgentEvent&)>;

    // startDispatcher=false：不创建分发线程（qa 静态期纪律——手动
    // publish + drainAll；生产装配始终 true）
    explicit CLFEventBus(bool startDispatcher = true);
    ~CLFEventBus();

    CLFEventBus(const CLFEventBus&)            = delete;
    CLFEventBus& operator=(const CLFEventBus&) = delete;

    // 订阅：handler 在分发线程被调（startDispatcher=false 时由 drainAll
    // 在调用线程同步调）；capacity=0 → 按通道策略默认（Lossless 1024 /
    // Coalescing 256）。停止后订阅照常入队（可 drain，不丢弃语义）
    CLFSubscription subscribe(Handler handler, ChannelPolicy policy,
                              size_t capacity = 0);

    // 发射（单生产者 = AgentLoop 工作线程）：seq 总线分配（全局严格递增）；
    // 按订阅策略入队；ConfirmResponse 自动路由到请求-响应等待表
    void publish(CLFAgentEvent ev);

    // 请求-响应原语（confirm 专用）：publish ConfirmRequested 后阻塞等
    // ConfirmResponse（requestId 匹配）；超时返回 false（默认拒绝语义，
    // 用户拍板 60s——挂机场景不无限卡任务）
    bool publishRequestAndWait(CLFAgentEvent request,
                               std::chrono::milliseconds timeout);

    // 退订（幂等）：置该订阅停止、分发线程退出；队列保留可 drain
    void unsubscribe(int subId);

    // 停止分发线程（幂等；停止后未消费事件保留在队列可 drain——不丢弃语义）
    void stop();

    // 测试/跨进程重连用：同步消费全部订阅队列（仅 startDispatcher=false
    // 时使用；分发线程运行时调用无意义）
    size_t drainAll();

    // 事件历史环快照（有界 4096、满丢最旧——qa 断言与未来跨进程重连用）
    std::vector<CLFAgentEvent> historySnapshot() const;

    // 订阅总数（qa 断言用）
    size_t subscriptionCount() const;

private:
    struct SubscriptionState {
        int            id = -1;
        Handler        handler;
        ChannelPolicy  policy = ChannelPolicy::Lossless;
        size_t         capacity = 0;
        std::mutex     queueMutex;
        std::condition_variable queueCv;
        std::deque<CLFAgentEvent> queue;
        bool           stopped = false;   // 停止后不再分发（队列保留可 drain）
        std::thread    thread;            // 分发线程（startDispatcher 时创建）
    };

    // 请求-响应等待表项（等待者自带 cv/mutex 与结果槽——publish 侧指针唤醒）
    struct PendingRequest {
        std::condition_variable* cv = nullptr;
        std::mutex*              mutex = nullptr;
        bool*                    repliedPtr = nullptr;
        bool*                    acceptedPtr = nullptr;
    };

    static constexpr size_t kHistoryCapacity = 4096;

    std::atomic<uint64_t> m_nextSeq{1};          // 全局 seq（单生产者保序）
    mutable std::mutex    m_mutex;               // 订阅列表/等待表锁
    std::vector<std::unique_ptr<SubscriptionState>> m_subs;
    int                   m_nextSubId = 1;
    std::unordered_map<std::string, PendingRequest> m_pending;
    std::deque<CLFAgentEvent> m_history;         // 事件历史环（满丢最旧）
    bool                  m_stopped = false;
};

} // namespace CLF::CLFTypes
