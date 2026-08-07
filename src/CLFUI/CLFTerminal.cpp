// CLFTerminal.cpp — ICLFOutput 实现 (FTXUI 驱动)
#include "CLFUI/CLFTerminal.hpp"
#include "CLFUI/CLFAnsi.hpp"

#ifdef _WIN32
#include <windows.h>
#undef min
#undef max
#endif

namespace CLF::CLFUI {

using namespace ftxui;

// ========== 静态工具 (委托 CLFAnsi) ==========

void CLFTerminal::enableAnsi() { CLFAnsi::enable(); }
std::string CLFTerminal::cyan(const std::string& s)  { return CLFAnsi::cyan(s); }
std::string CLFTerminal::red(const std::string& s)   { return CLFAnsi::red(s); }
std::string CLFTerminal::gray(const std::string& s)  { return CLFAnsi::gray(s); }
std::string CLFTerminal::bold(const std::string& s)  { return CLFAnsi::bold(s); }
int CLFTerminal::getTerminalHeight() { return CLFAnsi::terminalHeight(); }
int CLFTerminal::getTerminalWidth()  { return CLFAnsi::terminalWidth(); }
std::string CLFTerminal::diagnosticInfo() {
    return "终端: 高" + std::to_string(getTerminalHeight())
         + " x 宽" + std::to_string(getTerminalWidth())
         + ", ANSI: " + (CLFAnsi::isEnabled() ? "开" : "关");
}

// ========== FTXUI 核心 ==========

CLFTerminal::~CLFTerminal() {
    if (m_screen) m_screen->ExitLoopClosure()();
}

void CLFTerminal::requestRefresh() {
    if (m_screen) m_screen->PostEvent(Event::Custom);
}

// ---- 线程安全快照 ----

CLFTerminal::ContentSnapshot CLFTerminal::contentSnapshot() const {
    std::lock_guard lock(m_mutex);
    return {m_contentBuffer, m_pendingLine, m_statusText,
            m_confirmActive, m_confirmPrompt, m_confirmOpts, m_confirmSel};
}

// ---- ICLFOutput 实现 ----

void CLFTerminal::emitContent(const std::string& text) {
    {
        std::lock_guard lock(m_mutex);
        for (char c : text) {
            // 实时过滤 ANSI: 防止裸 ESC 码进入 m_pendingLine 被 Renderer 渲染
            if (m_inAnsiSeq) {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
                    m_inAnsiSeq = false;
                continue;
            }
            if (c == '\033') { m_inAnsiSeq = true; continue; }
            if (c == '\n') {
                m_contentBuffer.push_back(m_pendingLine);
                m_pendingLine.clear();
            } else {
                m_pendingLine += c;
            }
        }
    }
    if (!m_refreshPending.exchange(true))
        requestRefresh();
}

void CLFTerminal::emitRaw(const std::string& data) {
    {
        std::lock_guard lock(m_mutex);
        // flush m_pendingLine (切换输出模式, 防混合)
        if (!m_pendingLine.empty()) {
            m_contentBuffer.push_back(m_pendingLine);
            m_pendingLine.clear();
        }
        for (char c : data) {
            if (c == '\n') {
                m_contentBuffer.push_back(m_pendingLine);
                m_pendingLine.clear();
            } else {
                m_pendingLine += c;
            }
        }
    }
    if (!m_refreshPending.exchange(true))
        requestRefresh();
}

void CLFTerminal::setStatus(const std::string& title, int cur, int total) {
    {
        std::lock_guard lock(m_mutex);
        if (title.empty()) {
            m_statusText.clear();
        } else if (cur >= 0 && total > 0) {
            m_statusText = title + " (" + std::to_string(cur) + "/"
                         + std::to_string(total) + ")";
        } else {
            m_statusText = title;
        }
    }
    requestRefresh();
}

bool CLFTerminal::confirm(const std::string& prompt) {
    if (!m_screen) return false;
    {
        std::lock_guard lock(m_mutex);
        m_confirmPrompt = prompt;
        m_confirmOpts = {"确认", "返回"};
        m_confirmSel   = 0;
        m_confirmResult = false;
        m_confirmActive = true;
    }
    requestRefresh();

    // 同步等待主线程 (FTXUI Loop) 处理用户选择
    std::unique_lock lock(m_confirmMutex);
    m_confirmCv.wait(lock, [this] { return !m_confirmActive; });

    return m_confirmResult;
}

void CLFTerminal::onInterrupt(std::function<void()> cb) {
    m_interruptCb = std::move(cb);
}

void CLFTerminal::emitError(const std::string& msg) {
    emitContent(red("✗ ") + msg);
}

} // namespace CLF::CLFUI
