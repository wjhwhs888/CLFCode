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
        expect(r.m_errorKind == "interrupted");   // G3 归一化
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
        expect(r.m_errorKind == "timeout");   // G3 归一化
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

    "C4 头尾截断（G2）：输出超限额 → 头部与尾部都在 + m_truncated + 中段省略"_test = [] {
        CLFExecSpec spec;
        // 头标记 + ~140KB 填充 + 尾标记（限额 128KB = 头尾各 64KB）
        spec.m_command =
            "powershell -NoProfile -Command \"Write-Output 'HEAD_MARKER'; "
            "1..20000 | ForEach-Object { 'filler line data padding' }; "
            "Write-Output 'TAIL_MARKER'\"";
        spec.m_maxOutputBytes = 128 * 1024;
        const CLFExecResult r = CLFProcessRunner::run(spec);
        expect(r.m_truncated);
        expect(r.m_stdout.find("HEAD_MARKER") != std::string::npos);  // 头部保留
        expect(r.m_stdout.find("TAIL_MARKER") != std::string::npos);  // 尾部结论保留
        expect(r.m_stdout.find("中间输出省略") != std::string::npos); // 中段标记
        expect(r.m_stdout.size() < 160 * 1024);   // 限额生效（128KB + 标记余量）
    };

    "C6 错误归一化（G3）：不存在的命令 → not_found"_test = [] {
        CLFExecSpec spec;
        spec.m_command = "nosuchcmd__clf_qa";
        const CLFExecResult r = CLFProcessRunner::run(spec);
        expect(r.m_exitCode != 0);
        expect(r.m_errorKind == "not_found");   // cmd"不是内部或外部命令"文案归一
        expect(!r.m_stderr.empty());            // 原始 stderr 保留（快信号非替代）
    };

    "C7 argv 模式（G4）：git --version 正常——不经 shell 的辅助命令通道"_test = [] {
        CLFExecSpec spec;
        spec.m_argv = {"git", "--version"};
        const CLFExecResult r = CLFProcessRunner::run(spec);
        expect(r.m_exitCode == 0);
        expect(r.m_stdout.find("git version") != std::string::npos);
        expect(r.m_stdout.find("chcp") == std::string::npos);  // 无 shell 包装痕迹
    };

    "C8 引号边界：含空格与尾反斜杠参数原样到达子进程（qargs 转义）"_test = [] {
        auto dir = CLFTest::CLFTestTempDir("clf_qa_proc_c8");
        {
            std::ofstream f(fs::u8path(dir.string() + "/t.txt"), std::ios::binary);
            f << "hello world path\\\n";   // 含空格 + 尾反斜杠的内容
        }
        CLFExecSpec spec;
        spec.m_cwdUtf8 = dir.string();
        // pattern 含空格与反斜杠：转义错误则被切碎 → 不匹配（exitCode 1）
        spec.m_argv = {"findstr", "/c:hello world path\\\\", "t.txt"};
        const CLFExecResult r = CLFProcessRunner::run(spec);
        expect(r.m_exitCode == 0);   // 匹配成功 = 参数原样到达
    };

    "C9 nul 回归：辅助命令 argv 路径不再产生 nul 文件（W-5 缺陷）"_test = [] {
        auto dir = CLFTest::CLFTestTempDir("clf_qa_proc_c9");
        CLFExecSpec spec;
        spec.m_cwdUtf8 = dir.string();
        spec.m_argv = {"git", "-C", dir.string(), "status", "--short"};
        const CLFExecResult r = CLFProcessRunner::run(spec);
        (void)r;
        // 旧 shell 路径 2>nul 会在 POSIX 下生成名为 nul 的文件；argv 路径
        // stderr 单独捕获——工作目录无 nul 文件。
        // Windows 下 "nul" 是保留设备名（fs::exists 抛异常/设错误码）——
        // 改目录列举检查（跨平台安全）
        std::error_code ec;
        bool foundNul = false;
        for (auto it = fs::directory_iterator(fs::u8path(dir.string()), ec);
             it != fs::directory_iterator(); ++it) {
            if (it->path().filename().string() == "nul") {
                foundNul = true;
                break;
            }
        }
        expect(!foundNul);
    };
};

int main() {}
