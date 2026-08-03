// CLFTerminal.cpp — 终端 UI 工具实现（DECSTBM 5 区布局）
// 颜色/尺寸 → 委托 CLFAnsi；滚动缓冲 → 委托 CLFScrollBuffer

#include "CLFCore/CLFTerminal.hpp"
#include "CLFCore/CLFAnsi.hpp"
#include "CLFCore/CLFScrollBuffer.hpp"

#include <algorithm>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#undef min
#undef max
#endif

namespace CLF::CLFCore {

// 静态状态
CLFScrollBuffer CLFTerminal::s_buffer;
std::string CLFTerminal::s_statusTitle;
std::string CLFTerminal::s_statusContent;
std::string CLFTerminal::s_inputText;
std::string CLFTerminal::s_modeLabel;
int  CLFTerminal::s_inputCursor = 0;
bool CLFTerminal::s_inputDrawn = false;
bool CLFTerminal::s_confirmDrawn = false;

namespace {

constexpr int kFixedLines = 7;
int contentBottom(int H) { return H - kFixedLines; }
int inputRow(int H)      { return H - 2; }
int modeRow(int H)       { return H; }

std::string truncateToWidth(const std::string& text, int maxWidth) {
    if (CLFAnsi::textWidth(text) <= maxWidth) return text;
    std::string result = text;
    while (CLFAnsi::textWidth(result) > maxWidth) {
        size_t len = 1;
        while (len < result.size()
               && (static_cast<unsigned char>(result[result.size() - len]) & 0xC0) == 0x80)
            ++len;
        result.erase(result.size() - len);
    }
    return result;
}

std::string stripAnsi(const std::string& text) {
    std::string result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\033') {
            ++i;
            if (i < text.size() && text[i] == '[') {
                ++i;
                while (i < text.size()
                       && !((text[i] >= 'a' && text[i] <= 'z')
                            || (text[i] >= 'A' && text[i] <= 'Z'))) ++i;
            }
        } else result += text[i];
    }
    return result;
}

void setScrollRegion(int top, int bottom) {
    std::cout << "\033[" << top << ";" << bottom << "r" << std::flush;
}
void resetScrollRegion() {
    std::cout << "\033[r" << std::flush;
}

void drawModeLine(const std::string& mode) {
    int W = CLFAnsi::terminalWidth();
    if (W <= 0) W = 80;
    std::string left  = mode + " mode on";
    std::string hints = "shift+tab to cycle · esc to interrupt · ? for help";
    constexpr int kIndent = 2;
    int pad = W - kIndent - CLFAnsi::textWidth(left) - CLFAnsi::textWidth(hints);
    if (pad < 0) pad = 1;
    std::string display = truncateToWidth(left + std::string(pad, ' ') + hints, W - kIndent);
    std::cout << std::string(kIndent, ' ') << CLFAnsi::gray(display) << std::flush;
}

void redrawFixedArea(int W, int H, const std::string& inputText) {
    std::cout << "\033[" << (H - 4) << ";1H\033[K" << std::flush;
    std::cout << "\033[" << (H - 3) << ";1H"
              << CLFAnsi::lightBlue(std::string(W - 1, '-')) << std::flush;
    std::cout << "\033[" << (H - 2) << ";1H"
              << "❯ " << inputText << "\033[K" << std::flush;
    std::cout << "\033[" << (H - 1) << ";1H"
              << CLFAnsi::lightBlue(std::string(W - 1, '-')) << std::flush;
    std::cout << "\033[" << H << ";1H\033[K" << std::flush;
}

} // anonymous namespace

// ============================================================================
// ANSI 颜色 — 委托 CLFAnsi
// ============================================================================

void CLFTerminal::enableAnsi() { CLFAnsi::enable(); }

std::string CLFTerminal::green(const std::string& s)     { return CLFAnsi::green(s); }
std::string CLFTerminal::cyan(const std::string& s)      { return CLFAnsi::cyan(s); }
std::string CLFTerminal::lightBlue(const std::string& s) { return CLFAnsi::lightBlue(s); }
std::string CLFTerminal::yellow(const std::string& s)    { return CLFAnsi::yellow(s); }
std::string CLFTerminal::red(const std::string& s)       { return CLFAnsi::red(s); }
std::string CLFTerminal::gray(const std::string& s)      { return CLFAnsi::gray(s); }
std::string CLFTerminal::bold(const std::string& s)      { return CLFAnsi::bold(s); }

// ============================================================================
// 树状输出
// ============================================================================

void CLFTerminal::item(const std::string& text) { std::cout << "● " << text << std::endl; }
void CLFTerminal::sub(const std::string& text)  { std::cout << "  ⎿ " << text << std::endl; }
void CLFTerminal::sub2(const std::string& text) { std::cout << "    ⎿ " << text << std::endl; }
void CLFTerminal::ok(const std::string& text)   { std::cout << "● " << green("✓") << " " << text << std::endl; }
void CLFTerminal::fail(const std::string& text) { std::cout << "● " << red("✗") << " " << text << std::endl; }
void CLFTerminal::info(const std::string& text) { std::cout << "● " << yellow("⚠") << " " << text << std::endl; }

// ============================================================================
// 终端控制 — 委托 CLFAnsi
// ============================================================================

int CLFTerminal::getTerminalHeight() { return CLFAnsi::terminalHeight(); }
int CLFTerminal::getTerminalWidth()  { return CLFAnsi::terminalWidth(); }
int CLFTerminal::textWidth(const std::string& text) { return CLFAnsi::textWidth(text); }
int CLFTerminal::wrappedLines(const std::string& text) { return CLFAnsi::wrappedLines(text); }

void CLFTerminal::moveCursor(int row, int col) {
    if (!CLFAnsi::isEnabled()) return;
    std::cout << "\033[" << row << ";" << col << "H" << std::flush;
}
void CLFTerminal::clearLine() {
    if (!CLFAnsi::isEnabled()) return;
    std::cout << "\033[K" << std::flush;
}

// ============================================================================
// DECSTBM 滚动区布局
// ============================================================================

void CLFTerminal::initLayout(const std::string& modeLabel) {
    s_modeLabel = modeLabel;
    s_inputText.clear();
    s_inputCursor = 0;
    s_inputDrawn = false;
    s_confirmDrawn = false;
    s_buffer.clear();

    int H = getTerminalHeight();
    int W = getTerminalWidth();
    if (H <= 0) H = 30;
    if (W <= 0) W = 80;

    if (!CLFAnsi::isEnabled() || H < 10) {
        std::cout << "\033[2J\033[H" << std::flush;
        return;
    }

    std::cout << "\033[2J\033[H" << std::flush;

    int cb = contentBottom(H);
    setScrollRegion(1, cb);

    resetScrollRegion();
    redrawFixedArea(W, H, s_inputText);
    drawModeLine(s_modeLabel);

    setScrollRegion(1, cb);
    std::cout << "\033[H" << std::flush;

    s_inputDrawn = true;
}

void CLFTerminal::setScrollCollapsed(bool) {}
bool CLFTerminal::isScrollCollapsed() { return false; }

void CLFTerminal::scrollPrint(const std::string& text) {
    s_buffer.append(text);
    std::cout << text << std::flush;
}

void CLFTerminal::scrollAppend(const std::string& text) {
    scrollPrint(text);
}

void CLFTerminal::drawStatusArea(const std::string& title, const std::string& content) {
    s_statusTitle = title;
    s_statusContent = content;

    int W = getTerminalWidth();
    if (W <= 0) W = 80;

    if (!title.empty()) {
        scrollPrint(lightBlue("▍ ") + bold(truncateToWidth(title, W - 3)) + "\n");
    }
    if (!content.empty()) {
        std::string display = truncateToWidth(content, W - 5);
        if (display != content) display += "...";
        scrollPrint("  ⎿ " + gray(display) + "\n");
    }
}

void CLFTerminal::drawInputArea(const std::string& text, int cursorPos) {
    s_inputText = text;
    int cur = (cursorPos < 0) ? static_cast<int>(text.size()) : cursorPos;
    s_inputCursor = cur;

    int H = getTerminalHeight();
    int W = getTerminalWidth();
    if (H <= 0) H = 30;
    if (W <= 0) W = 80;

    if (!CLFAnsi::isEnabled() || H < 10) {
        if (!s_inputDrawn) {
            std::cout << "\n\n" << std::flush;
            std::cout << lightBlue(std::string(W - 1, '-')) << "\n" << std::flush;
            std::cout << "❯ " << text << "\n" << std::flush;
            std::cout << lightBlue(std::string(W - 1, '-')) << "\n" << std::flush;
            drawModeLine(s_modeLabel);
            s_inputDrawn = true;
            std::cout << "\033[3A\r" << std::flush;
        } else {
            std::cout << "\r❯ " << text << "\033[K" << std::flush;
        }
        int col = 1 + textWidth("❯ ") + textWidth(text.substr(0, static_cast<size_t>(cur)));
        std::cout << "\033[" << col << "G" << std::flush;
        return;
    }

    if (!s_inputDrawn) {
        std::cout << "\033[2J\033[H" << std::flush;
        int cb = contentBottom(H);
        setScrollRegion(1, cb);
        resetScrollRegion();
        redrawFixedArea(W, H, s_inputText);
        drawModeLine(s_modeLabel);
        setScrollRegion(1, cb);
        std::cout << "\033[H" << std::flush;
        s_inputDrawn = true;
    }

    std::cout << "\033[" << inputRow(H) << ";1H"
              << "\r❯ " << text << "\033[K" << std::flush;

    std::string prefix = text.substr(0, static_cast<size_t>(cur));
    int col = 1 + textWidth("❯ ") + textWidth(prefix);
    std::cout << "\033[" << inputRow(H) << ";" << col << "H" << std::flush;
}

void CLFTerminal::drawModeArea(const std::string& mode) {
    if (s_modeLabel == mode) return;
    s_modeLabel = mode;
    if (!s_inputDrawn || !CLFAnsi::isEnabled()) return;

    int H = getTerminalHeight();
    if (H < 10) return;

    std::cout << "\0337"
              << "\033[" << modeRow(H) << ";1H\033[K"
              << std::flush;
    drawModeLine(mode);
    std::cout << "\0338" << std::flush;
}

void CLFTerminal::drawConfirmArea(const std::vector<std::string>& options, int selected) {
    if (s_confirmDrawn && CLFAnsi::isEnabled()) {
        std::cout << "\033[2A" << std::flush;
    }
    s_confirmDrawn = true;

    std::cout << "\r\033[K" << yellow("⚠ ") << "请确认操作：" << "\n" << std::flush;

    std::string display;
    for (size_t i = 0; i < options.size(); ++i) {
        if (i > 0) display += "    ";
        if (static_cast<int>(i) == selected)
            display += "[" + green("●") + "] " + bold(options[i]);
        else
            display += "[ ] " + gray(options[i]);
    }
    std::cout << "\r\033[K" << display << "\n" << std::flush;
}

void CLFTerminal::clearConfirmArea() {}

void CLFTerminal::toContentArea() {
    int H = getTerminalHeight();
    if (H <= 0) H = 30;

    if (!CLFAnsi::isEnabled() || H < 10) {
        std::cout << "\r\033[K\n\033[K\n\033[K\n" << std::flush;
        s_inputDrawn = false;
        s_confirmDrawn = false;
        return;
    }

    int W = getTerminalWidth();
    if (W <= 0) W = 80;
    std::cout << "\033[" << inputRow(H) << ";1H" << std::flush;
    std::cout << "\r\033[K" << std::flush;
    std::cout << "\033[" << (H - 1) << ";1H" << std::flush;
    std::cout << lightBlue(std::string(W - 1, '-')) << std::flush;

    std::cout << "\033[" << contentBottom(H) << ";1H" << std::flush;
    std::cout << "\n" << std::flush;
    s_confirmDrawn = false;
}

void CLFTerminal::redrawAll() {
    int H = getTerminalHeight();
    int W = getTerminalWidth();
    if (H <= 0) H = 30;
    if (W <= 0) W = 80;

    if (!CLFAnsi::isEnabled() || H < 10) {
        std::cout << "\033[2J\033[H" << std::flush;
        for (const auto& line : s_buffer.lines()) {
            std::cout << line << "\n" << std::flush;
        }
        s_inputDrawn = false;
        drawInputArea(s_inputText, s_inputCursor);
        return;
    }

    std::cout << "\033[2J\033[H" << std::flush;

    int cb = contentBottom(H);
    resetScrollRegion();
    setScrollRegion(1, cb);

    const auto& lines = s_buffer.lines();
    size_t visible = static_cast<size_t>(cb - 1);
    size_t start = (lines.size() > visible) ? lines.size() - visible : 0;
    for (size_t i = start; i < lines.size(); ++i) {
        std::string stripped = stripAnsi(lines[i]);
        if (textWidth(stripped) <= W - 1) {
            std::cout << lines[i] << "\n" << std::flush;
        } else {
            std::cout << truncateToWidth(stripped, W - 1) << "\n" << std::flush;
        }
    }

    resetScrollRegion();
    redrawFixedArea(W, H, s_inputText);
    drawModeLine(s_modeLabel);

    setScrollRegion(1, cb);
    std::cout << "\033[H" << std::flush;

    s_inputDrawn = true;
    drawInputArea(s_inputText, s_inputCursor);
}

std::string CLFTerminal::diagnosticInfo() {
    int H = getTerminalHeight();
    int W = getTerminalWidth();
    return "终端: 高" + std::to_string(H) + " x 宽" + std::to_string(W)
         + ", ANSI: " + (CLFAnsi::isEnabled() ? "开" : "关");
}

void CLFTerminal::thoughtMark(int seconds, int searchCount, int readCount) {
    if (seconds <= 0) return;
    std::string msg = "  Thought for " + std::to_string(seconds) + "s";
    if (searchCount > 0) msg += ", searched for " + std::to_string(searchCount) + " pattern(s)";
    if (readCount > 0)   msg += ", read " + std::to_string(readCount) + " file(s)";
    scrollPrint("\n" + gray(msg) + "\n");
}

void CLFTerminal::restoreScrollRegion() {
    resetScrollRegion();
    if (CLFAnsi::isEnabled()) {
        std::cout << "\033[2J\033[H" << std::flush;
    }
}

} // namespace CLF::CLFCore
