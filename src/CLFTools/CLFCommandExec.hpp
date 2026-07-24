// CLFCommandExec.hpp — 命令执行工具
// 异步子进程管理，执行 shell 命令并捕获输出

#pragma once

#include <string>

namespace CLF::CLFTools {

struct CLFCommandResult {
    int         m_exitCode = -1;
    std::string m_stdout;
    std::string m_stderr;
    bool        m_timedOut = false;
};

// 执行命令（带超时控制）
CLFCommandResult executeCommand(const std::string& command, int timeoutSeconds = 30);

} // namespace CLF::CLFTools
