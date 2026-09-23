// CLFProcessRunner.cpp — 命令执行器实现（设计-命令执行层，2026-09-23）
// 第一波 G1：shell 模式等价搬移（自 CLFCommandExec 原逻辑保真）+ 取消检查 +
// 杀树（Job Object / 进程组，超时路径同改——原超时只杀父进程，孙进程成孤儿，
// 与项目既有孤儿进程血案同源）。
//
// Windows：CreateProcessW + CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
// CREATE_NO_WINDOW；CREATE_SUSPENDED 的作用 = resume 前纳管作业——关闭
// "子进程先于纳管派生孙进程"的竞态（dsh 同款，命令执行层 §七.2）。
// Job 任一步失败 → 静默回退旧行为（TerminateProcess 只杀父）+ cerr warn
// （分层约束：clf_types 不依赖 clf_core 的 CLFLogger，warn 走 cerr——
// 与 clf_network 定时器线程兜底同款先例）。
//
// POSIX：[未验证]——非 Windows 构建未覆盖。同步改造（setsid 独立进程组 +
// 取消检查 + kill(-pgid) 整组 + 补 <signal.h>）；输出重定向 /tmp 保持现状
// （管道直读属命令执行层 §八 第二期）。

#include "CLFTypes/CLFProcessRunner.hpp"
#include "CLFTypes/CLFEncoding.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>  // [未验证] SIGKILL（平台层收敛 E 类缺陷补齐——原缺显式包含）
#endif

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace CLF::CLFCore {

namespace {

// 读取文件全部内容到字符串（POSIX /tmp 重定向路径用）
std::string readFileContent(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

#ifdef _WIN32
// UTF-8 → UTF-16（CreateProcessW 命令行/工作目录；本实现私有）
std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                      static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}
#endif

} // anonymous namespace

CLFExecResult CLFProcessRunner::run(const CLFExecSpec& spec,
                                    const std::function<bool()>& isCancelled) {
    CLFExecResult result;

    // 超时 clamp：请求值 → [1, 硬顶]（安全网；配置上限 min 在 handler 层做）
    int timeoutSeconds = spec.m_timeoutSec;
    if (timeoutSeconds < 1) timeoutSeconds = 1;
    if (timeoutSeconds > kHardTimeoutCapSec) timeoutSeconds = kHardTimeoutCapSec;

#ifdef _WIN32
    // ==== Windows：CreateProcessW + 匿名管道 + Job Object（可靠捕获 + 杀树）====

    HANDLE hOutRead = nullptr, hOutWrite = nullptr;
    HANDLE hErrRead = nullptr, hErrWrite = nullptr;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    if (!CreatePipe(&hOutRead, &hOutWrite, &sa, 0)
        || !CreatePipe(&hErrRead, &hErrWrite, &sa, 0)) {
        result.m_launchFailed = true;
        result.m_exitCode = -1;
        result.m_stderr = "Failed to create pipes";
        return result;
    }
    SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hErrRead, HANDLE_FLAG_INHERIT, 0);

    // 匹配 std::system 行为：cmd.exe /s /c "..."；chcp 65001 源头 UTF-8 化
    // （2.2b 修根：中文 Windows 命令输出 GBK 与 UTF-8 混合 → 单次 CP_ACP
    // 转换不可靠；chcp 输出重定向 nul 不留痕、& 保证原命令照常执行）
    const std::string wrapped = "chcp 65001 >nul & " + spec.m_command;
    std::wstring cmdLine = L"cmd.exe /s /c \"" + utf8ToWide(wrapped) + L"\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hOutWrite;
    si.hStdError  = hErrWrite;
    si.hStdInput  = nullptr;  // 无交互式命令支持（B11：Ctrl+C 不会误杀子进程）

    const std::wstring cwdWide =
        spec.m_cwdUtf8.empty() ? std::wstring() : utf8ToWide(spec.m_cwdUtf8);

    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr,
                        TRUE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW
                            | CREATE_UNICODE_ENVIRONMENT,
                        nullptr,
                        cwdWide.empty() ? nullptr : cwdWide.c_str(),
                        &si, &pi)) {
        const DWORD err = GetLastError();
        CloseHandle(hOutRead); CloseHandle(hOutWrite);
        CloseHandle(hErrRead); CloseHandle(hErrWrite);
        result.m_launchFailed = true;
        result.m_exitCode = -1;
        result.m_stderr = "Failed to create process: error " + std::to_string(err);
        return result;
    }

    CloseHandle(hOutWrite);
    CloseHandle(hErrWrite);

    // Job Object 纳管（杀树关键）：cmd.exe /s /c 会派生孙进程子树，
    // TerminateProcess(pi.hProcess) 只杀 cmd → 孙进程成孤儿
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    bool jobOk = false;
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                    &jeli, sizeof(jeli))
            && AssignProcessToJobObject(job, pi.hProcess)) {
            jobOk = true;
        } else {
            CloseHandle(job);
            job = nullptr;
        }
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    // 终止路径唯一实现（G1）：取消与超时共用——杀整棵树；Job 失效 → 回退
    // 只杀父（旧行为）+ cerr warn（分层约束：clf_types 无 CLFLogger 依赖）
    auto terminateTree = [&]() {
        if (jobOk && TerminateJobObject(job, 1)) return;
        TerminateProcess(pi.hProcess, 1);
        std::cerr << "[ProcessRunner] warn: job terminate unavailable, "
                     "fallback TerminateProcess (orphan risk)" << std::endl;
    };

    // 轮询读取管道 + 取消/超时检测（WaitForSingleObject 50ms 即取消检查粒度）
    std::string outBuf, errBuf;
    char buf[4096];
    DWORD available, bytesRead;
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::seconds(timeoutSeconds);

    while (true) {
        // 取消检查（G1）：命中 → 杀树 + m_interrupted（与 m_timedOut 互斥）
        if (isCancelled && isCancelled()) {
            terminateTree();
            result.m_interrupted = true;
            result.m_exitCode = -1;
            break;
        }

        const DWORD waitResult = WaitForSingleObject(pi.hProcess, 50);

        // 读取 stdout
        while (PeekNamedPipe(hOutRead, nullptr, 0, nullptr, &available, nullptr)
               && available > 0) {
            const DWORD toRead = (available > sizeof(buf) - 1)
                               ? sizeof(buf) - 1 : available;
            if (ReadFile(hOutRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0) {
                outBuf.append(buf, bytesRead);
            }
        }
        // 读取 stderr
        while (PeekNamedPipe(hErrRead, nullptr, 0, nullptr, &available, nullptr)
               && available > 0) {
            const DWORD toRead = (available > sizeof(buf) - 1)
                               ? sizeof(buf) - 1 : available;
            if (ReadFile(hErrRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0) {
                errBuf.append(buf, bytesRead);
            }
        }

        if (waitResult == WAIT_OBJECT_0) break;  // 进程正常退出

        if (std::chrono::steady_clock::now() >= deadline) {
            terminateTree();  // 超时路径同改：原只杀父（W-2 缺陷）
            result.m_timedOut = true;
            result.m_exitCode = -1;
            break;
        }
    }

    // 进程退出后清空管道残余
    while (PeekNamedPipe(hOutRead, nullptr, 0, nullptr, &available, nullptr)
           && available > 0) {
        const DWORD toRead = (available > sizeof(buf) - 1) ? sizeof(buf) - 1 : available;
        if (ReadFile(hOutRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0)
            outBuf.append(buf, bytesRead);
    }
    while (PeekNamedPipe(hErrRead, nullptr, 0, nullptr, &available, nullptr)
           && available > 0) {
        const DWORD toRead = (available > sizeof(buf) - 1) ? sizeof(buf) - 1 : available;
        if (ReadFile(hErrRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0)
            errBuf.append(buf, bytesRead);
    }

    if (result.m_timedOut) {
        result.m_stderr = "Timed out after " + std::to_string(timeoutSeconds) + "s";
    } else if (!result.m_interrupted) {
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        result.m_exitCode = static_cast<int>(exitCode);
    }

    CloseHandle(pi.hProcess);
    if (job) CloseHandle(job);  // KILL_ON_JOB_CLOSE 兜底（句柄关闭即杀残余）
    CloseHandle(hOutRead);
    CloseHandle(hErrRead);

    result.m_stdout = CLFEncoding::toUtf8(outBuf);
    if (result.m_stderr.empty()) {
        result.m_stderr = CLFEncoding::toUtf8(errBuf);
    }

#else
    // ==== POSIX：fork + setsid + sh -c（[未验证]：非 Windows 构建未覆盖）====
    // A 批同步改造：独立进程组 + 取消检查 + kill(-pgid) 整组（原超时只
    // kill(child)——孙进程成孤儿，既有缺陷同修）。输出重定向 /tmp 保持
    // 现状（管道直读属命令执行层 §八 第二期）
    const std::string pidStr = std::to_string(static_cast<long long>(getpid()));
    const std::string stdoutFile = "/tmp/clf_cmd_stdout_" + pidStr + ".txt";
    const std::string stderrFile = "/tmp/clf_cmd_stderr_" + pidStr + ".txt";
    const std::string cmdWithRedirect =
        spec.m_command + " > " + stdoutFile + " 2> " + stderrFile;

    const pid_t child = fork();
    if (child == 0) {
        // 子进程：切到指定工作目录后通过 sh 执行命令
        (void)setsid();  // [未验证] 独立进程组——kill(-pgid) 杀树的基础
        if (!spec.m_cwdUtf8.empty() && chdir(spec.m_cwdUtf8.c_str()) != 0) {
            _exit(126);  // 与 shell 的"命令不可执行"退出码一致
        }
        execl("/bin/sh", "sh", "-c", cmdWithRedirect.c_str(), nullptr);
        _exit(127);
    } else if (child > 0) {
        auto startTime = std::chrono::steady_clock::now();
        int status = 0;
        bool stopped = false;

        // 轮询等待带超时 + 取消检查
        while (true) {
            if (isCancelled && isCancelled()) {  // [未验证] 取消检查（G1）
                kill(-child, SIGKILL);           // 负 pid = 整个进程组
                waitpid(child, &status, 0);      // 等待 kill 生效
                result.m_interrupted = true;
                result.m_exitCode = -1;
                stopped = true;
                break;
            }
            const pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) break;  // 子进程已退出

            const auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()
                >= timeoutSeconds) {
                kill(-child, SIGKILL);  // [未验证] 整组（W-2 缺陷同修）
                waitpid(child, &status, 0);
                result.m_timedOut = true;
                result.m_exitCode = -1;
                stopped = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!stopped) {
            result.m_exitCode = WEXITSTATUS(status);
        }
    } else {
        result.m_launchFailed = true;
        result.m_exitCode = -1;
        result.m_stderr = "Fork failed";
    }

    result.m_stdout = CLFEncoding::toUtf8(readFileContent(stdoutFile));
    result.m_stderr = CLFEncoding::toUtf8(readFileContent(stderrFile));

    auto tryRemove = [](const std::string& path) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (std::remove(path.c_str()) == 0) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    };
    tryRemove(stdoutFile);
    tryRemove(stderrFile);
#endif

    return result;
}

} // namespace CLF::CLFCore
