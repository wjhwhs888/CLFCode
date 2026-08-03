// CLFRepl.hpp — REPL 交互循环（5 区终端 UI 驱动）
// 负责：按键输入、命令处理、对话提交、确认交互、模式切换
//
// example:
//   CLF::CLFCore::CLFAgentLoop agent(config);
//   CLF::CLFCore::CLFRepl repl(agent, historyDir);
//   return repl.run();

#pragma once

#include <string>

namespace CLF::CLFCore {

class CLFAgentLoop;  // 前向声明（成员为引用，无需完整定义）

class CLFRepl {
public:
    CLFRepl(CLFAgentLoop& agent, const std::string& historyDir);

    // 启动 REPL（含横幅、崩溃恢复检测、按键主循环），返回退出码
    int run();

private:
    // 启动横幅输出到滚动区
    void printBanner();

    // 崩溃恢复检测（询问恢复/丢弃）
    void checkIncompleteSession();

    // 按键主循环的一轮迭代，返回 false 表示退出
    bool handleKey();

    // 提交输入（命令处理或普通对话）
    void submit(const std::string& input);

    // 处理 /xxx 命令，返回 true 表示已处理
    bool handleCommand(const std::string& input);

    // 确认交互（区域 5：上下键选择 + 回车确认）
    bool confirmDialog(const std::string& prompt);

    // 循环切换安全模式（Ctrl+N）
    void cycleMode();

    // 保存会话（正式或 incomplete）
    void saveSession(bool incomplete);

    CLFAgentLoop& m_agent;
    std::string   m_historyDir;
    std::string   m_input;    // 输入缓冲
    std::string   m_modeName; // 当前模式名
};

} // namespace CLF::CLFCore
