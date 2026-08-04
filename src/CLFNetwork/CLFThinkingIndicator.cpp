// CLFThinkingIndicator.cpp — API 等待指示器实现

#include "CLFNetwork/CLFThinkingIndicator.hpp"
#include "CLFNetwork/CLFHttpClient.hpp" // ICLFHttpClient 完整定义（调用 abort()）
#include "CLFTypes/ICLFOutput.hpp"

#include <iostream>

namespace CLF::CLFNetwork {

CLFThinkingIndicator::CLFThinkingIndicator(ICLFHttpClient* http, CLF::CLFTypes::ICLFOutput* output)
    : m_http(http)
    , m_output(output) {
    m_start = std::chrono::steady_clock::now();
    m_thread = std::thread([this]() {
        while (!m_done.load(std::memory_order_relaxed)) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - m_start).count();
            if (m_output)
                m_output->setStatus("· Thinking… (" + std::to_string(static_cast<int>(elapsed)) + "s)");

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (m_output) m_output->setStatus("");
    });
}

CLFThinkingIndicator::~CLFThinkingIndicator() { stop(); }

void CLFThinkingIndicator::stop() {
    if (!m_done.exchange(true, std::memory_order_relaxed)) {
        if (m_thread.joinable()) m_thread.join();
    }
}

} // namespace CLF::CLFNetwork
