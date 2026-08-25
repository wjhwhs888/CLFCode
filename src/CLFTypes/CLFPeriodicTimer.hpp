// CLFPeriodicTimer.hpp — 可即时中断的周期定时器
// 后台线程按固定间隔执行回调；stop() 用条件变量唤醒，**立即返回**，
// 不必等当前间隔走完。
//
// 背景：原先三处后台线程都写成 `while(flag) { sleep(1s); work(); }`，
// join 时平均要空等 ~0.8s。qa_CLFAgentLoop 12 个用例因此耗时 28-29 秒。
//
// example:
//   CLFPeriodicTimer timer(std::chrono::seconds(1), [&]{ tick(); });
//   ...
//   timer.stop();   // 立即返回

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace CLF::CLFTypes {

class CLFPeriodicTimer {
public:
    // 构造即启动线程；onTick 每隔 interval 执行一次（首次也需等待一个 interval）
    CLFPeriodicTimer(std::chrono::milliseconds interval, std::function<void()> onTick);

    // 析构自动 stop（RAII）
    ~CLFPeriodicTimer();

    //停止并汇合线程（幂等，可重复调用）
    // example:
    //   timer.stop();
    void stop();

    // 持有线程与回调，不可拷贝/移动
    CLFPeriodicTimer(const CLFPeriodicTimer&)            = delete;
    CLFPeriodicTimer& operator=(const CLFPeriodicTimer&) = delete;
    CLFPeriodicTimer(CLFPeriodicTimer&&)                 = delete;
    CLFPeriodicTimer& operator=(CLFPeriodicTimer&&)      = delete;

private:
    std::chrono::milliseconds m_interval;
    std::function<void()>     m_onTick;
    std::mutex                m_mutex;
    std::condition_variable   m_cv;
    bool                      m_stopped = false;   // 受 m_mutex 保护
    std::thread               m_thread;
};

} // namespace CLF::CLFTypes
