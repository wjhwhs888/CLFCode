// qa_CLFSessionUsage.cpp — 会话 usage 统计单元测试（缓存命中率显示）
// 覆盖：accumulate、floor 防虚报、100% clamp、gate（零命中/零 prompt 不显示）

#include <boost/ut.hpp>
#include "CLFTypes/CLFSessionUsage.hpp"

using namespace boost::ut;
using CLF::CLFTypes::CLFSessionUsage;

suite qa_CLFSessionUsage = [] {
    "基本命中率"_test = [] {
        CLFSessionUsage u;
        u.accumulate(100, 90);
        expect(u.percentText() == "90");
    };

    "全命中 → 100"_test = [] {
        CLFSessionUsage u;
        u.accumulate(100, 100);
        expect(u.percentText() == "100");
    };

    "hit=prompt-1 → floor 非 100（防虚报边界）"_test = [] {
        CLFSessionUsage u;
        u.accumulate(100, 99);
        expect(u.percentText() == "99");
    };

    "异常数据 cacheHit > prompt → clamp 100"_test = [] {
        CLFSessionUsage u;
        u.accumulate(100, 150);
        expect(u.percentText() == "100");
    };

    "gate：零命中 / 零 prompt 不显示"_test = [] {
        CLFSessionUsage zeroHit;
        zeroHit.accumulate(100, 0);
        expect(zeroHit.percentText().empty());

        CLFSessionUsage zeroPrompt;
        zeroPrompt.accumulate(0, 0);
        expect(zeroPrompt.percentText().empty());
    };

    "多笔累计 Σ 口径"_test = [] {
        CLFSessionUsage u;
        u.accumulate(100, 50);
        u.accumulate(100, 100);
        // Σ: prompt 200 / hit 150 → floor 75
        expect(u.percentText() == "75");
    };
};

int main() {}
