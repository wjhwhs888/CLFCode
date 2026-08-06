// CLFAsyncSubmit.hpp — 异步提交线程管理
// 保证同一时间只有一个提交线程在运行（join-before-respawn）

#pragma once

#include <atomic>
#include <functional>
#include <thread>

namespace CLF::CLFUI {

class CLFAsyncSubmit {
public:
    ~CLFAsyncSubmit();

    // 启动异步任务（如果上一个还在运行则 join 等待）
    void launch(std::function<void()> task);

    // 等待当前任务完成
    void join();

    // 是否有任务正在运行
    bool busy() const { return m_submitting; }

private:
    std::thread       m_thread;
    std::atomic<bool> m_submitting{false};
};

} // namespace CLF::CLFUI
