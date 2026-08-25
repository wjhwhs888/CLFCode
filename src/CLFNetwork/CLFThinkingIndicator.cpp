// CLFThinkingIndicator.cpp — API 等待指示器实现

#include "CLFNetwork/CLFThinkingIndicator.hpp"
#include "CLFNetwork/CLFHttpClient.hpp" // ICLFHttpClient 完整定义
#include "CLFTypes/ICLFOutput.hpp"

#include <iostream>

namespace CLF::CLFNetwork {

CLFThinkingIndicator::CLFThinkingIndicator(ICLFHttpClient* http,
                                           CLF::CLFTypes::ICLFOutput* output)
    : m_http(http)
    , m_output(output) {
    m_start = std::chrono::steady_clock::now();
}

CLFThinkingIndicator::~CLFThinkingIndicator() { stop(); }

void CLFThinkingIndicator::stop() {
    if (m_done.exchange(true, std::memory_order_relaxed)) return;  // 幂等
    if (!m_output) return;

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

} // namespace CLF::CLFNetwork
