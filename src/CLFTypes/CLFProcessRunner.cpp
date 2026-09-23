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
#include <cstring>
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

// ============================================================================
// G3 错误归一化（2026-09-23 命令执行层 §6.5）：not_found / permission /
// launch_failed 的识别模式（双语——中文 Windows 的 cmd 输出中文文案）。
// timeout / interrupted 由执行器自身状态直接归一；non_zero_exit 属 handler
// 层（依赖 exitCodeMeansSuccess 白名单，执行器不可见）。
// ============================================================================

// CreateProcess 失败码归一（Win32 错误码：2 = 找不到文件、5 = 拒绝访问）
std::string classifyLaunchError(int errorCode) {
    if (errorCode == 2) return "not_found";
    if (errorCode == 5) return "permission";
    return "launch_failed";
}

// 命令启动成功但报错的 stderr 文案归一（cmd 报"不是内部或外部命令"
// 等场景；chcp 65001 下 stderr 为 UTF-8 可直接匹配中文字面量）
std::string classifyStderrError(const std::string& stderrText) {
    // not_found：命令不存在（中/英）
    if (stderrText.find("不是内部或外部命令") != std::string::npos
        || stderrText.find("is not recognized as an internal") != std::string::npos
        || stderrText.find("系统找不到指定的文件") != std::string::npos
        || stderrText.find("The system cannot find the file") != std::string::npos) {
        return "not_found";
    }
    // permission：拒绝访问（中/英）
    if (stderrText.find("拒绝访问") != std::string::npos
        || stderrText.find("Access is denied") != std::string::npos) {
        return "permission";
    }
    return "";
}

// ============================================================================
// G2 输出限额器（2026-09-23 命令执行层 §6.4）：头+尾保留、中段丢弃——
// 命令结论常在尾部，只保头会让模型看不到结论（W-6 缺陷修根）。
// 行粒度环形切分：\n 是 ASCII（0x0A）——GBK 双字节 0x81-0xFE / 0x40-0xFE
// 均不含 0x0A，行边界切割零劈半风险（劈半字节进 toUtf8 会原样返回、
// 污染 JSON——2.2b type_error.316 事故）。单行超尾预算时按字节切尾 +
// GBK 边界回退（切点落在次字节则前进 1）。
// ============================================================================
struct OutputLimiter {
    std::string              head;        // 头部累积（≤ headBudget 字节）
    std::vector<std::string> tailLines;   // 尾部环形（行粒度）
    std::string              pendingLine; // 跨块半行
    size_t                   tailBytes = 0;
    bool                     overflow  = false;   // 中段被丢弃
    const size_t             headBudget;
    const size_t             tailBudget;

    explicit OutputLimiter(size_t totalBudget)
        : headBudget(totalBudget / 2), tailBudget(totalBudget / 2) {
        head.reserve(headBudget);
    }

    // 批量切行累积（memchr 找 \n 而非逐字节——大输出性能）
    void append(const char* data, size_t len) {
        size_t pos = 0;
        while (pos < len) {
            const void* hit = std::memchr(data + pos, '\n', len - pos);
            if (!hit) {
                pendingLine.append(data + pos, len - pos);
                return;
            }
            const size_t lineEnd = static_cast<const char*>(hit) - data;
            pendingLine.append(data + pos, lineEnd - pos + 1);  // 含 \n
            pushLine(std::move(pendingLine));
            pendingLine.clear();
            pos = lineEnd + 1;
        }
    }

    void pushLine(std::string line) {
        if (head.size() + line.size() <= headBudget) {
            head += line;
            return;
        }
        // 头预算满 → 进入尾环形（中段开始丢弃）
        overflow = true;
        tailBytes += line.size();
        tailLines.push_back(std::move(line));
        while (tailBytes > tailBudget && tailLines.size() > 1) {
            tailBytes -= tailLines.front().size();
            tailLines.erase(tailLines.begin());
        }
        if (tailBytes > tailBudget && tailLines.size() == 1) {
            // 单行超尾预算：字节切尾 + GBK 边界回退
            std::string& only = tailLines.front();
            size_t keep = tailBudget;
            const size_t cut = only.size() - keep;
            if (cut > 0 && cut < only.size()) {
                const unsigned char prev = static_cast<unsigned char>(only[cut - 1]);
                const unsigned char cur  = static_cast<unsigned char>(only[cut]);
                if (prev >= 0x81 && prev <= 0xFE && cur >= 0x40 && cur <= 0xFE
                    && cur != 0x7F) {
                    ++keep;   // 切点劈半 GBK 双字节 → 前进 1
                }
                only = only.substr(only.size() - keep);
            }
            tailBytes = only.size();
        }
    }

    std::string result() const {
        std::string out = head;
        if (overflow) {
            out += "\n...[中间输出省略]...\n";
            for (const auto& l : tailLines) out += l;
        }
        out += pendingLine;  // 尾部半行（head 模式下为 head 的自然延续）
        return out;
    }
};

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
        // G3：launch 失败码归一（2 → not_found、5 → permission、其余 launch_failed）
        result.m_launchErrorCode = static_cast<int>(err);
        result.m_errorKind = classifyLaunchError(static_cast<int>(err));
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
    // G2：输出限额器（头+尾保留、中段丢弃——防内存膨胀 + 保尾部结论）
    OutputLimiter outLimiter(spec.m_maxOutputBytes);
    OutputLimiter errLimiter(spec.m_maxOutputBytes);
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
            result.m_errorKind = "interrupted";   // G3
            break;
        }

        const DWORD waitResult = WaitForSingleObject(pi.hProcess, 50);

        // 读取 stdout（limiter 超限后仍继续 drain 管道——不读会写满死锁子进程）
        while (PeekNamedPipe(hOutRead, nullptr, 0, nullptr, &available, nullptr)
               && available > 0) {
            const DWORD toRead = (available > sizeof(buf) - 1)
                               ? sizeof(buf) - 1 : available;
            if (ReadFile(hOutRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0) {
                outLimiter.append(buf, bytesRead);
            }
        }
        // 读取 stderr
        while (PeekNamedPipe(hErrRead, nullptr, 0, nullptr, &available, nullptr)
               && available > 0) {
            const DWORD toRead = (available > sizeof(buf) - 1)
                               ? sizeof(buf) - 1 : available;
            if (ReadFile(hErrRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0) {
                errLimiter.append(buf, bytesRead);
            }
        }

        if (waitResult == WAIT_OBJECT_0) break;  // 进程正常退出

        if (std::chrono::steady_clock::now() >= deadline) {
            terminateTree();  // 超时路径同改：原只杀父（W-2 缺陷）
            result.m_timedOut = true;
            result.m_exitCode = -1;
            result.m_errorKind = "timeout";   // G3
            break;
        }
    }

    // 进程退出后清空管道残余
    while (PeekNamedPipe(hOutRead, nullptr, 0, nullptr, &available, nullptr)
           && available > 0) {
        const DWORD toRead = (available > sizeof(buf) - 1) ? sizeof(buf) - 1 : available;
        if (ReadFile(hOutRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0)
            outLimiter.append(buf, bytesRead);
    }
    while (PeekNamedPipe(hErrRead, nullptr, 0, nullptr, &available, nullptr)
           && available > 0) {
        const DWORD toRead = (available > sizeof(buf) - 1) ? sizeof(buf) - 1 : available;
        if (ReadFile(hErrRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0)
            errLimiter.append(buf, bytesRead);
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

    result.m_truncated = outLimiter.overflow || errLimiter.overflow;
    result.m_stdout = CLFEncoding::toUtf8(outLimiter.result());
    if (result.m_stderr.empty()) {
        result.m_stderr = CLFEncoding::toUtf8(errLimiter.result());
    }
    // G3：启动成功但报错（cmd "不是内部或外部命令"等）的 stderr 文案归一
    if (result.m_errorKind.empty() && !result.m_stderr.empty()) {
        result.m_errorKind = classifyStderrError(result.m_stderr);
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
                result.m_errorKind = "interrupted";   // [未验证] G3
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
                result.m_errorKind = "timeout";   // [未验证] G3
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

    // G2 限额（[未验证]：读全量后按同一口径截——内存膨胀未防；管道直读
    // 属命令执行层 §八 第二期）
    OutputLimiter outLimiter(spec.m_maxOutputBytes);
    OutputLimiter errLimiter(spec.m_maxOutputBytes);
    {
        const std::string rawOut = readFileContent(stdoutFile);
        outLimiter.append(rawOut.data(), rawOut.size());
        const std::string rawErr = readFileContent(stderrFile);
        errLimiter.append(rawErr.data(), rawErr.size());
    }
    result.m_truncated = outLimiter.overflow || errLimiter.overflow;
    result.m_stdout = CLFEncoding::toUtf8(outLimiter.result());
    result.m_stderr = CLFEncoding::toUtf8(errLimiter.result());

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
