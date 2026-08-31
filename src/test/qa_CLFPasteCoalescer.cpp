// qa_CLFPasteCoalescer.cpp — 粘贴事件突发合并器单元测试（P1-P10）
// 设计：`.claude/plans/设计/归档/归档-复制粘贴功能修改.md` §2.3
// 双配置：transition=200ms 窗（纯状态机序列，真实定时器在测试期内不触发）；
//         expiry=5ms 窗（真实等待验证窗满确认路径）。

#include <boost/ut.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include "CLFUI/CLFPasteCoalescer.hpp"

using namespace boost::ut;
using CLF::CLFUI::CLFPasteCoalescer;

namespace {

using Action = CLFPasteCoalescer::Action;
using Clock  = std::chrono::steady_clock;

// 轮询等待窗满确认（真实时间）
bool waitConfirmed(CLFPasteCoalescer& c, int timeoutMs) {
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        if (c.pendingConfirmed()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return c.pendingConfirmed();
}

} // anonymous namespace

const boost::ut::suite<"CLFPasteCoalescer"> tests = [] {

    // ========== 窗满确认路径（expiry 配置，5ms 窗） ==========

    "P1 Return 后静默 → 确认提交一次"_test = [] {
        std::atomic<int> wakes{0};
        CLFPasteCoalescer c([&] { wakes++; }, /*quietWindowMs=*/5);
        std::string text = "hello";
        expect(c.onReturn(Clock::now(), text) == Action::Consume);
        expect(c.pendingText() == "hello");
        expect(!c.pendingConfirmed());  // 窗内未确认
        expect(waitConfirmed(c, 100));
        expect(c.consumePendingConfirmation() == true);
        expect(c.pendingConfirmed() == false);  // 消费后清标志
        expect(wakes.load() >= 1);              // 定时线程经 wakeCb 唤醒主循环
    };

    "P4 Return 后 10ms 其他事件（Esc）→ 取消，零提交"_test = [] {
        CLFPasteCoalescer c([] {}, 5);
        std::string text = "keep";
        expect(c.onReturn(Clock::now(), text) == Action::Consume);
        expect(c.onOtherEvent(Clock::now()) == Action::PassThrough);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        expect(!c.pendingConfirmed());  // 窗满后谓词已取消，不确认
    };

    "P5 Return 后 confirm 激活（其他事件）→ 取消，零提交"_test = [] {
        CLFPasteCoalescer c([] {}, 5);
        expect(c.onReturn(Clock::now(), std::string("x")) == Action::Consume);
        expect(c.onOtherEvent(Clock::now()) == Action::PassThrough);  // confirm 分支路由
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        expect(!c.pendingConfirmed());
    };

    "P6 两次手打 Enter（各自窗满）→ 两次独立提交"_test = [] {
        CLFPasteCoalescer c([] {}, 5);
        expect(c.onReturn(Clock::now(), std::string("a")) == Action::Consume);
        expect(waitConfirmed(c, 100));
        expect(c.consumePendingConfirmation() == true);
        expect(c.onReturn(Clock::now(), std::string("b")) == Action::Consume);
        expect(waitConfirmed(c, 100));
        expect(c.consumePendingConfirmation() == true);
    };

    "P7 单行粘贴带尾换行（前置突发）→ 窗满不提交，显式二次 Enter 提交"_test = [] {
        CLFPasteCoalescer c([] {}, 5);
        auto t0 = Clock::now();
        expect(c.onCharacter(t0) == Action::PassThrough);              // 粘贴字符突发
        expect(c.onCharacter(t0 + std::chrono::milliseconds(2))
               == Action::PassThrough);
        // 粘贴尾换行：Return 距最后字符 3ms < 40ms → 粘贴上下文
        expect(c.onReturn(t0 + std::chrono::milliseconds(3),
                          std::string("echo hi")) == Action::Consume);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        expect(!c.pendingConfirmed());                                  // 窗满不提交
        // 显式二次 Enter：距最后字符 >60ms → 手打路径 → 提交
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        expect(c.onReturn(Clock::now(), std::string("echo hi")) == Action::Consume);
        expect(waitConfirmed(c, 100));
        expect(c.consumePendingConfirmation() == true);
    };

    "P10 Return 后窗内 ArrowUp（非字符事件）→ 取消，导航正常执行"_test = [] {
        CLFPasteCoalescer c([] {}, 5);
        expect(c.onReturn(Clock::now(), std::string("draft")) == Action::Consume);
        // CLFRepl 对非字符事件的路由：onOtherEvent → PassThrough（事件继续走历史分支）
        expect(c.onOtherEvent(Clock::now()) == Action::PassThrough);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        expect(!c.pendingConfirmed());
    };

    "P3 粘贴态静默后 Enter → 退出粘贴态按真回车提交"_test = [] {
        // burst=5ms：静默 30ms > 突发窗，第二次 Enter 判为手打真提交
        CLFPasteCoalescer c([] {}, /*quietWindowMs=*/5, /*pasteBurstMs=*/5);
        auto t0 = Clock::now();
        std::string text = "x";
        expect(c.onReturn(t0, text) == Action::Consume);          // 进入 PENDING
        expect(c.onCharacter(t0 + std::chrono::milliseconds(1))
               == Action::RestoreAndAppendChar);                  // 进 PASTE_MODE
        text = c.pendingText() + "\n" + "A";
        // 静默超过窗口后回车 = 真提交
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        expect(c.onReturn(Clock::now(), text) == Action::Consume);  // 重新 PENDING
        expect(waitConfirmed(c, 100));
        expect(c.consumePendingConfirmation() == true);
        expect(text == "x\nA");
    };

    // ========== 状态机序列（transition 配置，200ms 窗） ==========

    "P2 多行粘贴突发 → 全文并入输入框，零中途提交，静默后一次提交"_test = [] {
        CLFPasteCoalescer c([] {}, 200);
        auto t0 = Clock::now();
        std::string text = "hello";
        expect(c.onReturn(t0, text) == Action::Consume);           // 第一行回车 → PENDING
        expect(c.onCharacter(t0 + std::chrono::milliseconds(1))
               == Action::RestoreAndAppendChar);                   // 突发字符 → PASTE_MODE
        text = c.pendingText() + "\n" + "a";
        expect(c.onReturn(t0 + std::chrono::milliseconds(2), text)
               == Action::InsertNewline);                          // 粘贴 "a\nb" 的换行
        text += "\n";
        expect(c.onCharacter(t0 + std::chrono::milliseconds(3))
               == Action::PassThrough);
        text += "b";
        expect(c.onReturn(t0 + std::chrono::milliseconds(4), text)
               == Action::InsertNewline);                          // 粘贴尾部换行
        text += "\n";
        expect(text == "hello\na\nb\n");
        expect(!c.pendingConfirmed());  // 零中途提交
        // 静默（超过窗宽）后回车 → 真提交
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        expect(c.onReturn(Clock::now(), text) == Action::Consume);
        expect(waitConfirmed(c, 300));
        expect(c.consumePendingConfirmation() == true);
        expect(text == "hello\na\nb\n");
    };

    "P8 粘贴含空行 a\\n\\nb\\n（连续 Return）→ 空行保留，零中途提交"_test = [] {
        CLFPasteCoalescer c([] {}, 200);
        auto t0 = Clock::now();
        std::string text = "a";
        expect(c.onReturn(t0, text) == Action::Consume);                    // Return₁ → PENDING
        expect(c.onReturn(t0 + std::chrono::milliseconds(1), text)
               == Action::RestoreAndAppendNewline);                        // Return₂（空行）
        text = c.pendingText() + "\n\n";                                    // = "a\n\n"
        expect(c.onCharacter(t0 + std::chrono::milliseconds(2))
               == Action::PassThrough);
        text += "b";
        expect(c.onReturn(t0 + std::chrono::milliseconds(3), text)
               == Action::InsertNewline);                                   // Return₃（尾换行）
        text += "\n";
        expect(text == "a\n\nb\n");
        expect(!c.pendingConfirmed());
        // 静默后回车 → 一次提交
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        expect(c.onReturn(Clock::now(), text) == Action::Consume);
        expect(waitConfirmed(c, 300));
        expect(c.consumePendingConfirmation() == true);
    };

    "P9 粘贴以空行开头 \\nfoo → 前导空行保留"_test = [] {
        CLFPasteCoalescer c([] {}, 200);
        auto t0 = Clock::now();
        std::string text;
        expect(c.onReturn(t0, text) == Action::Consume);          // 空文本 Return → PENDING
        expect(c.onCharacter(t0 + std::chrono::milliseconds(1))
               == Action::RestoreAndAppendChar);                  // 'f' → PASTE_MODE
        text = c.pendingText() + "\n" + "f";
        expect(c.onCharacter(t0 + std::chrono::milliseconds(2))
               == Action::PassThrough);
        text += "o";
        expect(c.onCharacter(t0 + std::chrono::milliseconds(3))
               == Action::PassThrough);
        text += "o";
        expect(text == "\nfoo");
        expect(!c.pendingConfirmed());
    };

    // ========== 前置突发检测（2026-08-31 自问自答 Bug：注入末尾 Return 不得自动提交） ==========

    "N1 手打回车（字符后静默 > 突发窗）→ 一次提交不回归"_test = [] {
        CLFPasteCoalescer c([] {}, 5);
        expect(c.onCharacter(Clock::now()) == Action::PassThrough);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));  // 手打间隔 > 40ms
        expect(c.onReturn(Clock::now(), std::string("hello")) == Action::Consume);
        expect(waitConfirmed(c, 100));
        expect(c.consumePendingConfirmation() == true);
    };

    "N2 粘贴末尾 Return 窗满不提交，且不唤醒主循环（零自动请求）"_test = [] {
        std::atomic<int> wakes{0};
        CLFPasteCoalescer c([&] { wakes++; }, 5);
        auto t0 = Clock::now();
        expect(c.onCharacter(t0) == Action::PassThrough);          // 注入字符突发
        expect(c.onReturn(t0 + std::chrono::milliseconds(2),
                          std::string("txt")) == Action::Consume); // 注入尾换行
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        expect(!c.pendingConfirmed());
        expect(wakes.load() == 0);  // 不提交 → wakeCb 未触发 → 无 Custom → 无 doSubmit
    };

    "N3 粘贴末尾不提交后状态复位 Idle，后续字符正常放行"_test = [] {
        CLFPasteCoalescer c([] {}, 5);
        auto t0 = Clock::now();
        expect(c.onCharacter(t0) == Action::PassThrough);
        expect(c.onReturn(t0 + std::chrono::milliseconds(1),
                          std::string("x")) == Action::Consume);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        expect(!c.pendingConfirmed());
        expect(c.onCharacter(Clock::now()) == Action::PassThrough);  // 已回 Idle
    };

    "N4 多行粘贴（前置突发）中间 Return 转 PasteMode，零中途提交"_test = [] {
        CLFPasteCoalescer c([] {}, 200);
        auto t0 = Clock::now();
        std::string text = "a";
        expect(c.onCharacter(t0) == Action::PassThrough);          // 粘贴 'a' 突发
        expect(c.onReturn(t0 + std::chrono::milliseconds(1), text)
               == Action::Consume);                                 // Return₁ 粘贴上下文
        expect(c.onCharacter(t0 + std::chrono::milliseconds(2))
               == Action::RestoreAndAppendChar);                    // 窗内字符 → PasteMode
        text = c.pendingText() + "\n" + "b";
        expect(c.onReturn(t0 + std::chrono::milliseconds(3), text)
               == Action::InsertNewline);                           // 尾换行
        text += "\n";
        expect(text == "a\nb\n");
        expect(!c.pendingConfirmed());
        // 静默后用户显式 Enter → 一次提交完整内容
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        expect(c.onReturn(Clock::now(), text) == Action::Consume);
        expect(waitConfirmed(c, 300));
        expect(c.consumePendingConfirmation() == true);
    };

    "N5 突发判定边界：恰好等于突发窗 → 粘贴上下文不提交"_test = [] {
        CLFPasteCoalescer c([] {}, 5, /*pasteBurstMs=*/10);
        auto t0 = Clock::now();
        expect(c.onCharacter(t0) == Action::PassThrough);
        expect(c.onReturn(t0 + std::chrono::milliseconds(10),
                          std::string("x")) == Action::Consume);  // 恰好 10ms（≤ 判定）
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        expect(!c.pendingConfirmed());
    };

    "N6 突发判定边界：突发窗 + 1ms → 手打提交"_test = [] {
        CLFPasteCoalescer c([] {}, 5, /*pasteBurstMs=*/10);
        auto t0 = Clock::now();
        expect(c.onCharacter(t0) == Action::PassThrough);
        expect(c.onReturn(t0 + std::chrono::milliseconds(11),
                          std::string("x")) == Action::Consume);  // 11ms > 10ms
        expect(waitConfirmed(c, 100));
        expect(c.consumePendingConfirmation() == true);
    };

    "IDLE 态普通字符与事件：PassThrough 放行"_test = [] {
        CLFPasteCoalescer c([] {}, 200);
        expect(c.onCharacter(Clock::now()) == Action::PassThrough);
        expect(c.onOtherEvent(Clock::now()) == Action::PassThrough);
    };
};

int main() {}
