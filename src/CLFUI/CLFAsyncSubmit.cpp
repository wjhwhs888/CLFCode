// CLFAsyncSubmit.cpp — 异步提交线程管理实现

#include "CLFUI/CLFAsyncSubmit.hpp"

namespace CLF::CLFUI {

CLFAsyncSubmit::~CLFAsyncSubmit() {
    join();
}

void CLFAsyncSubmit::launch(std::function<void()> task) {
    join();  // 保证上一个已完成
    m_submitting = true;
    m_thread = std::thread([this, t = std::move(task)]() {
        t();
        m_submitting = false;
    });
}

void CLFAsyncSubmit::join() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

} // namespace CLF::CLFUI
