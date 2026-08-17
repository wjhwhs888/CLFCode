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

#include "CLFUI/CLFSelectionModel.hpp"
#include "CLFUI/CLFTerminal.hpp"   // ContentSnapshot 值成员需要完整类型

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
    void saveSession(bool isFinal);

    CLF::CLFCore::CLFAgentLoop& m_agent;
    CLF::CLFTypes::ICLFOutput*  m_output;
    std::string m_historyDir;
    std::unique_ptr<CLFCommandDispatcher> m_dispatcher;

    // 快捷键状态
    bool        m_escPending = false;       // Alt+Enter 延迟检测中
    std::chrono::steady_clock::time_point m_escTime;       // Alt+Enter timer 起始时间
    std::chrono::steady_clock::time_point m_lastEscTime;   // 双击退出计时
    bool        m_justInterrupted = false;  // ESC 中断标记（Renderer 剥离 CPR 残留）
    bool        m_needRestoreInput = false; // 中断后需恢复上次提交的输入
    int         m_escCleanupFrames = 0;     // ESC 后持续剥离 CPR 的帧数
    std::string m_lastSubmittedInput;       // 上一次提交的原始输入
    std::thread m_escTimer;                 // Alt+Enter 50ms 延迟线程
    bool        m_showThinking = false;  // Ctrl+T 切换思考过程显示（F7 注释修正）

    // 输入历史（第 3 批）
    std::vector<std::string> m_inputHistory;
    int m_historyIndex = -1;       // -1 = 不在历史浏览中
    std::string m_historyDraft;     // 进入历史前正在编辑的文字（↓到底时恢复）
    // P2-1/R5: 折叠块切换后保持折叠行可见
    bool        m_foldJustToggled = false;
    // P2-3: 时间戳跨日判定（"YYYY-MM-DD"）
    std::string m_lastTsDate;
    // 选区（复制粘贴功能修改 M2）：快照/行映射/行文本每帧重建，主线程独占
    CLFSelectionModel m_selection;
    CLFTerminal::ContentSnapshot m_lastSnapshot;
    std::vector<RowInfo>     m_lastRowMap;
    std::vector<std::string> m_lastRowTexts;
    std::vector<int>         m_lastRowStyles;  // 0=无 1=绿 2=红 3=dim
    // 复制时刻（Ctrl+C/Enter 键盘自动重复防护：复制后短窗内吞重复按键）
    std::chrono::steady_clock::time_point m_lastCopyTime{};
};

} // namespace CLF::CLFUI
