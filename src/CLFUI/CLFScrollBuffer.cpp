// CLFScrollBuffer.cpp — 线程安全滚动区内容缓冲实现

#include "CLFUI/CLFScrollBuffer.hpp"

namespace CLF::CLFUI {

void CLFScrollBuffer::append(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (char c : text) {
        if (c == '\n') {
            m_lines.push_back(m_pending);
            m_pending.clear();
        } else {
            m_pending += c;
        }
    }
}

void CLFScrollBuffer::flushPending() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pending.empty()) {
        m_lines.push_back(m_pending);
        m_pending.clear();
    }
}

void CLFScrollBuffer::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lines.clear();
    m_pending.clear();
}

} // namespace CLF::CLFUI
