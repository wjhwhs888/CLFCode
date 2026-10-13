// CLFEventBus.cpp — 事件总线实现（事件化重构步骤 2，分册 1 §三）

#include "CLFTypes/CLFEventBus.hpp"

namespace CLF::CLFTypes {

// ============================================================================
// 订阅句柄
// ============================================================================

void CLFSubscription::unsubscribe() {
    if (m_bus && m_id >= 0) m_bus->unsubscribe(m_id);
}

// ============================================================================
// CLFEventBus
// ============================================================================

namespace {
constexpr size_t kLosslessCapacity  = 1024;  // 语义帧默认容量（单回合 < 100 条）
constexpr size_t kCoalescingCapacity = 256;  // 增量帧默认容量
} // anonymous namespace

CLFEventBus::CLFEventBus(bool startDispatcher)
    : m_stopped(!startDispatcher) {}

CLFEventBus::~CLFEventBus() {
    stop();
}

void CLFEventBus::stop() {
    std::vector<SubscriptionState*> subs;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_stopped) return;
        m_stopped = true;
        for (auto& s : m_subs) subs.push_back(s.get());
    }
    for (auto* s : subs) {
        std::lock_guard<std::mutex> qlk(s->queueMutex);
        s->stopped = true;
        s->queueCv.notify_all();
    }
    // join 在 m_mutex 外（分发线程 handler 可能回调 publish → 避免锁序死锁）
    for (auto* s : subs) {
        if (s->thread.joinable()) s->thread.join();
    }
}

CLFSubscription CLFEventBus::subscribe(Handler handler, ChannelPolicy policy,
                                       size_t capacity) {
    if (capacity == 0) {
        capacity = (policy == ChannelPolicy::Lossless) ? kLosslessCapacity
                                                       : kCoalescingCapacity;
    }
    auto state = std::make_unique<SubscriptionState>();
    state->handler = std::move(handler);
    state->policy = policy;
    state->capacity = capacity;

    CLFSubscription sub;
    std::unique_lock<std::mutex> lk(m_mutex);
    state->id = m_nextSubId++;
    sub.m_id = state->id;
    sub.m_bus = this;
    SubscriptionState* raw = state.get();
    m_subs.push_back(std::move(state));

    if (!m_stopped) {
        // 分发线程：drain 自己的队列 → 调 handler（try/catch 隔离）
        raw->thread = std::thread([raw]() {
            for (;;) {
                CLFAgentEvent ev;
                {
                    std::unique_lock<std::mutex> qlk(raw->queueMutex);
                    raw->queueCv.wait(qlk, [&] {
                        return !raw->queue.empty() || raw->stopped;
                    });
                    if (raw->stopped && raw->queue.empty()) return;
                    ev = std::move(raw->queue.front());
                    raw->queue.pop_front();
                    raw->queueCv.notify_one();  // 腾出空位 → 唤醒阻塞的生产者（Lossless 背压）
                }
                try {
                    raw->handler(ev);
                } catch (...) {}  // B6：订阅者异常隔离
            }
        });
    }
    lk.unlock();
    return sub;
}

void CLFEventBus::publish(CLFAgentEvent ev) {
    ev.m_seq = m_nextSeq.fetch_add(1);  // 单生产者保序：全局严格递增

    // 请求-响应路由：ConfirmResponse 匹配等待表 → 唤醒（未匹配照常分发）
    if (ev.m_type == CLFEventType::ConfirmResponse) {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_pending.find(ev.m_requestId);
        if (it != m_pending.end()) {
            std::lock_guard<std::mutex> rlk(*it->second.mutex);
            *it->second.acceptedPtr = ev.m_accepted;
            *it->second.repliedPtr = true;
            it->second.cv->notify_all();
        }
    }

    // 事件历史环：有界、满丢最旧（qa 断言与跨进程重连用）
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_history.push_back(ev);
        while (m_history.size() > kHistoryCapacity) m_history.pop_front();
    }

    // 订阅快照（锁内拷贝裸指针——unsubscribe 只置 stopped 不释放，指针恒有效）
    std::vector<SubscriptionState*> subs;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        subs.reserve(m_subs.size());
        for (auto& s : m_subs) {
            if (!s->stopped) subs.push_back(s.get());
        }
    }
    for (auto* s : subs) {
        // 通道路由：增量帧只入 Coalescing 订阅、语义帧只入 Lossless——
        // 协议双通道分层（全事件订阅者订两通道各一，无重复）
        if (isDeltaEvent(ev.m_type) != (s->policy == ChannelPolicy::Coalescing)) {
            continue;
        }
        std::unique_lock<std::mutex> qlk(s->queueMutex);
        if (s->policy == ChannelPolicy::Lossless) {
            // 满 → 阻塞生产者（保序不丢硬约束，宁慢不错）；停止后不再阻塞
            s->queueCv.wait(qlk, [&] {
                return s->queue.size() < s->capacity || s->stopped;
            });
            if (s->stopped && s->queue.size() >= s->capacity) continue;  // 停止后满 → 丢弃
            s->queue.push_back(ev);   // 拷贝：多订阅者各自持完整事件（move 会掏空）
        } else {
            // Coalescing：满时丢队内最旧、保留最新（渲染只需最新画面）
            if (s->queue.size() >= s->capacity) s->queue.pop_front();
            s->queue.push_back(ev);
        }
        s->queueCv.notify_one();
    }
}

bool CLFEventBus::publishRequestAndWait(CLFAgentEvent request,
                                        std::chrono::milliseconds timeout) {
    std::mutex replyMutex;
    std::condition_variable replyCv;
    bool replied = false;
    bool accepted = false;

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        PendingRequest& slot = m_pending[request.m_requestId];
        slot.cv = &replyCv;
        slot.mutex = &replyMutex;
        slot.repliedPtr = &replied;
        slot.acceptedPtr = &accepted;
    }
    publish(std::move(request));
    {
        std::unique_lock<std::mutex> rlk(replyMutex);
        replyCv.wait_for(rlk, timeout, [&] { return replied; });
    }
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_pending.erase(request.m_requestId);
    }
    return accepted;  // 超时未答复 → false（默认拒绝语义）
}

void CLFEventBus::unsubscribe(int subId) {
    SubscriptionState* target = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& s : m_subs) {
            if (s->id == subId && !s->stopped) { target = s.get(); break; }
        }
    }
    if (!target) return;  // 幂等（未找到/已退订）
    {
        std::lock_guard<std::mutex> qlk(target->queueMutex);
        target->stopped = true;
        target->queueCv.notify_all();
    }
    if (target->thread.joinable()) target->thread.join();
}

size_t CLFEventBus::drainAll() {
    // 先快照订阅指针再释放总线锁：handler 可能回调 publish（锁序死锁防护）
    std::vector<SubscriptionState*> subs;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        subs.reserve(m_subs.size());
        for (auto& s : m_subs) subs.push_back(s.get());
    }
    size_t drained = 0;
    for (auto* s : subs) {
        for (;;) {
            CLFAgentEvent ev;
            {
                std::unique_lock<std::mutex> qlk(s->queueMutex);
                if (s->queue.empty()) break;
                ev = std::move(s->queue.front());
                s->queue.pop_front();
                s->queueCv.notify_one();  // 腾出空位 → 唤醒阻塞的生产者（Lossless 背压）
            }
            // 锁外调 handler：handler 可能回调 publish（同一订阅再入队——
            // 持队列锁会自死锁；A9 confirm 往返实抓，2026-10-13）
            ++drained;
            try {
                s->handler(ev);
            } catch (...) {}  // B6：订阅者异常隔离
        }
    }
    return drained;
}

std::vector<CLFAgentEvent> CLFEventBus::historySnapshot() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return {m_history.begin(), m_history.end()};
}

size_t CLFEventBus::subscriptionCount() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_subs.size();
}

} // namespace CLF::CLFTypes
