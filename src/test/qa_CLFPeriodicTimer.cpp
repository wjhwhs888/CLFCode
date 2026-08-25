// qa_CLFPeriodicTimer.cpp — 周期定时器单元测试
// P1: 周期回调确实被执行
// P2: stop() 立即返回（本次改造的核心目的——原 sleep 轮询要空等一个间隔）
// P3: stop 幂等 / 析构自动 stop
// P4: 回调抛异常不会击穿线程（防 std::terminate）

#include <boost/ut.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include "CLFTypes/CLFPeriodicTimer.hpp"

using namespace boost::ut;
using CLF::CLFTypes::CLFPeriodicTimer;
using namespace std::chrono;

namespace {

long long elapsedMsSince(const steady_clock::time_point& t0) {
    return duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

} // anonymous namespace

const boost::ut::suite<"CLFPeriodicTimer"> tests = [] {
    // ========== P1: 周期回调 ==========

    "P1a 回调按间隔重复执行"_test = [] {
        std::atomic<int> ticks{0};
        CLFPeriodicTimer timer(milliseconds(30), [&ticks]() { ticks.fetch_add(1); });
        std::this_thread::sleep_for(milliseconds(200));
        timer.stop();
        // 200ms / 30ms ≈ 6 次，放宽下限避免调度抖动导致偶发失败
        expect(ticks.load() >= 2_i) << "实际 tick 次数: " << ticks.load();
    };

    "P1b 首次回调需等待一个间隔（不立即触发）"_test = [] {
        std::atomic<int> ticks{0};
        CLFPeriodicTimer timer(seconds(5), [&ticks]() { ticks.fetch_add(1); });
        std::this_thread::sleep_for(milliseconds(50));
        expect(ticks.load() == 0_i);
        timer.stop();
    };

    // ========== P2: stop 立即返回（核心） ==========

    // 这是本次改造的全部意义所在：原实现是 while(flag){ sleep(1s); }，
    // join 时平均空等 ~0.8s。改条件变量后 stop 应当立刻返回。
    "P2a stop 不等待剩余间隔"_test = [] {
        CLFPeriodicTimer timer(seconds(30), []() {});
        std::this_thread::sleep_for(milliseconds(20));   // 确保线程已进入等待
        const auto t0 = steady_clock::now();
        timer.stop();
        const auto cost = elapsedMsSince(t0);
        expect(cost < 200_ll) << "stop 耗时 " << cost << "ms，应远小于 30000ms 间隔";
    };

    "P2b 析构同样立即返回"_test = [] {
        const auto t0 = steady_clock::now();
        {
            CLFPeriodicTimer timer(seconds(30), []() {});
            std::this_thread::sleep_for(milliseconds(20));
        }   // 析构 → stop
        const auto cost = elapsedMsSince(t0);
        expect(cost < 300_ll) << "析构耗时 " << cost << "ms";
    };

    // ========== P3: 幂等 ==========

    "P3a stop 可重复调用"_test = [] {
        CLFPeriodicTimer timer(milliseconds(50), []() {});
        timer.stop();
        timer.stop();      // 第二次应安全返回（线程已 join）
        timer.stop();
        expect(true);      // 未崩溃即通过
    };

    "P3b stop 后析构不重复 join"_test = [] {
        {
            CLFPeriodicTimer timer(milliseconds(50), []() {});
            timer.stop();
        }   // 析构再次 stop
        expect(true);
    };

    // ========== P4: 异常防护 ==========

    // 定时器线程逸出异常会导致 std::terminate（v0.3.3 静默退出事故根因之一），
    // 回调内的异常必须被兜住
    "P4 回调抛异常不击穿线程"_test = [] {
        std::atomic<int> ticks{0};
        CLFPeriodicTimer timer(milliseconds(20), [&ticks]() {
            ticks.fetch_add(1);
            throw std::runtime_error("boom");
        });
        std::this_thread::sleep_for(milliseconds(150));
        timer.stop();
        // 抛异常后循环应继续，而非线程死掉只 tick 一次
        expect(ticks.load() >= 2_i) << "异常后 tick 次数: " << ticks.load();
    };

    "P4b 空回调不崩溃"_test = [] {
        CLFPeriodicTimer timer(milliseconds(20), nullptr);
        std::this_thread::sleep_for(milliseconds(60));
        timer.stop();
        expect(true);
    };
};

int main() {}
