// CLFScrollBuffer.hpp — 线程安全滚动区内容缓冲
// HTTP 流式回调线程写入 + 主线程读取/清空，内部加锁保护

#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace CLF::CLFUI {

class CLFScrollBuffer {
public:
    // 追加文本行（线程安全，HTTP 流式回调线程调用）
    void append(const std::string& text);

    // 获取缓冲区引用（调用方需持锁，仅在主线程使用）
    const std::vector<std::string>& lines() const { return m_lines; }
    size_t size() const { std::lock_guard<std::mutex> lock(m_mutex); return m_lines.size() + (m_pending.empty()?0:1); }

    // 清空缓冲 (包括未完成行)
    void clear();

    // 将未完成行推入 m_lines (读取 buffer 前调用)
    void flushPending();

    // 锁住缓冲，禁止流式线程写入（缩放重绘时使用）
    void lock()   { m_mutex.lock(); }
    void unlock() { m_mutex.unlock(); }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_lines;
    std::string m_pending;  // 跨 append 调用的未完成行累加器
};

} // namespace CLF::CLFUI
