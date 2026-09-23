// CLFSystemInfoProvider.cpp — 系统信息捕获实现
// C5 拆分：逻辑自 CLFSystemPromptBuilder（detectOsInfo/detectShellInfo/
// captureGitStatus）原样搬移；s_gitCache 文件级静态 → 实例成员（P0-7）

#include "CLFCore/CLFSystemInfoProvider.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>

#include "CLFCore/CLFSubprocessRunner.hpp"
#include "CLFTypes/CLFTextUtil.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace CLF::CLFCore {

std::string CLFSystemInfoProvider::detectOsInfo() {
#ifdef _WIN32
    // 命令执行层 §九（2026-09-23）：`ver` 是 cmd 内建、无 argv 等价物——
    // 改用 RtlGetVersion 原语（ntdll 每个进程必已加载，GetModuleHandleW +
    // GetProcAddress 零链接依赖；GetVersionExW 已弃用且受版本欺骗）。
    // 输出形制："Windows 10.0.26200"（信息等价于原 ver 截取；第三波
    // 平台层收敛时下沉 CLFPlatform 版本原语）
    auto* ntdll = GetModuleHandleW(L"ntdll.dll");
    using RtlGetVersionFn = LONG(WINAPI*)(RTL_OSVERSIONINFOW*);
    const auto rtlGetVersion = ntdll
        ? reinterpret_cast<RtlGetVersionFn>(
              GetProcAddress(ntdll, "RtlGetVersion"))
        : nullptr;
    if (rtlGetVersion) {
        RTL_OSVERSIONINFOW vi = {};
        vi.dwOSVersionInfoSize = sizeof(vi);
        if (rtlGetVersion(&vi) == 0) {
            std::ostringstream oss;
            oss << "- 操作系统：Windows " << vi.dwMajorVersion << "."
                << vi.dwMinorVersion << "." << vi.dwBuildNumber;
            return oss.str();
        }
    }
    return "- 操作系统：Windows";
#else
    // [未验证] argv 化：uname 不经 shell（stderr 单独捕获，不再需要 2>/dev/null）
    std::string uname = CLFSubprocessRunner::run({"uname", "-a"});
    if (!uname.empty()) return "- 操作系统：" + uname;
    return "- 操作系统：Linux / macOS";
#endif
}

std::string CLFSystemInfoProvider::detectShellInfo() {
#ifdef _WIN32
    const char* comspec = std::getenv("COMSPEC");
    if (comspec) {
        std::string s(comspec);
        // 判断是 cmd 还是 bash
        if (s.find("bash") != std::string::npos) return "bash (Git Bash)";
        if (s.find("powershell") != std::string::npos || s.find("pwsh") != std::string::npos)
            return "PowerShell";
        return "cmd.exe";
    }
    return "cmd.exe";
#else
    const char* shell = std::getenv("SHELL");
    if (shell) {
        std::string s(shell);
        size_t pos = s.rfind('/');
        return (pos != std::string::npos) ? s.substr(pos + 1) : s;
    }
    return "sh";
#endif
}

std::string CLFSystemInfoProvider::captureGitStatus(const std::string& workspaceRoot) {
    // 检查缓存是否有效
    std::time_t now = std::time(nullptr);
    if (m_gitCache.workspaceRoot == workspaceRoot &&
        m_gitCache.captureTime > 0 &&
        (now - m_gitCache.captureTime) < kGitTTLSeconds) {
        return m_gitCache.info;  // 缓存命中
    }

    // 检查是否为 git 仓库
    std::error_code ec;
    if (!fs::exists(workspaceRoot + "/.git", ec)) {
        m_gitCache = {};
        return "";
    }

    // 保存当前目录，切换到工作区执行 git 命令（命令执行层 §九 argv 化：
    // stderr 由执行器单独捕获，2>nul 重定向整类消除；含空格路径不再需要
    // 引号转义——参数原样到达子进程）
    std::string result;
    std::string branch = CLFSubprocessRunner::run(
        {"git", "-C", workspaceRoot, "branch", "--show-current"});
    if (branch.empty()) {
        m_gitCache = {};
        return "";
    }

    result = "- Git 分支：" + branch + "\n- 最近提交：\n";
    std::string log = CLFSubprocessRunner::run(
        {"git", "-C", workspaceRoot, "log", "--oneline", "-5"});
    if (!log.empty()) {
        std::istringstream iss(log);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) result += "  " + line + "\n";
        }
    }

    std::string status = CLFSubprocessRunner::run(
        {"git", "-C", workspaceRoot, "status", "--short"});
    if (status.empty()) {
        result += "- 工作区状态：干净（无未提交变更）\n";
    } else {
        int count = 0;
        std::istringstream iss(status);
        std::string line;
        while (std::getline(iss, line)) { if (!line.empty()) ++count; }
        result += "- 工作区状态：" + std::to_string(count) + " 个文件有变更\n";
    }

    // 时间戳（A2：唯一裸 localtime → CLFTextUtil::localNow，线程安全）
    result += std::string("（Git 状态捕获于 ")
           + CLFTextUtil::localNow("%H:%M:%S")
           + "，如需实时状态请使用 execute_command 查询）\n";

    // 更新缓存
    m_gitCache.info           = result;
    m_gitCache.workspaceRoot  = workspaceRoot;
    m_gitCache.captureTime    = now;
    return result;
}

} // namespace CLF::CLFCore
