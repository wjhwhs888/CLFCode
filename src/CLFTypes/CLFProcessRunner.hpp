// CLFProcessRunner.hpp — 命令执行器（设计-命令执行层 §六/§七/§八，2026-09-23）
// 进程创建 / 终止（杀树）/ 取消 / 超时的唯一实现；平台差异集中于此，
// 上层只见 CLFExecSpec / CLFExecResult（进程内结构，不进插件 ABI）。
// 落点 clf_types：零项目依赖、最底层稳定组件（SDP）——命令插件与
// clf_tools 门面均经 clf_types 链接，单编即可用（不再双编源文件）。
//
// 第一波（G1）：shell 模式等价搬移 + 取消检查 + Job Object / 进程组杀树
// （超时路径同改）。第二波（G2）：输出限额 m_maxOutputBytes（头+尾各半
// 保留、中段丢弃、行粒度切分）——m_truncated 标记；错误归一化（G3）、
// argv 模式属第二波后续（设计-命令执行层 §十一 步骤 4/5）——进程内结构
// 加字段零破坏。
//
// 取消语义（设计-中断时效性 §术语）：isCancelled 为空 = 不可取消（行为与
// 改造前一致）；命中 → 杀整棵树 + m_interrupted = true（与 m_timedOut 互斥）。
// example:
//   CLFExecSpec spec{"git status", 120, ""};
//   auto r = CLFProcessRunner::run(spec);
//   auto r2 = CLFProcessRunner::run(spec, [] { return interrupted.load(); });

#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace CLF::CLFCore {

// 执行请求（宿主内部结构，不进插件 ABI）
struct CLFExecSpec {
    std::string m_command;          // shell 模式命令字符串（经平台 shell 解释）
    std::string m_cwdUtf8;          // 子进程工作目录（UTF-8；空 = 继承）
    int         m_timeoutSec = 120; // 请求超时；执行器内 clamp 到 [1, 硬顶]
    // G2（2026-09-23）：执行器层输出限额（stdout/stderr 各一份预算）——
    // 读取阶段头+尾各半保留、中段丢弃（防内存膨胀 + 保尾部结论）
    size_t      m_maxOutputBytes = 256 * 1024;
};

// 执行结果
struct CLFExecResult {
    int         m_exitCode     = -1;
    bool        m_timedOut     = false;
    bool        m_interrupted  = false;  // 取消触发（与 m_timedOut 互斥）
    bool        m_launchFailed = false;
    bool        m_truncated    = false;  // G2：输出超限额（中段被丢弃）
    std::string m_stdout;                // 已按头+尾保留（限额内）
    std::string m_stderr;
};

class CLFProcessRunner {
public:
    // 执行器层硬顶（安全网：防调用方误传巨大值；单一 clamp 点原则——
    // 配置上限 min 在 handler 层做，此处只兜底）
    static constexpr int kHardTimeoutCapSec = 3600;

    // 执行命令。isCancelled 为空 = 不可取消（向后兼容）
    static CLFExecResult run(const CLFExecSpec& spec,
                             const std::function<bool()>& isCancelled = {});
};

} // namespace CLF::CLFCore
