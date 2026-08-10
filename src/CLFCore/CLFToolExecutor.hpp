// CLFToolExecutor.hpp — 工具调用执行器
// 负责工具查表、安全策略检查、用户确认、结果格式化显示
//
// example:
//   CLFToolExecutor executor(tools, securityPolicy, confirmCb);
//   auto results = executor.execute(toolCalls);

#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "CLFTypes/CLFTypes.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"

namespace CLF::CLFCore {

struct ToolStats;

class CLFToolExecutor {
public:
    CLFToolExecutor(std::vector<CLFTool>& tools,
                    CLFSecurityPolicy& policy,
                    std::function<bool(const std::string&)> confirmCallback,
                    ToolStats& stats,
                    CLF::CLFTypes::ICLFOutput* output = nullptr,
                    std::atomic<bool>* interruptFlag = nullptr,
                    const CLFTimerLabels* labels = nullptr,
                    std::atomic<int>* thinkingSec = nullptr);

    // 执行一组工具调用
    // labels + thinkingSec 非空时启用渐进式显示（showProgress/finishProgress）
    std::vector<CLFToolResult> execute(const std::vector<CLFToolCall>& calls);

private:
    std::vector<CLFTool>& m_tools;
    CLFSecurityPolicy& m_securityPolicy;
    std::function<bool(const std::string&)> m_confirmCallback;
    ToolStats& m_stats;
    CLF::CLFTypes::ICLFOutput* m_output;
    std::atomic<bool>* m_interruptFlag;
    const CLFTimerLabels* m_labels = nullptr;
    std::atomic<int>* m_thinkingSec = nullptr;
};

} // namespace CLF::CLFCore
