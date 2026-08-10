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
    // 思考缓冲按换行切割（快照中提供行列表）
    std::vector<std::string> thinkLines;
    if (!m_thinkingBuffer.empty()) {
        size_t pos = 0;
        while (pos < m_thinkingBuffer.size()) {
            size_t nl = m_thinkingBuffer.find('\n', pos);
            if (nl == std::string::npos) {
                thinkLines.push_back(m_thinkingBuffer.substr(pos));
                break;
            }
            thinkLines.push_back(m_thinkingBuffer.substr(pos, nl - pos));
            pos = nl + 1;
        }
    }
    int elapsed = m_thinkingElapsed;
    if (m_thinkingActive) {
        elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - m_thinkingStart).count());
    }
    auto stylesSnap = m_lineStyles;
    std::vector<std::string> progressSnap;
    {
        std::lock_guard plock(m_progressMutex);
        progressSnap = m_progressLines;
    }
    return {m_contentBuffer, m_pendingLine, std::move(stylesSnap), m_statusText, std::move(progressSnap),
            std::move(thinkLines), m_thinkingActive, m_thinkingBytes, elapsed,
            m_confirmActive, m_confirmPrompt, m_confirmOpts, m_confirmSel};
}

// ---- ICLFOutput 实现 ----

void CLFTerminal::emitContent(const std::string& text) {
    {
        std::lock_guard lock(m_mutex);
        // 正式回复开始 → 思考阶段结束，保存耗时
        if (!text.empty() && m_thinkingActive) {
            m_thinkingActive = false;
            m_thinkingElapsed = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - m_thinkingStart).count());
        }
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
                m_lineStyles.push_back(0); // Normal
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
                m_lineStyles.push_back(0); // Normal
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

void CLFTerminal::emitStyledLine(const std::string& line, LineStyle style) {
    {
        std::lock_guard lock(m_mutex);
        m_contentBuffer.push_back(line);
        m_lineStyles.push_back(static_cast<uint8_t>(style));
    }
    // 不主动 refresh，由后续 emitContent 顺带刷新
}

void CLFTerminal::setStatusTextOnly(const std::string& title) {
    {
        std::lock_guard lock(m_mutex);
        m_statusText = title;
    }
    // 不调 requestRefresh，由其他 emitContent 调用顺便刷新
}

void CLFTerminal::showProgress(const std::vector<std::string>& lines) {
    {
        std::lock_guard lock(m_progressMutex);
        m_progressLines = lines;
    }
    // 不主动刷新，由 emitContent 顺带刷新
}

void CLFTerminal::finishProgress(const std::string& summary) {
    // 先写 summary 到永久缓冲（需要 m_mutex），再清除 progress
    // 保持与 contentSnapshot 一致的锁顺序：m_mutex → m_progressMutex
    if (!summary.empty()) {
        emitContent(summary);
    }
    {
        std::lock_guard lock(m_progressMutex);
        m_progressLines.clear();
    }
    // 空摘要不调 refresh，由后续 emitContent 顺带刷新
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

// ---- 思考内容（与 emitContent 分离，UI 层 Ctrl+O 折叠/展开） ----

void CLFTerminal::appendThinking(const std::string& text) {
    {
        std::lock_guard lock(m_mutex);
        if (m_thinkingBuffer.empty())
            m_thinkingStart = std::chrono::steady_clock::now();
        m_thinkingBuffer += text;
        m_thinkingBytes += text.size();
        m_thinkingActive = true;
    }
    if (!m_refreshPending.exchange(true))
        requestRefresh();
}

void CLFTerminal::clearThinking() {
    {
        std::lock_guard lock(m_mutex);
        m_thinkingBuffer.clear();
        m_thinkingBytes = 0;
        m_thinkingActive = false;
    }
}

bool CLFTerminal::hasThinkingContent() const {
    std::lock_guard lock(m_mutex);
    return !m_thinkingBuffer.empty();
}

std::vector<std::string> CLFTerminal::getThinkingLines() const {
    std::lock_guard lock(m_mutex);
    std::vector<std::string> result;
    if (m_thinkingBuffer.empty()) return result;
    // 简单按换行分割（推理内容通常是一段连续文字）
    size_t pos = 0;
    while (pos < m_thinkingBuffer.size()) {
        size_t nl = m_thinkingBuffer.find('\n', pos);
        if (nl == std::string::npos) {
            result.push_back(m_thinkingBuffer.substr(pos));
            break;
        }
        result.push_back(m_thinkingBuffer.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return result;
}

} // namespace CLF::CLFUI
