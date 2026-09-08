// qa_CLFTurnUsage.cpp — 回合 usage 统计单元测试（缓存命中率显示）
// 覆盖：accumulate/reset、floor 防虚报、100% clamp、gate（零命中/零 prompt 不显示）、文案格式

#include <boost/ut.hpp>
#include "CLFTypes/CLFTurnUsage.hpp"

using namespace boost::ut;
using CLF::CLFTypes::CLFTurnUsage;

suite qa_CLFTurnUsage = [] {
    "accumulate 后 reset → displayLine 空"_test = [] {
        CLFTurnUsage u;
        u.accumulate(100, 80);
        expect(!u.displayLine().empty());
        u.reset();
        expect(u.displayLine().empty());
    };

    "基本命中率"_test = [] {
        CLFTurnUsage u;
        u.accumulate(100, 90);
        expect(u.displayLine() == "\n \n✳ 缓存命中 90%\n \n");
    };

    "全命中 → 100%"_test = [] {
        CLFTurnUsage u;
        u.accumulate(100, 100);
        expect(u.displayLine() == "\n \n✳ 缓存命中 100%\n \n");
    };

    "hit=prompt-1 → floor 非 100（防虚报边界）"_test = [] {
        CLFTurnUsage u;
        u.accumulate(100, 99);
        expect(u.displayLine() == "\n \n✳ 缓存命中 99%\n \n");
    };

    "异常数据 cacheHit > prompt → clamp 100%"_test = [] {
        CLFTurnUsage u;
        u.accumulate(100, 150);
        expect(u.displayLine() == "\n \n✳ 缓存命中 100%\n \n");
    };

    "gate：零命中 / 零 prompt 不显示"_test = [] {
        CLFTurnUsage zeroHit;
        zeroHit.accumulate(100, 0);
        expect(zeroHit.displayLine().empty());

        CLFTurnUsage zeroPrompt;
        zeroPrompt.accumulate(0, 0);
        expect(zeroPrompt.displayLine().empty());
    };

    "多笔累计 Σ 口径"_test = [] {
        CLFTurnUsage u;
        u.accumulate(100, 50);
        u.accumulate(100, 100);
        // Σ: prompt 200 / hit 150 → floor 75%
        expect(u.displayLine() == "\n \n✳ 缓存命中 75%\n \n");
    };
};

int main() {}
