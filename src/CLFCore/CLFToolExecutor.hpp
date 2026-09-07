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

namespace CLF::CLFPluginApi { class ICLFFileService; }
namespace CLF::CLFTypes {
class ICLFContentOutput;
class ICLFProgressOutput;
}
namespace CLF::CLFCore {

struct ToolStats;

class CLFToolExecutor {
public:
    // C3：依赖窄化——5 方法跨内容+进度两通道，收两窄指针替代宽 ICLFOutput
    CLFToolExecutor(std::vector<CLFTool>& tools,
                    CLFSecurityPolicy& policy,
                    std::function<bool(const std::string&)> confirmCallback,
                    ToolStats& stats,
                    CLF::CLFPluginApi::ICLFFileService* fileService,
                    CLF::CLFTypes::ICLFContentOutput* contentOutput = nullptr,
                    CLF::CLFTypes::ICLFProgressOutput* progressOutput = nullptr,
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
    CLF::CLFTypes::ICLFContentOutput*  m_contentOutput = nullptr;   // C3 窄指针
    CLF::CLFTypes::ICLFProgressOutput* m_progressOutput = nullptr;  // C3 窄指针
    std::atomic<bool>* m_interruptFlag;
    const CLFTimerLabels* m_labels = nullptr;
    std::atomic<int>* m_thinkingSec = nullptr;
    CLF::CLFPluginApi::ICLFFileService* m_fileService = nullptr;
};

} // namespace CLF::CLFCore
