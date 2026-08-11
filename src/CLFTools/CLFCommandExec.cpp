// CLFCommandExec.cpp — 命令执行工具实现
// 编码转换 → 委托 CLFEncoding

#include "CLFTools/CLFCommandExec.hpp"
#include "CLFTypes/CLFEncoding.hpp"

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

} // anonymous namespace

CLFCommandResult executeCommand(const std::string& command, int timeoutSeconds) {
    CLFCommandResult result;

    // 参数校验：clamp 超时到合理范围
    if (timeoutSeconds < 1) timeoutSeconds = 1;
    if (timeoutSeconds > 600) timeoutSeconds = 600;

#ifdef _WIN32
    // ==== Windows：CreateProcess + 匿名管道（可靠捕获 stdout/stderr）====

    // 创建匿名管道（可继承句柄）
    HANDLE hOutRead, hOutWrite, hErrRead, hErrWrite;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    CreatePipe(&hOutRead, &hOutWrite, &sa, 0);
    CreatePipe(&hErrRead, &hErrWrite, &sa, 0);
    SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hErrRead, HANDLE_FLAG_INHERIT, 0);

    // 匹配 std::system 行为：cmd.exe /s /c "..."
    std::string cmdLine = "cmd.exe /s /c \"" + CLF::CLFCore::CLFEncoding::fromUtf8(command) + "\"";
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hOutWrite;
    si.hStdError  = hErrWrite;
    si.hStdInput  = nullptr;  // cmd.exe /s /c 模式不需要 stdin，避免继承 FTXUI 控制台句柄

    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr,
                        TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        DWORD err = GetLastError();
        CloseHandle(hOutRead); CloseHandle(hOutWrite);
        CloseHandle(hErrRead); CloseHandle(hErrWrite);
        result.m_exitCode = -1;
        result.m_stderr = "Failed to create process: error " + std::to_string(err);
        return result;
    }

    CloseHandle(hOutWrite);
    CloseHandle(hErrWrite);
    CloseHandle(pi.hThread);

    // 轮询读取管道 + 超时检测
    std::string outBuf, errBuf;
    char buf[4096];
    DWORD available, bytesRead;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    bool timedOut = false;

    while (true) {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 50);

        // 读取 stdout
        while (PeekNamedPipe(hOutRead, nullptr, 0, nullptr, &available, nullptr)
               && available > 0) {
            DWORD toRead = (available > sizeof(buf) - 1) ? sizeof(buf) - 1 : available;
            if (ReadFile(hOutRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0) {
                outBuf.append(buf, bytesRead);
            }
        }
        // 读取 stderr
        while (PeekNamedPipe(hErrRead, nullptr, 0, nullptr, &available, nullptr)
               && available > 0) {
            DWORD toRead = (available > sizeof(buf) - 1) ? sizeof(buf) - 1 : available;
            if (ReadFile(hErrRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0) {
                errBuf.append(buf, bytesRead);
            }
        }

        if (waitResult == WAIT_OBJECT_0) break;  // 进程正常退出

        if (std::chrono::steady_clock::now() >= deadline) {
            TerminateProcess(pi.hProcess, 1);
            timedOut = true;
            break;
        }
    }

    // 进程退出后清空管道残余
    while (PeekNamedPipe(hOutRead, nullptr, 0, nullptr, &available, nullptr)
           && available > 0) {
        DWORD toRead = (available > sizeof(buf) - 1) ? sizeof(buf) - 1 : available;
        if (ReadFile(hOutRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0)
            outBuf.append(buf, bytesRead);
    }
    while (PeekNamedPipe(hErrRead, nullptr, 0, nullptr, &available, nullptr)
           && available > 0) {
        DWORD toRead = (available > sizeof(buf) - 1) ? sizeof(buf) - 1 : available;
        if (ReadFile(hErrRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0)
            errBuf.append(buf, bytesRead);
    }

    if (timedOut) {
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
    CloseHandle(hOutRead);
    CloseHandle(hErrRead);

    result.m_stdout = CLF::CLFCore::CLFEncoding::toUtf8(outBuf);
    if (result.m_stderr.empty()) {
        result.m_stderr = CLF::CLFCore::CLFEncoding::toUtf8(errBuf);
    }

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

    result.m_stdout = CLF::CLFCore::CLFEncoding::toUtf8(readFileContent(stdoutFile));
    result.m_stderr = CLF::CLFCore::CLFEncoding::toUtf8(readFileContent(stderrFile));

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
