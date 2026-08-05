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
std::string CLFTerminal::green(const std::string& s) { return CLFAnsi::green(s); }
std::string CLFTerminal::cyan(const std::string& s)  { return CLFAnsi::cyan(s); }
std::string CLFTerminal::yellow(const std::string& s){ return CLFAnsi::yellow(s); }
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

// ---- 内部: ANSI 过滤 ----

static std::string stripAnsi(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !((s[i] >= 'a' && s[i] <= 'z') ||
                                     (s[i] >= 'A' && s[i] <= 'Z'))) ++i;
        } else {
            r += s[i];
        }
    }
    return r;
}

// ---- ICLFOutput 映射 ----

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
            m_contentBuffer.push_back(stripAnsi(m_pendingLine));
            m_pendingLine.clear();
        }
        for (char c : data) {
            if (c == '\n') {
                m_contentBuffer.push_back(m_pendingLine);  // 保留原始 ANSI
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
    if (title.empty()) {
        m_statusText.clear();
    } else if (cur >= 0 && total > 0) {
        m_statusText = title + " (" + std::to_string(cur) + "/"
                     + std::to_string(total) + ")";
    } else {
        m_statusText = title;
    }
    requestRefresh();
}

void CLFTerminal::onToolCall(const std::string& name, const std::string& params) {
    emitContent("● " + name + "(" + params + ")");
}

void CLFTerminal::onToolResult(const std::string&, const std::string& result, bool ok) {
    emitContent("  ⎿ " + std::string(ok ? "✓ " : "✗ ") + result);
}

bool CLFTerminal::confirm(const std::string& prompt) {
    if (!m_screen) return false;
    // 设置 Modal 弹窗状态
    m_confirmPrompt = prompt;
    m_confirmOpts = {"确认", "取消"};
    m_confirmSel   = 0;
    m_confirmResult = false;
    m_confirmActive = true;
    requestRefresh();

    // 同步等待主线程 (FTXUI Loop) 处理用户选择
    std::unique_lock lock(m_confirmMutex);
    m_confirmCv.wait(lock, [this] { return !m_confirmActive; });

    return m_confirmResult;
}

int CLFTerminal::askSelect(const std::vector<std::string>&, const std::string&) {
    return -1;
}

std::optional<std::string> CLFTerminal::askInput(const std::string&, const std::string&) {
    return std::nullopt;
}

void CLFTerminal::onInterrupt(std::function<void()> cb) {
    m_interruptCb = std::move(cb);
}

void CLFTerminal::emitError(const std::string& msg) {
    emitContent(red("✗ ") + msg);
}

void CLFTerminal::requestShutdown(const std::string& reason) {
    emitContent("[FATAL] " + reason + "\n");
    m_shutdownRequested = true;
}

} // namespace CLF::CLFUI
