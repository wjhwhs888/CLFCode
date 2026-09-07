// CLFSummaryCache.hpp — 会话摘要缓存（C2：AgentLoop 拆角色，2026-09-07）
// 持有摘要生成器（CLFSessionSummarizer）+ 缓存 + S3-1 频控轮计数。
// 不落盘（summary 行追加由调用方编排至 CLFSessionFileCtx）、不注入上下文
// （rebuildSystemMessage 编排留 AgentLoop）。
//
// example:
//   CLFSummaryCache cache(config, httpClient);
//   cache.generate(messages);                       // 生成并缓存（失败自动规则降级）
//   if (cache.cached().m_valid) use(cache.cached());

#pragma once

#include <memory>
#include <vector>

#include "CLFTypes/CLFTypes.hpp"
#include "CLFCore/CLFSessionSummarizer.hpp"

namespace CLF::CLFNetwork { class ICLFHttpClient; }
namespace CLF::CLFCore {

class CLFSummaryCache {
public:
    // config 只读引用（调用方保证生命周期）；httpClient 注入生成器
    CLFSummaryCache(const CLFAgentConfig& config,
                    std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> httpClient);

    // 生成摘要并缓存（开关关时 no-op 保持缓存空；API 失败自动规则降级）
    void generate(const std::vector<CLFMessage>& messages);

    // 最近一次生成的摘要（/exit 生成、saveSession 消费；无效 = m_valid false）
    const CLFSessionSummary& cached() const { return m_cachedSummary; }

    // S3-1 频控：距上次自动摘要的轮数（冷却阈值由编排层持有）
    int turnsSinceLast() const { return m_turnsSinceSummary; }
    void noteTurn() { ++m_turnsSinceSummary; }
    void resetCooldown() { m_turnsSinceSummary = 0; }

    // S3-1 触发判定：开关开启且剩余窗口 < 阈值（currentTokens = 当前上下文
    // 全量 token 估算，由调用方传入）
    bool shouldSummarize(int currentTokens) const;

private:
    const CLFAgentConfig&            m_config;      // 只读引用（AgentLoop 持有）
    std::unique_ptr<CLFSessionSummarizer> m_summarizer;
    CLFSessionSummary                m_cachedSummary;
    int                              m_turnsSinceSummary = 0;
};

} // namespace CLF::CLFCore
