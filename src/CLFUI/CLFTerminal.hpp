// CLFTerminal.hpp — 终端 UI (FTXUI 组件树 + ICLFOutput 实现)
// 渲染核心由 FTXUI 全帧驱动, CLFTerminal 只管理状态 + 实现 ICLFOutput

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "CLFTypes/ICLFOutput.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

namespace CLF::CLFUI {

class CLFRepl;

class CLFTerminal : public CLF::CLFTypes::ICLFOutput {
public:
    CLFTerminal() = default;
    ~CLFTerminal();

    // === FTXUI 入口 ===
    void setScreen(ftxui::ScreenInteractive* screen) { m_screen = screen; }
    void requestRefresh();

    // === 静态工具 (CLFAnsi 委托, emitContent 会自动过滤 ANSI) ===
    static void enableAnsi();
    static std::string green(const std::string& s);
    static std::string cyan(const std::string& s);
    static std::string yellow(const std::string& s);
    static std::string red(const std::string& s);
    static std::string gray(const std::string& s);
    static std::string bold(const std::string& s);
    static int  getTerminalHeight();
    static int  getTerminalWidth();
    static std::string diagnosticInfo();

    // === ICLFOutput 实现 ===
    void emitContent(const std::string& t) override;
    void emitRaw(const std::string& d) override;
    void setStatus(const std::string& title, int cur=-1, int total=-1) override;
    void onToolCall(const std::string& n, const std::string& p) override;
    void onToolResult(const std::string& n, const std::string& r, bool ok) override;
    bool confirm(const std::string& prompt) override;
    int  askSelect(const std::vector<std::string>& opts, const std::string& p) override;
    std::optional<std::string> askInput(const std::string& p, const std::string& d) override;
    void onInterrupt(std::function<void()> cb) override;
    void emitError(const std::string& m) override;
    void requestShutdown(const std::string& reason) override;

    // === 状态查询 ===
    bool isShutdownRequested() const { return m_shutdownRequested; }
    void setRepl(CLFRepl* repl) { m_repl = repl; }

    // === 组件状态 (public — Renderer lambdas 需要访问) ===
    std::vector<std::string> m_contentBuffer;
    std::string  m_pendingLine;
    bool         m_inAnsiSeq = false;
    std::string  m_statusText;
    // confirm 弹窗 (Modal 组件使用)
    std::string  m_confirmPrompt;
    std::vector<std::string> m_confirmOpts;
    int          m_confirmSel = 0;
    bool         m_confirmActive = false;
    bool         m_confirmResult = false;
    std::mutex   m_confirmMutex;
    std::condition_variable m_confirmCv;
    // 中断 / 刷新 / 退出
    std::function<void()> m_interruptCb;
    std::atomic<bool>     m_refreshPending{false};
    bool                  m_shutdownRequested = false;

private:
    ftxui::ScreenInteractive* m_screen = nullptr;
    CLFRepl*  m_repl = nullptr;
    std::mutex m_mutex;         // 保护 contentBuffer + pendingLine
};

} // namespace CLF::CLFUI
