// CLFSubprocessRunner.cpp — 子进程命令执行封装实现
// argv 模式门面（命令执行层 §九，2026-09-23）：原 popen 封装必然经过
// shell → `2>nul` 重定向缺陷出生于此；改经 CLFProcessRunner argv 模式
// （不经 shell），stderr 由执行器单独捕获。

#include "CLFCore/CLFSubprocessRunner.hpp"

#include <string>

#include "CLFTypes/CLFProcessRunner.hpp"

namespace CLF::CLFCore {

std::string CLFSubprocessRunner::run(const std::vector<std::string>& argv) {
    if (argv.empty()) return "";

    CLFExecSpec spec;
    spec.m_argv        = argv;
    spec.m_timeoutSec  = 30;   // 辅助命令短平快（原 popen 无超时——新设 30s 兜底）
    spec.m_maxOutputBytes = 64 * 1024;  // 辅助命令输出小（原无上限——设限防膨胀）

    CLFExecResult r = CLFProcessRunner::run(spec);
    if (!r.m_stdout.empty() && r.m_stdout.back() == '\n') r.m_stdout.pop_back();
    return r.m_stdout;
}

} // namespace CLF::CLFCore
