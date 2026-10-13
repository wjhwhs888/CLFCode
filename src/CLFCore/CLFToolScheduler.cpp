// CLFToolScheduler.cpp — 并发工具调度器实现（事件化重构分册 3）

#include "CLFCore/CLFToolScheduler.hpp"

namespace CLF::CLFCore {

ToolExecMode classifyMode(const CLFTool& tool, const std::string& argsJson) {
    if (!tool.m_isConcurrencySafe) return ToolExecMode::Exclusive;  // fail-closed
    try {
        // 约束（dsh §16.2）：判定内不抛异常表达"不安全"——直接返回 false；
        // 此处 catch 兜底（第三方声明异常 → 独占）
        return tool.m_isConcurrencySafe(argsJson) ? ToolExecMode::Parallel
                                                  : ToolExecMode::Exclusive;
    } catch (...) {
        return ToolExecMode::Exclusive;
    }
}

} // namespace CLF::CLFCore
