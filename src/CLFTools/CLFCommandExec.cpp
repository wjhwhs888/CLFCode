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

#ifdef _WIN32
    // ==== Windows：std::system（命令行解析正确）+ 后台线程超时 ====
    std::string pidStr = std::to_string(static_cast<long long>(GetCurrentProcessId()));
    std::string stdoutFile = "clf_cmd_stdout_" + pidStr + ".txt";
    std::string stderrFile = "clf_cmd_stderr_" + pidStr + ".txt";

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

    std::string cmdLine = "cmd /c \"" + utf8ToAcp(command) + "\" > \"" + stdoutFile
                        + "\" 2> \"" + stderrFile + "\"";

    auto startTime = std::chrono::steady_clock::now();

    // 后台线程运行 std::system，主线程等待超时
    std::atomic<bool> cmdDone{false};
    int exitCode = -1;
    std::thread cmdThread([&]() {
        exitCode = std::system(cmdLine.c_str());
        cmdDone.store(true, std::memory_order_release);
    });

    // 轮询等待，超时则放弃（线程会残留但 CLFCode 不再阻塞）
    auto deadline = startTime + std::chrono::seconds(timeoutSeconds);
    bool timedOut = false;
    while (!cmdDone.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            timedOut = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (timedOut) {
        cmdThread.detach(); // 无法杀 std::system，但不再阻塞主线程
        result.m_timedOut = true;
        result.m_exitCode = -1;
        result.m_stderr  = "Timed out after " + std::to_string(timeoutSeconds) + "s";
    } else {
        cmdThread.join();
        result.m_exitCode = exitCode;
        result.m_timedOut = false;
    }

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
    // ==== Linux/macOS：std::system（后续可改为 fork+alarm）====
    std::string pidStr = std::to_string(static_cast<long long>(getpid()));
    std::string stdoutFile = "/tmp/clf_cmd_stdout_" + pidStr + ".txt";
    std::string stderrFile = "/tmp/clf_cmd_stderr_" + pidStr + ".txt";
    std::string cmdWithRedirect = command + " > " + stdoutFile + " 2> " + stderrFile;

    auto startTime = std::chrono::steady_clock::now();
    int exitCode = std::system(cmdWithRedirect.c_str());
    auto elapsed = std::chrono::steady_clock::now() - startTime;
    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

    result.m_exitCode = exitCode;
    result.m_timedOut = (elapsedSeconds >= timeoutSeconds);
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
