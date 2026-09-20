// qa_CLFScrollView.cpp — 滚动视口组件单元测试（2026-09-20 跨视口拖选配套）
// 覆盖：滚动事件后可见区间立即重算（不等下一帧 update）——拖选期间滚轮
// 滚动后随即 hitTest 扩展选区依赖此行为；clamp 边界与滚动提示行口径

#include <boost/ut.hpp>
#include "CLFUI/CLFScrollView.hpp"

using namespace boost::ut;
using CLF::CLFUI::CLFScrollView;

static ftxui::Event wheelUp() {
    return ftxui::Event::Mouse(
        "", ftxui::Mouse{ftxui::Mouse::WheelUp, ftxui::Mouse::Pressed,
                         false, false, false, 0, 0});
}

static ftxui::Event wheelDown() {
    return ftxui::Event::Mouse(
        "", ftxui::Mouse{ftxui::Mouse::WheelDown, ftxui::Mouse::Pressed,
                         false, false, false, 0, 0});
}

// 键盘事件用 Event::Special 显式构造（不用 Event::Home 等静态常量——本套件
// 在 boost::ut 静态初始化期执行，静态 Event 常量尚未初始化、input 为空，
// == 比较不可靠；与 CLFScrollView 的 input 字符串比较语义配对）
static ftxui::Event pageUp()   { return ftxui::Event::Special("\x1B[5~"); }
static ftxui::Event homeKey()  { return ftxui::Event::Special("\x1B[H"); }
static ftxui::Event endKey()   { return ftxui::Event::Special("\x1B[F"); }

suite qa_CLFScrollView = [] {
    "W1 滚轮滚动后可见区间立即更新（不等下一帧 update）"_test = [] {
        CLFScrollView sv;
        sv.update(100, 30, 6);   // viewH=24, maxOff=76；底部窗口 [76,100)
        auto [s0, e0] = sv.visibleRange();
        expect(s0 == 76);
        expect(e0 == 100);

        // WheelUp ×1（距底偏移 +3）→ startLine = 100−24−3 = 73，立即生效
        expect(sv.handleEvent(wheelUp()) == true);
        auto [s1, e1] = sv.visibleRange();
        expect(s1 == 73);
        expect(e1 == 97);

        // WheelDown ×1（偏移 −3）→ 回 76
        expect(sv.handleEvent(wheelDown()) == true);
        auto [s2, e2] = sv.visibleRange();
        expect(s2 == 76);
        expect(e2 == 100);
    };

    "W2 滚轮 clamp：顶部不越界、底部回底自动跟随"_test = [] {
        CLFScrollView sv;
        sv.update(10, 30, 6);   // totalLines < viewH → maxOff=0
        auto [s0, e0] = sv.visibleRange();
        expect(s0 == 0);
        expect(e0 == 10);
        // WheelUp 在 maxOff=0 时 clamp 不越界
        expect(sv.handleEvent(wheelUp()) == true);
        auto [s1, e1] = sv.visibleRange();
        expect(s1 == 0);
        expect(e1 == 10);

        // 大内容滚离底部后 WheelDown 连续滚回底部（offset=0）
        CLFScrollView sv2;
        sv2.update(100, 30, 6);
        expect(sv2.handleEvent(homeKey()) == true);   // 到顶
        auto [s2, e2] = sv2.visibleRange();
        expect(s2 == 0);
        expect(sv2.topHintCount() == 1);       // 有上方内容 → 提示行
        for (int i = 0; i < 40; ++i) sv2.handleEvent(wheelDown());
        auto [s3, e3] = sv2.visibleRange();
        expect(s3 == 76);                      // 回底部
        expect(sv2.topHintCount() == 0);       // 底部无提示行
    };

    "W3 键盘翻页与 Home/End 立即更新 + clamp"_test = [] {
        CLFScrollView sv;
        sv.update(100, 30, 6);
        expect(sv.handleEvent(pageUp()) == true);
        auto [s1, e1] = sv.visibleRange();
        expect(s1 == 61);   // 76 − 15

        expect(sv.handleEvent(homeKey()) == true);
        auto [s2, e2] = sv.visibleRange();
        expect(s2 == 0);
        expect(e2 == 24);

        expect(sv.handleEvent(endKey()) == true);
        auto [s3, e3] = sv.visibleRange();
        expect(s3 == 76);
        expect(e3 == 100);
    };

    // W4（2026-09-20 记事本式边缘滚动配套）：单步滚动与内容区高度口径
    "W4 stepScroll 单步滚动 + 内容区高度（上下提示行口径）"_test = [] {
        CLFScrollView sv;
        sv.update(100, 30, 6);   // viewH=24, maxOff=76；底部窗口 [76,100)
        expect(sv.contentHeight() == 25);          // 24 可见 + 下提示 1（offset<maxOff）
        expect(sv.bottomHintCount() == 1);

        sv.stepScroll(true);                       // 上滚一步（+3）
        auto [s1, e1] = sv.visibleRange();
        expect(s1 == 73);
        expect(sv.topHintCount() == 1);            // 有上方内容
        expect(sv.bottomHintCount() == 1);         // 未到底 → 下提示
        expect(sv.contentHeight() == 26);          // 24 可见 + 上下提示各 1

        sv.stepScroll(false);                      // 下滚一步（−3）→ 回底
        auto [s2, e2] = sv.visibleRange();
        expect(s2 == 76);
        expect(sv.topHintCount() == 0);
        expect(sv.contentHeight() == 25);          // 回底：仅下提示 1

        // 顶部 clamp：连续上滚不越界
        CLFScrollView sv2;
        sv2.update(100, 30, 6);
        for (int i = 0; i < 30; ++i) sv2.stepScroll(true);   // +90 > maxOff 76
        auto [s3, e3] = sv2.visibleRange();
        expect(s3 == 0);                          // clamp 到顶
        expect(sv2.contentHeight() == 25);        // 顶部：24 + 下提示 1
    };
};

int main() {}
