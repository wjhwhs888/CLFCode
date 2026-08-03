// CLFEventQueue.hpp — 线程安全事件队列
// 环形缓冲, 主线程读, 后台线程写(ThinkingIndicator/HTTP回调)

#pragma once

#include "CLFCore/CLFEvent.hpp"

#include <array>
#include <atomic>
#include <mutex>

namespace CLF::CLFCore {

class CLFEventQueue {
public:
    static constexpr int kMaxEvents = 256;

    // 推入事件（线程安全, 后台线程调用）
    // 队列满时合并连续的 ContentAppend, 或丢弃最旧事件
    void push(const Event& e);

    // 是否为空（仅主线程调用）
    bool empty() const { return m_read == m_write.load(std::memory_order_acquire); }

    // 取出队首事件（仅主线程调用, 调用前检查 !empty()）
    Event pop();

    // 合并优化: 连续 ContentAppend → 拼接 text
    void coalesce();

private:
    std::array<Event, kMaxEvents> m_ring;
    std::atomic<int> m_write{0};
    int m_read = 0;
    std::mutex m_mutex;
};

} // namespace CLF::CLFCore
