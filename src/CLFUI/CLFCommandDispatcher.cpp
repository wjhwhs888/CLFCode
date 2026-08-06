// CLFCommandDispatcher.cpp — REPL 命令分发实现（注册表模式）

#include "CLFUI/CLFCommandDispatcher.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFCore/CLFAgentLoop.hpp"

#include <string>

namespace CLF::CLFUI {

CLFCommandDispatcher::CLFCommandDispatcher(CLF::CLFCore::CLFAgentLoop& agent,
                                           const std::string& historyDir,
                                           CLF::CLFTypes::ICLFOutput* output,
                                           std::function<void()> onExit)
    : m_agent(agent)
    , m_historyDir(historyDir)
    , m_output(output)
    , m_onExit(std::move(onExit)) {
    registerBuiltinCommands(*this);
}

std::string CLFCommandDispatcher::modeName() const {
    return m_agent.getSecurityModeName();
}

void CLFCommandDispatcher::registerCommand(CLFCommand cmd) {
    m_commands.push_back(std::move(cmd));
}

bool CLFCommandDispatcher::handle(const std::string& input) {
    // 非命令 → 不处理
    if (input.empty() || input[0] != '/') return false;

    // 拆分命令名和参数
    auto spacePos = input.find(' ');
    std::string cmdName = (spacePos == std::string::npos)
        ? input
        : input.substr(0, spacePos);
    std::string args;
    if (spacePos != std::string::npos) {
        args = input.substr(spacePos + 1);
        // trim leading spaces
        size_t start = args.find_first_not_of(' ');
        args = (start == std::string::npos) ? "" : args.substr(start);
    }

    // 查表路由
    for (auto& cmd : m_commands) {
        if (cmd.m_name == cmdName) {
            bool handled = cmd.m_handler(cmdName, args, m_agent, m_historyDir, m_output);
            // /exit 特殊处理：handler 返回后调用 onExit
            if (handled && cmdName == "/exit" && m_onExit) {
                m_onExit();
            }
            return handled;
        }
    }
    return false;
}

} // namespace CLF::CLFUI
