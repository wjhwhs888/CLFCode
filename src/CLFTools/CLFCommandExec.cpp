// CLFCommandExec.cpp — 命令执行工具（兼容门面，2026-09-23 起）
// 实现已迁 CLFProcessRunner（CLFTypes，设计-命令执行层）：进程创建/终止
// （杀树）/取消/超时的唯一实现。本文件仅做参数换算与结果转写——
// 双编保持（clf_tools 源清单 + tools.command 插件显式编，两处签名同步重编）。
// 超时 clamp [1,600] 保留自原实现（保真）；命令执行层步骤 6 将改配置化
// （默认 120s、上限 command_max_timeout_sec 默认 600、handler 层 clamp）。

#include "CLFTools/CLFCommandExec.hpp"
#include "CLFTypes/CLFProcessRunner.hpp"

namespace CLF::CLFTools {

CLFCommandResult executeCommand(const std::string& command, int timeoutSeconds,
                                const std::string& cwd,
                                const std::function<bool()>& isCancelled) {
    // 参数校验：clamp 超时到合理范围（自原实现保真搬移）
    if (timeoutSeconds < 1) timeoutSeconds = 1;
    if (timeoutSeconds > 600) timeoutSeconds = 600;

    CLF::CLFCore::CLFExecSpec spec;
    spec.m_command    = command;
    spec.m_cwdUtf8    = cwd;
    spec.m_timeoutSec = timeoutSeconds;

    const CLF::CLFCore::CLFExecResult r =
        CLF::CLFCore::CLFProcessRunner::run(spec, isCancelled);

    CLFCommandResult result;
    result.m_exitCode    = r.m_exitCode;
    result.m_stdout      = r.m_stdout;
    result.m_stderr      = r.m_stderr;
    result.m_timedOut    = r.m_timedOut;
    result.m_interrupted = r.m_interrupted;
    return result;
}

} // namespace CLF::CLFTools
