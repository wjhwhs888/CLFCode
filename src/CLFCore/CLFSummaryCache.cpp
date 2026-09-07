// CLFSummaryCache.cpp — 会话摘要缓存实现（C2 拆分，逻辑自 AgentLoop 原样搬移）

#include "CLFCore/CLFSummaryCache.hpp"

namespace CLF::CLFCore {

CLFSummaryCache::CLFSummaryCache(
    const CLFAgentConfig& config,
    std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> httpClient)
    : m_config(config)
    , m_summarizer(std::make_unique<CLFSessionSummarizer>(std::move(httpClient), m_config)) {
}

void CLFSummaryCache::generate(const std::vector<CLFMessage>& messages) {
    if (!m_config.m_contextCompression) return;
    m_cachedSummary = m_summarizer->generate(messages);
}

bool CLFSummaryCache::shouldSummarize(int currentTokens) const {
    if (!m_config.m_contextCompression) return false;
    const int remaining = m_config.m_maxContextWindow - currentTokens;
    return remaining < m_config.m_autoSummaryThreshold;
}

} // namespace CLF::CLFCore
