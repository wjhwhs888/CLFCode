// CLFScrollBuffer.cpp — 线程安全滚动区内容缓冲实现

#include "CLFCore/CLFScrollBuffer.hpp"

namespace CLF::CLFCore {

void CLFScrollBuffer::append(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string line;
    for (char c : text) {
        if (c == '\n') {
            m_lines.push_back(line);
            line.clear();
        } else {
            line += c;
        }
    }
    if (!line.empty()) {
        m_lines.push_back(line);
    }
}

void CLFScrollBuffer::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lines.clear();
}

} // namespace CLF::CLFCore
