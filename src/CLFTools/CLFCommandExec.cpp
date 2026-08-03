// CLFCommandExec.cpp — 命令执行工具实现

#include "CLFTools/CLFCommandExec.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

namespace CLF::CLFTools {

namespace {

// 读取文件全部内容到字符串
std::string readFileContent(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

// Windows 下将系统代码页（GBK/CP936）输出转为 UTF-8
std::string toUtf8(const std::string& input) {
    if (input.empty()) return input;
#ifdef _WIN32
    // ACP → UTF-16
    int wideLen = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS,
                                       input.c_str(), static_cast<int>(input.size()),
                                       nullptr, 0);
    if (wideLen <= 0) {
        // 含有非 ACP 字符，尝试原样返回（可能已是 UTF-8）
        return input;
    }
    std::wstring wide(wideLen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, input.c_str(), static_cast<int>(input.size()),
                        wide.data(), wideLen);

    // UTF-16 → UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideLen,
                                       nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return input;
    std::string utf8(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideLen,
                        utf8.data(), utf8Len, nullptr, nullptr);
    return utf8;
#else
    return input;
#endif
}

} // anonymous namespace

CLFCommandResult executeCommand(const std::string& command, int timeoutSeconds) {
    CLFCommandResult result;

    // 参数校验：clamp 超时到合理范围
    if (timeoutSeconds < 1) timeoutSeconds = 1;
    if (timeoutSeconds > 600) timeoutSeconds = 600;

#ifdef _WIN32
    // ==== Windows：CreateProcess + WaitForSingleObject（可真正终止子进程）====

    // 随机后缀防并发/残留冲突（PID + 时间戳）
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string suffix = std::to_string(GetCurrentProcessId()) + "_" + std::to_string(now);
    std::string stdoutFile = "clf_cmd_stdout_" + suffix + ".txt";
    std::string stderrFile = "clf_cmd_stderr_" + suffix + ".txt";

    // 命令中的 UTF-8 中文 → ACP，否则 cmd 乱码
    auto utf8ToAcp = [](const std::string& utf8) -> std::string {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        if (wlen <= 1) return utf8;
        std::wstring wide(wlen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), wlen - 1);
        int alen = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), wlen - 1,
                                        nullptr, 0, nullptr, nullptr);
        if (alen <= 0) return utf8;
        std::string acp(alen, '\0');
        WideCharToMultiByte(CP_ACP, 0, wide.c_str(), wlen - 1,
                            acp.data(), alen, nullptr, nullptr);
        return acp;
    };

    std::string cmdLine = "cmd.exe /c \"" + utf8ToAcp(command) + "\" > \""
                        + stdoutFile + "\" 2> \"" + stderrFile + "\"";

    // CreateProcess（隐藏窗口，获取进程句柄以支持 TerminateProcess）
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        DWORD err = GetLastError();
        result.m_exitCode = -1;
        result.m_stderr = "Failed to create process: error " + std::to_string(err);
        DeleteFileA(stdoutFile.c_str());
        DeleteFileA(stderrFile.c_str());
        return result;
    }

    CloseHandle(pi.hThread);  // 不需要线程句柄

    // 等待超时，超时则强制终止
    DWORD waitResult = WaitForSingleObject(pi.hProcess,
                                           static_cast<DWORD>(timeoutSeconds) * 1000);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        result.m_timedOut = true;
        result.m_exitCode = -1;
        result.m_stderr = "Timed out after " + std::to_string(timeoutSeconds) + "s";
    } else {
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        result.m_exitCode = static_cast<int>(exitCode);
        result.m_timedOut = false;
    }
    CloseHandle(pi.hProcess);

    // 读取输出（转为 UTF-8，防止 GBK 中文破坏 JSON）
    result.m_stdout = toUtf8(readFileContent(stdoutFile));
    if (result.m_stderr.empty()) {
        result.m_stderr = toUtf8(readFileContent(stderrFile));
    }

    // 清理临时文件（重试 3 次）
    auto tryRemove = [](const std::string& path) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (DeleteFileA(path.c_str())) return;
            if (GetLastError() == ERROR_FILE_NOT_FOUND) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    };
    tryRemove(stdoutFile);
    tryRemove(stderrFile);

#else
    // ==== Linux/macOS：fork + execvp + waitpid（可真正终止子进程）====
    std::string pidStr = std::to_string(static_cast<long long>(getpid()));
    std::string stdoutFile = "/tmp/clf_cmd_stdout_" + pidStr + ".txt";
    std::string stderrFile = "/tmp/clf_cmd_stderr_" + pidStr + ".txt";
    std::string cmdWithRedirect = command + " > " + stdoutFile + " 2> " + stderrFile;

    pid_t child = fork();
    if (child == 0) {
        // 子进程：通过 sh 执行命令
        execl("/bin/sh", "sh", "-c", cmdWithRedirect.c_str(), nullptr);
        _exit(127);
    } else if (child > 0) {
        auto startTime = std::chrono::steady_clock::now();
        int status = 0;
        bool timedOut = false;

        // 轮询等待带超时
        while (true) {
            pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) break; // 子进程已退出

            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()
                >= timeoutSeconds) {
                kill(child, SIGKILL);
                waitpid(child, &status, 0); // 等待 kill 生效
                timedOut = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        result.m_exitCode = timedOut ? -1 : WEXITSTATUS(status);
        result.m_timedOut = timedOut;
    } else {
        result.m_exitCode = -1;
        result.m_stderr = "Fork failed";
    }

    result.m_stdout = toUtf8(readFileContent(stdoutFile));
    result.m_stderr = toUtf8(readFileContent(stderrFile));

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

} // namespace CLF::CLFTools
