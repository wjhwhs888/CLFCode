// CLFToolExecutor.hpp — 工具调用执行器
// 负责工具查表、安全策略检查、用户确认、结果格式化显示
//
// example:
//   CLFToolExecutor executor(tools, securityPolicy, confirmCb);
//   auto results = executor.execute(toolCalls);

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "CLFCore/CLFTypes.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"

namespace CLF::CLFCore {

struct ToolStats;

class CLFToolExecutor {
public:
    CLFToolExecutor(std::vector<CLFTool>& tools,
                    CLFSecurityPolicy& policy,
                    std::function<bool(const std::string&)> confirmCallback,
                    ToolStats& stats);

    // 执行一组工具调用，返回结果列表
    std::vector<CLFToolResult> execute(const std::vector<CLFToolCall>& calls);

private:
    std::vector<CLFTool>& m_tools;
    CLFSecurityPolicy& m_securityPolicy;
    std::function<bool(const std::string&)> m_confirmCallback;
    ToolStats& m_stats;
};

} // namespace CLF::CLFCore
