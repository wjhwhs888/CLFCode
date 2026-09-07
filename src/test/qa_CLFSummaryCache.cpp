// qa_CLFSummaryCache.cpp — 会话摘要缓存测试（C2，2026-09-07）
// C1-C4: 开关 no-op / 触发判定 / 频控轮计数
// generate 的 API 路径由 qa_CLFAgentLoop W 系列覆盖（此处仅测开关关的 no-op）

#include <boost/ut.hpp>

#include <memory>
#include <string>
#include <vector>

#include "CLFCore/CLFSummaryCache.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFAgentConfig;
using CLF::CLFCore::CLFSummaryCache;

namespace {

CLFAgentConfig makeConfig(bool compression, int maxWindow = 1000, int threshold = 100) {
    CLFAgentConfig config;
    config.m_contextCompression = compression;
    config.m_maxContextWindow = maxWindow;
    config.m_autoSummaryThreshold = threshold;
    return config;
}

} // anonymous namespace

const boost::ut::suite<"CLFSummaryCache"> tests = [] {
    "C1 开关关：generate no-op（缓存保持无效）"_test = [] {
        CLFAgentConfig config = makeConfig(false);
        CLFSummaryCache cache(config, nullptr);
        expect(!cache.cached().m_valid);
        cache.generate({});
        expect(!cache.cached().m_valid);   // 开关关不生成
    };

    "C2 开关关：shouldSummarize 恒 false"_test = [] {
        CLFSummaryCache cache(makeConfig(false), nullptr);
        expect(!cache.shouldSummarize(0));
        expect(!cache.shouldSummarize(9999));
    };

    "C3 开关开：剩余窗口阈值判定"_test = [] {
        // 窗口 1000、阈值 100：remaining = 1000 - tokens，remaining < 阈值触发
        CLFSummaryCache cache(makeConfig(true, 1000, 100), nullptr);
        expect(cache.shouldSummarize(950));   // remaining 50 < 100 → true
        expect(!cache.shouldSummarize(850));  // remaining 150 → false
        expect(cache.shouldSummarize(1000));  // remaining 0 < 100 → true（窗口满必须摘要）
        expect(cache.shouldSummarize(1001));  // remaining -1 → true
    };

    "C4 频控：noteTurn 递增 + resetCooldown 清零"_test = [] {
        CLFSummaryCache cache(makeConfig(false), nullptr);
        expect(cache.turnsSinceLast() == 0);
        cache.noteTurn();
        cache.noteTurn();
        expect(cache.turnsSinceLast() == 2);
        cache.resetCooldown();
        expect(cache.turnsSinceLast() == 0);
    };
};

int main() {}
