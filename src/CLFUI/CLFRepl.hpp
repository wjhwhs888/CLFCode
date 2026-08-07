// CLFRepl.hpp — REPL 交互循环 (FTXUI 全帧驱动)
// 命令分发 → CLFCommandDispatcher; 输入+渲染 → FTXUI Loop + Modal
//
// example:
//   CLFAgentLoop agent(config);
//   CLFRepl repl(agent, historyDir);
//   return repl.run();

#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace CLF::CLFCore { class CLFAgentLoop; }
namespace CLF::CLFTypes { class ICLFOutput; }

namespace CLF::CLFUI {

class CLFCommandDispatcher;

class CLFRepl {
public:
    CLFRepl(CLF::CLFCore::CLFAgentLoop& agent, const std::string& historyDir,
            CLF::CLFTypes::ICLFOutput* output = nullptr);
    ~CLFRepl();

    int  run();
    bool confirmDialog(const std::string& prompt);
    void submit(const std::string& input);
    void cycleMode();

private:
    void printBanner();
    void saveSession(bool incomplete);

    CLF::CLFCore::CLFAgentLoop& m_agent;
    CLF::CLFTypes::ICLFOutput*  m_output;
    std::string m_historyDir;
    std::unique_ptr<CLFCommandDispatcher> m_dispatcher;

    // 快捷键状态（第 1 批占位，第 2 批启用 Alt+Enter / 双击退出）
    bool        m_escPending = false;
    bool        m_justInterrupted = false;  // ESC 中断标记（Renderer 剥离 CPR 残留）
    bool        m_needRestoreInput = false; // 中断后需恢复上次提交的输入
    int         m_escCleanupFrames = 0;     // ESC 后持续剥离 CPR 的帧数
    std::string m_lastSubmittedInput;       // 上一次提交的原始输入
    std::thread m_escTimer;
    // 输入历史（第 3 批启用）
    std::vector<std::string> m_inputHistory;
    int m_historyIndex = -1;
};

} // namespace CLF::CLFUI
