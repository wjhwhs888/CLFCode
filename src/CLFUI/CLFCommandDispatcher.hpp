// CLFCommandDispatcher.hpp — REPL 命令注册表分发器
// 命令注册表模式：新增命令只需注册，不修改已有代码（OCP）
//
// example:
//   CLFCommandDispatcher dispatcher(agent, historyDir, output, onExit);
//   dispatcher.handle("/model");  // 显示模型信息

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace CLF::CLFCore { class CLFAgentLoop; }
namespace CLF::CLFTypes { class ICLFOutput; }

namespace CLF::CLFUI {

// ---- 命令 handler 签名 ----
// 参数: 命令名 (/xxx)、参数字符串（不含命令名，已 trim）、Agent、历史目录、输出
// 返回: true = 已处理
using CLFCommandHandler = std::function<bool(
    const std::string& cmdName,
    const std::string& args,
    CLF::CLFCore::CLFAgentLoop& agent,
    const std::string& historyDir,
    CLF::CLFTypes::ICLFOutput* output)>;

// ---- 命令注册项 ----
struct CLFCommand {
    std::string       m_name;         // "/exit"（含前缀）
    std::string       m_description;  // 帮助文本
    CLFCommandHandler m_handler;
};

class CLFCommandDispatcher {
public:
    CLFCommandDispatcher(CLF::CLFCore::CLFAgentLoop& agent,
                         const std::string& historyDir,
                         CLF::CLFTypes::ICLFOutput* output,
                         std::function<void()> onExit);

    // 注册命令（内部调用，构造时批量注册内置命令）
    void registerCommand(CLFCommand cmd);

    // 处理 /xxx 输入，返回 true 表示已处理（非命令返回 false）
    bool handle(const std::string& input);

    // 获取当前模式名（代理 agent，单一权威源）
    std::string modeName() const;

    // 设置退出回调（screen 创建后注入）
    void setOnExit(std::function<void()> cb) { m_onExit = std::move(cb); }

    // 获取注册表（供 /help 等遍历）
    const std::vector<CLFCommand>& commands() const { return m_commands; }

private:
    CLF::CLFCore::CLFAgentLoop& m_agent;
    std::string m_historyDir;
    CLF::CLFTypes::ICLFOutput* m_output;
    std::function<void()> m_onExit;

    std::vector<CLFCommand> m_commands;  // 注册表（线性搜索，11 个命令性能充足）
};

// 注册所有内置斜杠命令（由 CLFCommandDispatcher 构造时调用）
void registerBuiltinCommands(CLFCommandDispatcher& dispatcher);

} // namespace CLF::CLFUI
