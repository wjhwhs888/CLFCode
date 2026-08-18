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
            // StatusLine 由 turnTimer 统一管理，此处不再更新（仅计时 + abort）

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (m_output) {
            // B2: 兜底防 std::terminate。clf_network 不依赖 clf_core，不能用
            //     CLFLogger，退而用 stderr 记录（本路径极罕见，纯防御）。
            try {
                m_output->setStatus("");
            } catch (const std::exception& e) {
                std::cerr << "[ThinkingIndicator] exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[ThinkingIndicator] unknown exception" << std::endl;
            }
        }
    });
}

CLFThinkingIndicator::~CLFThinkingIndicator() { stop(); }

void CLFThinkingIndicator::stop() {
    if (!m_done.exchange(true, std::memory_order_relaxed)) {
        if (m_thread.joinable()) m_thread.join();
    }
}

} // namespace CLF::CLFNetwork
