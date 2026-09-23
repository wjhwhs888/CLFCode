// qa_CLFProcessRunner.cpp — 命令执行器测试（设计-命令执行层 §十二，2026-09-23）
// C1 基线 / C2 取消时效 / C3 取消杀整棵进程树（孙进程心跳探针）/
// C4 超时杀树 / C5 无取消回调兼容。
//
// 测试基建注意（项目既有教训）：qa 运行在 boost::ut 静态初始化期——
// 探针不得依赖静态初始化顺序；临时文件用 CLFTestTempDir RAII。
// 取消模拟用"轮询计数"方案（第 N 次轮询后返回 true ≈ 50ms×N 后取消）——
// 无线程、无静态对象依赖。心跳命令的 cwd 用临时目录相对路径（TEMP 路径
// 含空格的引号转义问题由相对路径规避）。

#include <boost/ut.hpp>
#include "CLFTestTempDir.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "CLFTypes/CLFProcessRunner.hpp"

using namespace boost::ut;
namespace fs = std::filesystem;

using CLF::CLFCore::CLFExecSpec;
using CLF::CLFCore::CLFExecResult;
using CLF::CLFCore::CLFProcessRunner;

namespace {

// 心跳命令：powershell（孙进程）每 200ms 向 cwd 内 heart.txt 追加一行
constexpr const char* kHeartbeatFile = "heart.txt";
constexpr const char* kHeartbeatCmd =
    "powershell -NoProfile -Command \"1..1000 | ForEach-Object { "
    "Add-Content -Path heart.txt -Value tick; Start-Sleep -Milliseconds 200 }\"";

size_t readHeartbeatSize(const std::string& dir) {
    std::ifstream f(fs::u8path(dir + "/" + kHeartbeatFile),
                    std::ios::binary | std::ios::ate);
    return f.is_open() ? static_cast<size_t>(f.tellg()) : 0;
}

} // anonymous namespace

const boost::ut::suite<"CLFProcessRunner"> tests = [] {

    "C1 基线短命令正常完成"_test = [] {
        CLFExecSpec spec;
        spec.m_command = "echo hello";
        const CLFExecResult r = CLFProcessRunner::run(spec);
        expect(r.m_exitCode == 0);
        expect(!r.m_timedOut);
        expect(!r.m_interrupted);
        expect(!r.m_launchFailed);
        expect(r.m_stdout.find("hello") != std::string::npos);
    };

    "C2 取消时效——长睡眠命令 ≤2 秒返回 + m_interrupted"_test = [] {
        CLFExecSpec spec;
        spec.m_command = "powershell -NoProfile -Command \"Start-Sleep 30\"";
        spec.m_timeoutSec = 30;
        int polls = 0;  // 第 11 次轮询（≈550ms）后取消——模拟"执行中途按 ESC"
        const auto t0 = std::chrono::steady_clock::now();
        const CLFExecResult r = CLFProcessRunner::run(
            spec, [&polls] { return ++polls > 10; });
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        expect(r.m_interrupted);
        expect(r.m_exitCode != 0);
        expect(elapsedMs < 2000);
    };

    "C3 取消杀整棵进程树——孙进程心跳停止"_test = [] {
        auto dir = CLFTest::CLFTestTempDir("clf_qa_proc_c3");
        CLFExecSpec spec;
        spec.m_command = kHeartbeatCmd;   // cmd → powershell 两级进程树
        spec.m_cwdUtf8 = dir.string();
        spec.m_timeoutSec = 60;
        int polls = 0;
        const CLFExecResult r = CLFProcessRunner::run(
            spec, [&polls] { return ++polls > 20; });   // ≈1s 后取消
        expect(r.m_interrupted);
        // 取消后两次采样：心跳不再更新 = 孙进程已随树被杀（无孤儿）
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const size_t s1 = readHeartbeatSize(dir.string());
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const size_t s2 = readHeartbeatSize(dir.string());
        expect(s1 == s2);
    };

    "C4 超时杀整棵进程树——无残留（回归 W-2 只杀父缺陷）"_test = [] {
        auto dir = CLFTest::CLFTestTempDir("clf_qa_proc_c4");
        CLFExecSpec spec;
        spec.m_command = kHeartbeatCmd;
        spec.m_cwdUtf8 = dir.string();
        spec.m_timeoutSec = 1;
        const CLFExecResult r = CLFProcessRunner::run(spec);
        expect(r.m_timedOut);
        expect(r.m_exitCode == -1);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const size_t s1 = readHeartbeatSize(dir.string());
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const size_t s2 = readHeartbeatSize(dir.string());
        expect(s1 == s2);
    };

    "C5 isCancelled 为空——行为与改造前一致"_test = [] {
        CLFExecSpec spec;
        spec.m_command = "echo compat";
        const CLFExecResult r = CLFProcessRunner::run(spec);  // 不传取消回调
        expect(r.m_exitCode == 0);
        expect(!r.m_interrupted);
        expect(r.m_stdout.find("compat") != std::string::npos);
    };
};

int main() {}
