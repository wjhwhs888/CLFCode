// CLFCommandExec.cpp — 命令执行工具（兼容门面，2026-09-23 起）
// 实现已迁 CLFProcessRunner（CLFTypes，设计-命令执行层）：进程创建/终止
// （杀树）/取消/超时的唯一实现。本文件仅做参数换算与结果转写——
// 双编保持（clf_tools 源清单 + tools.command 插件显式编，两处签名同步重编）。
// clamp 分层（命令执行层 §10.2 单一 clamp 点原则）：有效超时 = min(请求值,
// 配置上限) 在 handler 层做；执行器只留 [1, 3600] 硬顶——门面不再 clamp。

#include "CLFTools/CLFCommandExec.hpp"
#include "CLFTypes/CLFProcessRunner.hpp"

namespace CLF::CLFTools {

CLFCommandResult executeCommand(const std::string& command, int timeoutSeconds,
                                const std::string& cwd,
                                const std::function<bool()>& isCancelled) {
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
