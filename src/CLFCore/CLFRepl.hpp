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

namespace CLF::CLFCore {

class CLFAgentLoop;
class CLFCommandDispatcher;

class CLFRepl {
public:
    CLFRepl(CLFAgentLoop& agent, const std::string& historyDir);
    ~CLFRepl();

    int run();

private:
    void printBanner();
    void checkIncompleteSession();
    void submit(const std::string& input);
    bool confirmDialog(const std::string& prompt);
    void cycleMode();
    void saveSession(bool incomplete);

    CLFAgentLoop& m_agent;
    std::string m_historyDir;
    std::string m_input;
    std::unique_ptr<CLFCommandDispatcher> m_dispatcher;
};

} // namespace CLF::CLFCore
