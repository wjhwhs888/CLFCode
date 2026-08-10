// CLFTerminal.hpp — 终端 UI (FTXUI 组件树 + ICLFOutput 实现)
// 渲染核心由 FTXUI 全帧驱动, CLFTerminal 只管理状态 + 实现 ICLFOutput

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "CLFTypes/ICLFOutput.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

namespace CLF::CLFUI {

class CLFTerminal : public CLF::CLFTypes::ICLFOutput {
public:
    CLFTerminal() = default;
    ~CLFTerminal();

    // === FTXUI 入口 ===
    void setScreen(ftxui::ScreenInteractive* screen) { m_screen = screen; }
    void requestRefresh();

    // === 静态工具 (委托 CLFAnsi) ===
    static void enableAnsi();
    static std::string cyan(const std::string& s);
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
    void setStatusTextOnly(const std::string& title) override;
    void showProgress(const std::vector<std::string>& lines) override;
    void finishProgress(const std::string& summary) override;
    bool confirm(const std::string& prompt) override;
    void onInterrupt(std::function<void()> cb) override;
    void emitError(const std::string& m) override;

    // === ICLFOutput ⑥ 思考内容 ===
    void appendThinking(const std::string& text) override;
    void clearThinking() override;
    bool hasThinkingContent() const;
    std::vector<std::string> getThinkingLines() const;

    // 线程安全：confirm 工作线程写 / 主线程 CatchEvent 读
    bool isConfirmActive() const { std::lock_guard lock(m_mutex); return m_confirmActive; }
    void setConfirmActive(bool v) { std::lock_guard lock(m_mutex); m_confirmActive = v; }

    // === 线程安全快照 (Renderer 使用, 一次加锁拷贝) ===
    struct ContentSnapshot {
        std::vector<std::string> lines;
        std::string pendingLine;
        std::string statusText;
        std::vector<std::string> progressLines; // 渐进式进度块
        // 思考内容（折叠/展开用）
        std::vector<std::string> thinkingLines;
        bool thinkingActive = false;
        size_t thinkingBytes = 0;
        int  thinkingElapsed = 0;  // 思考已持续秒数
        // confirm
        bool confirmActive = false;
        std::string confirmPrompt;
        std::vector<std::string> confirmOpts;
        int  confirmSel = 0;
    };
    ContentSnapshot contentSnapshot() const;

    // === 组件状态 (public — Renderer / ConfirmBar 需要访问) ===
    std::vector<std::string> m_contentBuffer;
    std::string  m_pendingLine;
    bool         m_inAnsiSeq = false;
    // 思考缓冲（与 content 分离，Ctrl+T 折叠/展开）
    std::string  m_thinkingBuffer;
    bool         m_thinkingActive = false;
    size_t       m_thinkingBytes = 0;
    int          m_thinkingElapsed = 0;  // 思考总耗时（秒）
    std::chrono::steady_clock::time_point m_thinkingStart;
    std::string  m_statusText;
    std::vector<std::string> m_progressLines;
    mutable std::mutex m_progressMutex;
    // confirm (由 CLFTerminal::confirm + CLFConfirmBar 共同操作)
    std::string  m_confirmPrompt;
    std::vector<std::string> m_confirmOpts;
    int          m_confirmSel = 0;
    bool         m_confirmActive = false;
    bool         m_confirmResult = false;
    std::mutex   m_confirmMutex;
    std::condition_variable m_confirmCv;
    // 中断 / 刷新
    std::function<void()> m_interruptCb;
    std::atomic<bool>     m_refreshPending{false};

private:
    ftxui::ScreenInteractive* m_screen = nullptr;
    mutable std::mutex m_mutex;  // 保护 m_contentBuffer + m_pendingLine + m_statusText + confirm 字段
};

} // namespace CLF::CLFUI
