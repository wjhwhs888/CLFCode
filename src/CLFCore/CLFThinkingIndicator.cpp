// CLFThinkingIndicator.cpp — API 等待指示器实现

#include "CLFCore/CLFThinkingIndicator.hpp"
#include "CLFNetwork/CLFHttpClient.hpp" // ICLFHttpClient 完整定义（调用 abort()）
#include "CLFCore/CLFConsole.hpp"
#include "CLFCore/CLFTerminal.hpp"

#include <iostream>

namespace CLF::CLFCore {

CLFThinkingIndicator::CLFThinkingIndicator(CLF::CLFNetwork::ICLFHttpClient* http)
    : m_http(http) {
    m_start = std::chrono::steady_clock::now();
    m_thread = std::thread([this]() {
        while (!m_done.load(std::memory_order_relaxed)) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - m_start).count();
            // 更新固定状态区 (而非直接写 stdout)
            CLFTerminal::showThinking(static_cast<int>(elapsed));

            if (m_http && CLFConsole::checkEscape()) {
                m_escPressed.store(true, std::memory_order_relaxed);
                m_http->abort();
                break;
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        CLFTerminal::clearStatus();
    });
}

CLFThinkingIndicator::~CLFThinkingIndicator() { stop(); }

void CLFThinkingIndicator::stop() {
    if (!m_done.exchange(true, std::memory_order_relaxed)) {
        if (m_thread.joinable()) m_thread.join();
    }
}

} // namespace CLF::CLFCore
