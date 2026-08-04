// CLFRepl.hpp — REPL 交互循环（5 区终端 UI 驱动）
// 命令分发 → CLFCommandDispatcher；按键循环 + 提交 + 确认 + 模式切换
//
// example:
//   CLFAgentLoop agent(config);
//   CLFRepl repl(agent, historyDir);
//   return repl.run();

#pragma once

#include <memory>
#include <string>

namespace CLF::CLFCore { class CLFAgentLoop; class CLFEventQueue; }
namespace CLF::CLFTypes { class ICLFOutput; }

namespace CLF::CLFUI {

class CLFCommandDispatcher;

class CLFRepl {
public:
    CLFRepl(CLF::CLFCore::CLFAgentLoop& agent, const std::string& historyDir,
            CLF::CLFTypes::ICLFOutput* output = nullptr);
    ~CLFRepl();

    int run();
    bool confirmDialog(const std::string& prompt);
    void submit(const std::string& input);   // FTXUI 回调
    void cycleMode();                        // FTXUI 回调
    std::string& inputText() { return m_input; }

private:
    void printBanner();
    void checkIncompleteSession();
    void saveSession(bool incomplete);

    CLF::CLFCore::CLFAgentLoop& m_agent;
    CLF::CLFTypes::ICLFOutput* m_output;
    std::string m_historyDir;
    std::string m_input;
    int m_cursorPos = 0;
    std::unique_ptr<CLFCommandDispatcher> m_dispatcher;
    std::unique_ptr<CLF::CLFCore::CLFEventQueue> m_eventQueue;
    bool m_exit = false;
    int m_lastHeight = -1;
};

} // namespace CLF::CLFUI
