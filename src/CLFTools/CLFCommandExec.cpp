// CLFCommandExec.cpp — 命令执行工具实现

#include "CLFTools/CLFCommandExec.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <chrono>
#include <thread>

namespace CLF::CLFTools {

CLFCommandResult executeCommand(const std::string& command, int timeoutSeconds) {
    CLFCommandResult result;
    std::string cmdWithRedirect;

#ifdef _WIN32
    // Windows: 使用临时文件捕获输出
    cmdWithRedirect = "cmd /c \"" + command + "\" > %TEMP%\\clf_cmd_stdout.txt 2> %TEMP%\\clf_cmd_stderr.txt";
#else
    cmdWithRedirect = command + " > /tmp/clf_cmd_stdout.txt 2> /tmp/clf_cmd_stderr.txt";
#endif

    auto startTime = std::chrono::steady_clock::now();

    int exitCode = std::system(cmdWithRedirect.c_str());

    auto elapsed = std::chrono::steady_clock::now() - startTime;
    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

    result.m_exitCode = exitCode;
    result.m_timedOut = (elapsedSeconds >= timeoutSeconds);

#ifdef _WIN32
    // 读取 Windows 临时文件
    FILE* stdoutFile = nullptr;
    FILE* stderrFile = nullptr;
    errno_t err;

    // ... 读取文件内容到 result.m_stdout / result.m_stderr
#else
    // 读取 Unix 临时文件
    FILE* stdoutFile = fopen("/tmp/clf_cmd_stdout.txt", "r");
    if (stdoutFile) {
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), stdoutFile)) {
            result.m_stdout += buffer;
        }
        fclose(stdoutFile);
    }

    FILE* stderrFile = fopen("/tmp/clf_cmd_stderr.txt", "r");
    if (stderrFile) {
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), stderrFile)) {
            result.m_stderr += buffer;
        }
        fclose(stderrFile);
    }
#endif

    return result;
}

} // namespace CLF::CLFTools
