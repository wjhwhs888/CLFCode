// CLFEventQueue.cpp — 事件队列实现

#include "CLFTypes/CLFEventQueue.hpp"

namespace CLF::CLFCore {

void CLFEventQueue::push(const Event& e) {
    std::lock_guard<std::mutex> lock(m_mutex);
    int w = m_write.load(std::memory_order_relaxed);
    int next = (w + 1) % kMaxEvents;
    if (next == m_read) {
        // 队列满: 覆盖旧事件 (除了 ContentAppend 合并)
        if (e.type == EventType::ContentAppend
            && m_ring[(w - 1 + kMaxEvents) % kMaxEvents].type == EventType::ContentAppend) {
            m_ring[(w - 1 + kMaxEvents) % kMaxEvents].text += e.text;
            return;
        }
        m_read = (m_read + 1) % kMaxEvents; // 丢弃最旧
    }
    m_ring[w] = e;
    m_write.store(next, std::memory_order_release);
}

Event CLFEventQueue::pop() {
    int r = m_read;
    m_read = (r + 1) % kMaxEvents;
    return m_ring[r];
}

void CLFEventQueue::coalesce() {
    // 合并连续 ContentAppend 事件（减少渲染次数）
    int w = m_write.load(std::memory_order_acquire);
    int pos = m_read;
    while (pos != w) {
        int next = (pos + 1) % kMaxEvents;
        if (next != w
            && m_ring[pos].type == EventType::ContentAppend
            && m_ring[next].type == EventType::ContentAppend) {
            m_ring[pos].text += m_ring[next].text;
            // 移除 next: 后续元素前移
            int cur = next;
            while (cur != w) {
                int nxt = (cur + 1) % kMaxEvents;
                m_ring[cur] = m_ring[nxt];
                cur = nxt;
            }
            w = (w - 1 + kMaxEvents) % kMaxEvents;
            m_write.store(w, std::memory_order_release);
        }
        pos = (pos + 1) % kMaxEvents;
    }
}

} // namespace CLF::CLFCore
