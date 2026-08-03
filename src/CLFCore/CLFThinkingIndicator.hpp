// CLFThinkingIndicator.hpp — API 等待指示器
// 后台线程显示 "· Thinking… (Ns)" + 每秒检测 ESC 按键中断 HTTP 请求
//
// example:
//   CLFThinkingIndicator thinking(httpClient.get());
//   // ... HTTP 请求 ...
//   thinking.stop();
//   if (thinking.escPressed()) { /* 用户中断 */ }

#pragma once

#include <atomic>
#include <chrono>
#include <thread>

namespace CLF::CLFNetwork {
class ICLFHttpClient;
}

namespace CLF::CLFCore {

class CLFThinkingIndicator {
public:
    explicit CLFThinkingIndicator(CLF::CLFNetwork::ICLFHttpClient* http = nullptr);
    ~CLFThinkingIndicator();

    void stop();
    bool escPressed() const { return m_escPressed.load(std::memory_order_relaxed); }

    CLFThinkingIndicator(const CLFThinkingIndicator&) = delete;
    CLFThinkingIndicator& operator=(const CLFThinkingIndicator&) = delete;

private:
    std::atomic<bool> m_done{false};
    std::atomic<bool> m_escPressed{false};
    CLF::CLFNetwork::ICLFHttpClient* m_http;
    std::chrono::steady_clock::time_point m_start;
    std::thread m_thread;
};

} // namespace CLF::CLFCore
