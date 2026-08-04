// CLFCommandDispatcher.hpp — REPL 命令注册表分发器
// 命令注册表模式：新增命令只需注册，不修改已有代码（OCP）
//
// example:
//   CLFCommandDispatcher dispatcher(agent, historyDir);
//   dispatcher.handle("/model");  // 显示模型信息

#pragma once

#include <functional>
#include <string>

namespace CLF::CLFCore { class CLFAgentLoop; }

namespace CLF::CLFUI {

class CLFCommandDispatcher {
public:
    CLFCommandDispatcher(CLF::CLFCore::CLFAgentLoop& agent, const std::string& historyDir);

    // 处理 /xxx 命令，返回 true 表示已处理（非命令返回 false）
    bool handle(const std::string& input);

    // 获取/设置当前模式名（/mode 命令修改）
    const std::string& modeName() const { return m_modeName; }
    void setModeName(const std::string& name) { m_modeName = name; }

private:
    CLF::CLFCore::CLFAgentLoop& m_agent;
    std::string m_historyDir;
    std::string m_modeName;
};

} // namespace CLF::CLFUI
