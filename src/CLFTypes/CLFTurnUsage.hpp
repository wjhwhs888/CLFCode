// CLFTurnUsage.hpp — 回合 usage 统计（缓存命中率显示的累计与文案）
// 纯值逻辑类：reset / accumulate / displayLine
// 累计口径：主循环内正常解析轮（R3 gate）累计；runTurn 入口 reset
//
// example:
//CLF::CLFTypes::CLFTurnUsage usage;
//usage.accumulate(100, 90);          // prompt=100, cacheHit=90
//std::string line = usage.displayLine();  // "\n \n✳ 缓存命中 90%\n \n"

#pragma once

#include <string>

namespace CLF::CLFTypes {

class CLFTurnUsage {
public:
    void reset() { m_promptTokens = 0; m_cacheHitTokens = 0; }

    void accumulate(int promptTokens, int cacheHitTokens) {
        m_promptTokens += promptTokens;
        m_cacheHitTokens += cacheHitTokens;
    }

    // 回合收尾统计行（设计-缓存命中率显示 §3.3）
    // gate：prompt>0 且 cacheHit>0 才返回非空（无 usage 数据/零命中不宣称 0%）
    // 防虚报：cacheHit>=prompt → 100%，否则 floor 永不四舍五入
    // 注：文案/块状分隔为 MVP 形态，UI 布局变化（行内/状态栏）时抽离至显示层
    std::string displayLine() const {
        if (m_promptTokens <= 0 || m_cacheHitTokens <= 0) return {};
        const int percent = m_cacheHitTokens >= m_promptTokens
                          ? 100
                          : m_cacheHitTokens * 100 / m_promptTokens;
        return "\n \n✳ 缓存命中 " + std::to_string(percent) + "%\n \n";
    }

private:
    int m_promptTokens = 0;
    int m_cacheHitTokens = 0;
};

} // namespace CLF::CLFTypes
