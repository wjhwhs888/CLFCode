// CLFRepl.hpp — REPL 交互循环 (FTXUI 全帧驱动)
// 命令分发 → CLFCommandDispatcher; 输入+渲染 → FTXUI Loop + Modal
//
// example:
//   CLFAgentLoop agent(config);
//   CLFRepl repl(agent, historyDir);
//   return repl.run();

#pragma once

#include <memory>
#include <string>

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
};

} // namespace CLF::CLFUI
