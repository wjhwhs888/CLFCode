// CLFSessionUsage.hpp — 会话 usage 统计（缓存命中率的累计与文案）
// 纯值逻辑类：accumulate / percentText
// 累计口径：主循环内正常解析轮（R3 gate）累计；生命周期与 m_totalTokensUsed 一致
//（不随回合/会话重置——"本次运行累计"，与 /context 的"本次会话累计"同口径）
//
// example:
//CLF::CLFTypes::CLFSessionUsage usage;
//usage.accumulate(100, 90);            // prompt=100, cacheHit=90
//std::string p = usage.percentText();  // "90"

#pragma once

#include <string>

namespace CLF::CLFTypes {

class CLFSessionUsage {
public:
    void accumulate(int promptTokens, int cacheHitTokens) {
        m_promptTokens += promptTokens;
        m_cacheHitTokens += cacheHitTokens;
    }

    // 命中率百分比文本（底部常亮参数行显示）
    // gate：prompt>0 且 cacheHit>0 才返回非空（无 usage 数据/零命中不显示，不宣称 0%）
    // 防虚报：cacheHit>=prompt → 100，否则 floor 永不四舍五入
    std::string percentText() const {
        if (m_promptTokens <= 0 || m_cacheHitTokens <= 0) return {};
        const int percent = m_cacheHitTokens >= m_promptTokens
                          ? 100
                          : m_cacheHitTokens * 100 / m_promptTokens;
        return std::to_string(percent);
    }

private:
    int m_promptTokens = 0;
    int m_cacheHitTokens = 0;
};

} // namespace CLF::CLFTypes
