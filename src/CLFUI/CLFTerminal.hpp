// CLFTerminal.hpp — 终端 UI (FTXUI 组件树 + ICLFOutput 实现)
// 保留静态颜色/控制方法兼容旧代码; 渲染核心由 FTXUI 全帧驱动

#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "CLFTypes/ICLFOutput.hpp"

#include <ftxui/component/component.hpp>      // Component, Renderer, Input...
#include <ftxui/component/screen_interactive.hpp> // ScreenInteractive

namespace CLF::CLFUI {

class CLFRepl;
class CLFScrollBuffer;

class CLFTerminal : public CLF::CLFTypes::ICLFOutput {
public:
    CLFTerminal() = default;
    ~CLFTerminal();

    // === FTXUI 入口 ===
    void setScreen(ftxui::ScreenInteractive* screen) { m_screen = screen; }
    ftxui::Component buildUI(class CLFRepl* repl);  // 构建组件树
    void requestRefresh();                            // 触发重渲染

    // === 兼容旧 API (逐步迁移) ===
    static void drawInputArea(const std::string& text, int cp = -1) { drawInput(text, cp); }
    static void drawModeArea(const std::string& m)   { drawMode(m); }
    static void drawConfirmArea(const std::vector<std::string>& o, int s) { showConfirm(o, s); }
    static void clearConfirmArea() { hideConfirm(); }

    // === 静态工具 (兼容旧代码) ===
    static void enableAnsi();
    static std::string green(const std::string& s);
    static std::string cyan(const std::string& s);
    static std::string lightBlue(const std::string& s);
    static std::string yellow(const std::string& s);
    static std::string red(const std::string& s);
    static std::string gray(const std::string& s);
    static std::string bold(const std::string& s);
    static int getTerminalHeight();
    static int getTerminalWidth();
    static int textWidth(const std::string& text);
    static int wrappedLines(const std::string& text);
    static void scrollPrint(const std::string& text);
    static void thoughtMark(int seconds, int searchCount = 0, int readCount = 0);
    static void showThinking(int seconds);
    static void showWorking(const std::string& title);
    static void showTaskTree(const std::vector<std::string>& phases);
    static void clearStatus();
    static void drawInput(const std::string& text, int cursorPos = -1);
    static void drawMode(const std::string& mode);
    static void showConfirm(const std::vector<std::string>& options, int selected);
    static void hideConfirm();
    static void initLayout(const std::string& modeLabel);
    static void redrawAll();
    static void restoreScrollRegion();
    static std::string diagnosticInfo();
    static void drawStatusArea(const std::string& t, const std::string& c);
    static void toContentArea();

    // === ICLFOutput 实现 (FTXUI 驱动) ===
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
    bool isShutdownRequested() const { return m_shutdownRequested; }
    void setRepl(CLFRepl* repl) { m_repl = repl; }

    // === FTXUI 组件状态 (public, Renderer lambda 需要访问) ===
    std::vector<std::string> m_contentBuffer;
    std::string m_pendingLine;               // 跨 emitContent 调用的未完成行
    std::string m_statusText;
    std::string m_inputText;
    std::string m_modeText;
    std::vector<std::string> m_confirmOpts;
    int m_confirmSel = 0;
    bool m_confirmActive = false;
    bool m_confirmResult = false;
    std::function<void()> m_interruptCb;
    std::atomic<bool> m_refreshPending{false};
    bool m_shutdownRequested = false;

private:
    ftxui::ScreenInteractive* m_screen = nullptr;
    CLFRepl* m_repl = nullptr;
    std::mutex m_mutex;
};

} // namespace CLF::CLFUI
