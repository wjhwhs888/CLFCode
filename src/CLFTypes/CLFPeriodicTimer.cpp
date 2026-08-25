// CLFPeriodicTimer.cpp — 周期定时器实现

#include "CLFTypes/CLFPeriodicTimer.hpp"

#include <iostream>

namespace CLF::CLFTypes {

CLFPeriodicTimer::CLFPeriodicTimer(std::chrono::milliseconds interval,
                                   std::function<void()> onTick)
    : m_interval(interval)
    , m_onTick(std::move(onTick)) {
    m_thread = std::thread([this]() {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (true) {
            // 谓词为真（被 stop 唤醒）返回 true → 立即退出；
            // 超时返回 false → 说明该执行一次周期任务
            if (m_cv.wait_for(lock, m_interval, [this] { return m_stopped; })) {
                return;
            }

            // 回调期间放锁：回调可能写 UI 且耗时，持锁会让 stop() 被拖住
            lock.unlock();
            try {
                if (m_onTick) m_onTick();
            } catch (const std::exception& e) {
                // 定时器线程逸出异常会导致 std::terminate（v0.3.3 静默退出事故的
                // 根因之一）。CLFTypes 不依赖 CLFLogger，退而用 stderr。
                std::cerr << "[PeriodicTimer] exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[PeriodicTimer] unknown exception" << std::endl;
            }
            lock.lock();
        }
    });
}

CLFPeriodicTimer::~CLFPeriodicTimer() {
    stop();
}

void CLFPeriodicTimer::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
    }
    m_cv.notify_all();
    // joinable 判定天然幂等：join 过之后即为 false
    if (m_thread.joinable()) m_thread.join();
}

} // namespace CLF::CLFTypes
