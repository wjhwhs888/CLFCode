// CLFTerminal.cpp — 终端 UI（Claude Code 风格多行输入区）
// 颜色/尺寸 → 委托 CLFAnsi；滚动缓冲 → 委托 CLFScrollBuffer
//
// 布局（从底向上）：
//   Row H:     [状态行]  mode · hints
//   Row H-1:   [下分隔线]
//   Row H-2 ~ H-1-inputLines:  [多行输入区]  ❯ line1 / line2 / ...
//   Row H-2-inputLines: [上分隔线]  ─── info ───
//   以上:      滚动内容区 (DECSTBM)

#include "CLFCore/CLFTerminal.hpp"
#include "CLFCore/CLFAnsi.hpp"
#include "CLFCore/CLFScrollBuffer.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#undef min
#undef max
#endif

namespace CLF::CLFCore {

CLFScrollBuffer CLFTerminal::s_buffer;
std::string CLFTerminal::s_statusTitle;
std::string CLFTerminal::s_statusContent;
std::string CLFTerminal::s_inputText;
std::string CLFTerminal::s_modeLabel;
int  CLFTerminal::s_inputCursor = 0;
bool CLFTerminal::s_inputDrawn = false;
bool CLFTerminal::s_confirmDrawn = false;

namespace {

// ============ 布局计算 ============
// 固定区 = 上分隔线(1) + 输入行(N) + 下分隔线(1) + 状态行(1) = N + 3

int inputLines(const std::string& text) {
    int lines = 1;
    for (char c : text) if (c == '\n') ++lines;
    return lines;
}

int inputRowTop(int H, int iLines)   { return H - 1 - iLines; }     // 输入区第一行
int upperSepRow(int H, int iLines)   { return H - 2 - iLines; }     // 上分隔线
int contentBottom(int H, int iLines) { return H - 3 - iLines; }     // 滚动区底
int lowerSepRow(int H)               { return H - 1; }              // 下分隔线
int statusRow(int H)                 { return H; }                  // 状态行

// 多行文本切分（按 \n）
std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else cur += c;
    }
    lines.push_back(cur);
    return lines;
}

// 计算光标在输入区的视觉位置（行索引 0=第一行, 列偏移=显示宽度）
void cursorVisualPos(const std::string& text, int bytePos, int& outLine, int& outColWidth) {
    outLine = 0;
    outColWidth = 0;
    for (int i = 0; i < bytePos && i < static_cast<int>(text.size()); ) {
        if (text[i] == '\n') { ++outLine; outColWidth = 0; ++i; }
        else {
            // 取一个完整 UTF-8 字符，计算显示宽度
            unsigned char c = static_cast<unsigned char>(text[i]);
            int charLen = 1;
            if ((c & 0xE0) == 0xC0) charLen = 2;
            else if ((c & 0xF0) == 0xE0) charLen = 3;
            else if ((c & 0xF8) == 0xF0) charLen = 4;
            std::string ch = text.substr(i, charLen);
            outColWidth += CLFAnsi::textWidth(ch);
            i += charLen;
        }
    }
}

// ============ 绘制辅助 ============

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

// 绘制上分隔线（含右侧信息）
void drawUpperSep(int W, int H, int iLines) {
    std::string info = "clfcode";
    std::string left = std::string(3, '-');
    int pad = W - CLFAnsi::textWidth(left) - CLFAnsi::textWidth(info) - 2;
    if (pad < 1) pad = 1;
    std::string line = left + std::string(pad, '-') + " " + info;
    std::cout << "\033[" << upperSepRow(H, iLines) << ";1H"
              << CLFAnsi::gray(truncateToWidth(line, W)) << "\033[K" << std::flush;
}

// 绘制下分隔线
void drawLowerSep(int W, int H) {
    std::cout << "\033[" << lowerSepRow(H) << ";1H"
              << CLFAnsi::gray(std::string(W, '-')) << "\033[K" << std::flush;
}

// 绘制状态行
void drawStatusLine(int W, int H, const std::string& mode) {
    std::string left  = "  " + mode + " mode on";
    std::string hints = "shift+tab to cycle · esc to interrupt · ? for help";
    int pad = W - CLFAnsi::textWidth(left) - CLFAnsi::textWidth(hints) - 2;
    if (pad < 1) pad = 1;
    std::string display = left + std::string(pad, ' ') + hints;
    std::cout << "\033[" << statusRow(H) << ";1H"
              << CLFAnsi::gray(truncateToWidth(display, W)) << "\033[K" << std::flush;
}

} // anonymous namespace

// ============================================================================
// ANSI — 委托 CLFAnsi
// ============================================================================

void CLFTerminal::enableAnsi() { CLFAnsi::enable(); }
std::string CLFTerminal::green(const std::string& s)  { return CLFAnsi::green(s); }
std::string CLFTerminal::cyan(const std::string& s)   { return CLFAnsi::cyan(s); }
std::string CLFTerminal::lightBlue(const std::string& s) { return CLFAnsi::lightBlue(s); }
std::string CLFTerminal::yellow(const std::string& s) { return CLFAnsi::yellow(s); }
std::string CLFTerminal::red(const std::string& s)    { return CLFAnsi::red(s); }
std::string CLFTerminal::gray(const std::string& s)   { return CLFAnsi::gray(s); }
std::string CLFTerminal::bold(const std::string& s)   { return CLFAnsi::bold(s); }

void CLFTerminal::item(const std::string& text) { std::cout << "● " << text << std::endl; }
void CLFTerminal::sub(const std::string& text)  { std::cout << "  ⎿ " << text << std::endl; }
void CLFTerminal::sub2(const std::string& text) { std::cout << "    ⎿ " << text << std::endl; }
void CLFTerminal::ok(const std::string& text)   { std::cout << "● " << green("✓") << " " << text << std::endl; }
void CLFTerminal::fail(const std::string& text) { std::cout << "● " << red("✗") << " " << text << std::endl; }
void CLFTerminal::info(const std::string& text) { std::cout << "● " << yellow("⚠") << " " << text << std::endl; }

int CLFTerminal::getTerminalHeight() { return CLFAnsi::terminalHeight(); }
int CLFTerminal::getTerminalWidth()  { return CLFAnsi::terminalWidth(); }
int CLFTerminal::textWidth(const std::string& t) { return CLFAnsi::textWidth(t); }
int CLFTerminal::wrappedLines(const std::string& t) { return CLFAnsi::wrappedLines(t); }

void CLFTerminal::moveCursor(int r, int c) {
    if (CLFAnsi::isEnabled()) std::cout << "\033[" << r << ";" << c << "H" << std::flush;
}
void CLFTerminal::clearLine() {
    if (CLFAnsi::isEnabled()) std::cout << "\033[K" << std::flush;
}

// ============================================================================
// 布局初始化
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
    if (H <= 0) H = 30; if (W <= 0) W = 80;

    if (!CLFAnsi::isEnabled() || H < 10) {
        std::cout << "\033[2J\033[H" << std::flush;
        return;
    }

    std::cout << "\033[2J\033[H" << std::flush;
    int iLines = inputLines(s_inputText);
    int cb = contentBottom(H, iLines);
    if (cb < 1) cb = 1;

    setScrollRegion(1, cb);
    resetScrollRegion();

    drawUpperSep(W, H, iLines);
    drawLowerSep(W, H);
    drawStatusLine(W, H, s_modeLabel);

    // 绘制空输入行
    std::cout << "\033[" << inputRowTop(H, iLines) << ";1H"
              << "❯ " << "\033[K" << std::flush;

    setScrollRegion(1, cb);
    std::cout << "\033[H" << std::flush;
    s_inputDrawn = true;
}

void CLFTerminal::setScrollCollapsed(bool) {}
bool CLFTerminal::isScrollCollapsed() { return false; }

// ============================================================================
// 多行输入区绘制（含光标定位）
// ============================================================================

void CLFTerminal::drawInputArea(const std::string& text, int cursorPos) {
    s_inputText = text;
    int cur = (cursorPos < 0) ? static_cast<int>(text.size()) : cursorPos;
    s_inputCursor = cur;

    int H = getTerminalHeight();
    int W = getTerminalWidth();
    if (H <= 0) H = 30; if (W <= 0) W = 80;

    int iLines = inputLines(text);
    if (iLines < 1) iLines = 1;

    if (!CLFAnsi::isEnabled() || H < 10) {
        std::cout << "\n\n" << std::flush;
        std::cout << lightBlue(std::string(W - 1, '-')) << "\n" << std::flush;
        auto lines = splitLines(text);
        for (const auto& l : lines) std::cout << "❯ " << l << "\n" << std::flush;
        std::cout << lightBlue(std::string(W - 1, '-')) << "\n" << std::flush;
        s_inputDrawn = true;
        return;
    }

    // 计算布局（动态适应行数变化）
    int cb = contentBottom(H, iLines);
    if (cb < 1) cb = 1;
    int topRow = inputRowTop(H, iLines);

    // 重设滚动区（输入行数可能变化）
    resetScrollRegion();
    setScrollRegion(1, cb);

    // 绘制固定区
    drawUpperSep(W, H, iLines);

    // 逐行绘制输入文本
    auto lines = splitLines(text);
    for (size_t li = 0; li < lines.size(); ++li) {
        int row = topRow + static_cast<int>(li);
        std::string prefix = (li == 0) ? "❯ " : "  ";
        std::cout << "\033[" << row << ";1H"
                  << prefix << lines[li] << "\033[K" << std::flush;
    }
    // 清除多余的旧行
    int maxRows = H - 2 - upperSepRow(H, iLines);
    for (int r = topRow + static_cast<int>(lines.size()); r < topRow + maxRows; ++r) {
        std::cout << "\033[" << r << ";1H\033[K" << std::flush;
    }

    drawLowerSep(W, H);
    drawStatusLine(W, H, s_modeLabel);

    // 光标定位（显示宽度，非字节偏移）
    int cursorLine, cursorColW;
    cursorVisualPos(text, cur, cursorLine, cursorColW);
    int prefixW = (cursorLine == 0) ? CLFAnsi::textWidth("❯ ") : CLFAnsi::textWidth("  ");
    int col = 1 + prefixW + cursorColW;
    std::cout << "\033[" << (topRow + cursorLine) << ";" << col << "H" << std::flush;

    s_inputDrawn = true;
}

// ============================================================================
// 内容区输出 / 区域绘制
// ============================================================================

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
    int W = getTerminalWidth(); if (W <= 0) W = 80;
    if (!title.empty())
        scrollPrint(lightBlue("▍ ") + bold(truncateToWidth(title, W - 3)) + "\n");
    if (!content.empty()) {
        std::string d = truncateToWidth(content, W - 5);
        if (d != content) d += "...";
        scrollPrint("  ⎿ " + gray(d) + "\n");
    }
}

void CLFTerminal::drawModeArea(const std::string& mode) {
    if (s_modeLabel == mode) return;
    s_modeLabel = mode;
    if (!s_inputDrawn || !CLFAnsi::isEnabled()) return;
    int H = getTerminalHeight(); if (H < 10) return;
    int W = getTerminalWidth(); if (W <= 0) W = 80;
    int iLines = inputLines(s_inputText); if (iLines < 1) iLines = 1;
    std::cout << "\0337";
    drawStatusLine(W, H, mode);
    std::cout << "\0338" << std::flush;
}

void CLFTerminal::drawConfirmArea(const std::vector<std::string>& options, int selected) {
    if (s_confirmDrawn && CLFAnsi::isEnabled()) std::cout << "\033[2A" << std::flush;
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
    int H = getTerminalHeight(); if (H <= 0) H = 30;
    int W = getTerminalWidth(); if (W <= 0) W = 80;
    int iLines = inputLines(s_inputText); if (iLines < 1) iLines = 1;

    if (!CLFAnsi::isEnabled() || H < 10) {
        std::cout << "\r\033[K\n\033[K\n\033[K\n" << std::flush;
        s_inputDrawn = false;
        s_confirmDrawn = false;
        return;
    }

    // 清除所有输入行 + 重绘固定区
    int topRow = inputRowTop(H, iLines);
    for (int r = topRow; r <= H; ++r)
        std::cout << "\033[" << r << ";1H\033[K" << std::flush;

    // 重置为单行空输入 + 重绘固定区
    s_inputText.clear();
    s_inputCursor = 0;
    iLines = 1;
    int cb = contentBottom(H, iLines); if (cb < 1) cb = 1;

    resetScrollRegion();
    setScrollRegion(1, cb);

    // 重绘固定区（不调用 drawInputArea——它会定位光标到输入区）
    drawUpperSep(W, H, iLines);
    std::cout << "\033[" << inputRowTop(H, iLines) << ";1H❯ \033[K" << std::flush;
    drawLowerSep(W, H);
    drawStatusLine(W, H, s_modeLabel);

    // 光标回到内容区（后续 scrollPrint / ThinkingIndicator 从这里输出）
    std::cout << "\033[" << cb << ";1H" << std::flush;
    std::cout << "\n" << std::flush;

    s_inputDrawn = true;
    s_confirmDrawn = false;
}

void CLFTerminal::redrawAll() {
    int H = getTerminalHeight(); int W = getTerminalWidth();
    if (H <= 0) H = 30; if (W <= 0) W = 80;

    if (!CLFAnsi::isEnabled() || H < 10) {
        std::cout << "\033[2J\033[H" << std::flush;
        for (const auto& line : s_buffer.lines())
            std::cout << line << "\n" << std::flush;
        s_inputDrawn = false;
        drawInputArea(s_inputText, s_inputCursor);
        return;
    }

    std::cout << "\033[2J\033[H" << std::flush;
    int iLines = inputLines(s_inputText); if (iLines < 1) iLines = 1;
    int cb = contentBottom(H, iLines); if (cb < 1) cb = 1;

    resetScrollRegion();
    setScrollRegion(1, cb);

    // 重放缓冲内容
    const auto& lines = s_buffer.lines();
    size_t visible = static_cast<size_t>(cb - 1);
    size_t start = (lines.size() > visible) ? lines.size() - visible : 0;
    for (size_t i = start; i < lines.size(); ++i) {
        std::string stripped = stripAnsi(lines[i]);
        if (textWidth(stripped) <= W - 1)
            std::cout << lines[i] << "\n" << std::flush;
        else
            std::cout << truncateToWidth(stripped, W - 1) << "\n" << std::flush;
    }

    // 重绘固定区
    resetScrollRegion();
    s_inputDrawn = false;
    drawInputArea(s_inputText, s_inputCursor);

    setScrollRegion(1, cb);
    std::cout << "\033[H" << std::flush;
}

std::string CLFTerminal::diagnosticInfo() {
    return "终端: 高" + std::to_string(getTerminalHeight())
         + " x 宽" + std::to_string(getTerminalWidth())
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
    if (CLFAnsi::isEnabled()) std::cout << "\033[2J\033[H" << std::flush;
}

} // namespace CLF::CLFCore
