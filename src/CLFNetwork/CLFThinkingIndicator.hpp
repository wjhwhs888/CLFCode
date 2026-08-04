// CLFThinkingIndicator.hpp — API 等待指示器
// 后台线程通过 ICLFOutput::setStatus 显示 "· Thinking… (Ns)"
// ESC 中断由 AgentLoop 通过 onInterrupt 回调处理, 此处不再检测

#pragma once

#include <atomic>
#include <chrono>
#include <thread>

namespace CLF::CLFNetwork { class ICLFHttpClient; }
namespace CLF::CLFTypes { class ICLFOutput; }

namespace CLF::CLFNetwork {

class CLFThinkingIndicator {
public:
    CLFThinkingIndicator(CLF::CLFNetwork::ICLFHttpClient* http = nullptr,
                         CLF::CLFTypes::ICLFOutput* output = nullptr);
    ~CLFThinkingIndicator();

    void stop();

    CLFThinkingIndicator(const CLFThinkingIndicator&) = delete;
    CLFThinkingIndicator& operator=(const CLFThinkingIndicator&) = delete;

private:
    std::atomic<bool> m_done{false};
    CLF::CLFNetwork::ICLFHttpClient* m_http;
    CLF::CLFTypes::ICLFOutput* m_output = nullptr;
    std::chrono::steady_clock::time_point m_start;
    std::thread m_thread;
};

} // namespace CLF::CLFNetwork
