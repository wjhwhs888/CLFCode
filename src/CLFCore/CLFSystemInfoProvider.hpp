// CLFSystemInfoProvider.hpp — 系统信息捕获（C5：Builder 拆分，2026-09-07）
// OS / Shell 检测 + Git 状态捕获（TTL 30s 缓存）。
// Git 缓存随实例（消文件级静态对象，P0-7）；detectOsInfo/detectShellInfo
// 无状态保持 static。
//
// example:
//   CLFSystemInfoProvider info;
//   std::string git = info.captureGitStatus(workspaceRoot);  // TTL 内复用

#pragma once

#include <ctime>
#include <string>

namespace CLF::CLFCore {

class CLFSystemInfoProvider {
public:
    // 操作系统信息行（"- 操作系统：…"）；检测失败 → 平台默认文案
    static std::string detectOsInfo();
    // Shell 信息（"bash (Git Bash)" / "PowerShell" / "cmd.exe" / POSIX shell 名）
    static std::string detectShellInfo();

    // Git 状态快照（分支/最近提交/工作区变更 + 捕获时间）；
    // 非 git 仓库 / 无分支 → 空串。TTL 30s 内同 workspaceRoot 复用缓存
    std::string captureGitStatus(const std::string& workspaceRoot);

private:
    static constexpr int kGitTTLSeconds = 30;

    struct GitCache {
        std::string info;
        std::string workspaceRoot;
        std::time_t captureTime = 0;
    };
    GitCache m_gitCache;
};

} // namespace CLF::CLFCore
