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
    std::string cmdWithRedirect;

#ifdef _WIN32
    // Windows: 使用唯一临时文件捕获输出（含进程 ID 防并发冲突）
    std::string pidStr = std::to_string(static_cast<long long>(GetCurrentProcessId()));
    std::string stdoutFile = "clf_cmd_stdout_" + pidStr + ".txt";
    std::string stderrFile = "clf_cmd_stderr_" + pidStr + ".txt";
    cmdWithRedirect = "cmd /c \"" + command + "\" > \"" + stdoutFile
                    + "\" 2> \"" + stderrFile + "\"";
#else
    std::string pidStr = std::to_string(static_cast<long long>(getpid()));
    std::string stdoutFile = "/tmp/clf_cmd_stdout_" + pidStr + ".txt";
    std::string stderrFile = "/tmp/clf_cmd_stderr_" + pidStr + ".txt";
    cmdWithRedirect = command + " > " + stdoutFile + " 2> " + stderrFile;
#endif

    auto startTime = std::chrono::steady_clock::now();

    int exitCode = std::system(cmdWithRedirect.c_str());

    auto elapsed = std::chrono::steady_clock::now() - startTime;
    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

    result.m_exitCode = exitCode;
    result.m_timedOut = (elapsedSeconds >= timeoutSeconds);

    // 读取输出文件
    result.m_stdout = readFileContent(stdoutFile);
    result.m_stderr = readFileContent(stderrFile);

    // 清理临时文件
#ifdef _WIN32
    DeleteFileA(stdoutFile.c_str());
    DeleteFileA(stderrFile.c_str());
#else
    std::remove(stdoutFile.c_str());
    std::remove(stderrFile.c_str());
#endif

    return result;
}

} // namespace CLF::CLFTools
