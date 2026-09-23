// CLFCommandExec.hpp — 命令执行工具（兼容门面，2026-09-23 起）
// 实现唯一权威 = CLFProcessRunner（CLFTypes，设计-命令执行层 §6.2 定案：
// 门面内部转调、外部行为不变）；本门面供 handler（CLFCommandToolHandler）
// 与既有调用方使用——签名扩展为尾部追加 isCancelled（可为空 = 不可取消，
// 行为与改造前一致，向后兼容）。
// example:
//   auto r = executeCommand("git status", 30, "E:/proj/sub");
//   auto r2 = executeCommand("node test.js", 120, "", [] { return interrupted; });

#pragma once

#include <functional>
#include <string>

namespace CLF::CLFTools {

struct CLFCommandResult {
    int         m_exitCode = -1;
    std::string m_stdout;
    std::string m_stderr;
    bool        m_timedOut = false;
    bool        m_interrupted = false;  // A 批新增：取消触发（handler 读）
    bool        m_truncated = false;    // G2：输出超执行器限额（中段被丢弃）
};

// 执行命令（带超时控制）
// cwd 非空时作为子进程工作目录；**调用方负责校验其合法性**（工具层已做工作区边界检查）
// isCancelled 为空 = 不可中断（行为与现状一致，向后兼容）
// example:
//   auto r = executeCommand("git status", 30, "E:/proj/sub");
CLFCommandResult executeCommand(const std::string& command,
                                int timeoutSeconds = 30,
                                const std::string& cwd = "",
                                const std::function<bool()>& isCancelled = {});

} // namespace CLF::CLFTools
