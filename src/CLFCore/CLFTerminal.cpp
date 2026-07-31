// CLFTerminal.cpp — 终端 UI 工具实现（DECSTBM 滚动区 + 固定底部区）

#include "CLFCore/CLFTerminal.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#undef min
#undef max
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace CLF::CLFCore {

bool CLFTerminal::s_ansiEnabled = false;
bool CLFTerminal::s_scrollCollapsed = true;
std::vector<std::string> CLFTerminal::s_scrollBuffer;
std::string CLFTerminal::s_statusTitle;
std::string CLFTerminal::s_statusContent;
std::string CLFTerminal::s_inputText;
std::string CLFTerminal::s_modeLabel;
int  CLFTerminal::s_inputCursor = 0;
bool CLFTerminal::s_inputDrawn = false;
bool CLFTerminal::s_confirmDrawn = false;

namespace {

// ============ 布局常量 ============
constexpr int kFixedLines = 5; // blank + upper-sep + input + lower-sep + mode
int contentBottom(int H) { return H - kFixedLines; }
int inputRow(int H)      { return H - 2; }
int modeRow(int H)       { return H; }

// ============ UTF-8 / ANSI 工具 ============
std::string truncateToWidth(const std::string& text, int maxWidth) {
    if (CLFTerminal::textWidth(text) <= maxWidth) return text;
    std::string result = text;
    while (CLFTerminal::textWidth(result) > maxWidth) {
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

// ============ 滚动区控制 ============
void setScrollRegion(int top, int bottom) {
    std::cout << "\033[" << top << ";" << bottom << "r" << std::flush;
}
void resetScrollRegion() {
    std::cout << "\033[r" << std::flush;
}

// ============ 模式行 ============
void drawModeLine(const std::string& mode) {
    int W = CLFTerminal::getTerminalWidth();
    if (W <= 0) W = 80;
    std::string left  = mode + " mode on";
    std::string hints = "shift+tab to cycle · esc to interrupt · ? for help";
    constexpr int kIndent = 2;
    int pad = W - kIndent - CLFTerminal::textWidth(left) - CLFTerminal::textWidth(hints);
    if (pad < 0) pad = 1;
    std::string display = truncateToWidth(left + std::string(pad, ' ') + hints, W - kIndent);
    std::cout << std::string(kIndent, ' ') << CLFTerminal::gray(display) << std::flush;
}

// ============ 固定区绘制（接收 inputText 参数，避免访问私有成员）============
void redrawFixedArea(int W, int H, const std::string& inputText) {
    std::cout << "\033[" << (H - 4) << ";1H\033[K" << std::flush;
    std::cout << "\033[" << (H - 3) << ";1H"
              << CLFTerminal::lightBlue(std::string(W - 1, '-')) << std::flush;
    std::cout << "\033[" << (H - 2) << ";1H"
              << "❯ " << inputText << "\033[K" << std::flush;
    std::cout << "\033[" << (H - 1) << ";1H"
              << CLFTerminal::lightBlue(std::string(W - 1, '-')) << std::flush;
    std::cout << "\033[" << H << ";1H\033[K" << std::flush;
}

} // anonymous namespace

// ============================================================================
// ANSI 颜色
// ============================================================================

void CLFTerminal::enableAnsi() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            if (SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
                s_ansiEnabled = true;
            }
        }
    }
#else
    s_ansiEnabled = true;
#endif
}

std::string CLFTerminal::green(const std::string& s)     { return s_ansiEnabled ? "\033[32m" + s + "\033[0m" : s; }
std::string CLFTerminal::cyan(const std::string& s)      { return s_ansiEnabled ? "\033[36m" + s + "\033[0m" : s; }
std::string CLFTerminal::lightBlue(const std::string& s) { return s_ansiEnabled ? "\033[94m" + s + "\033[0m" : s; }
std::string CLFTerminal::yellow(const std::string& s)    { return s_ansiEnabled ? "\033[33m" + s + "\033[0m" : s; }
std::string CLFTerminal::red(const std::string& s)       { return s_ansiEnabled ? "\033[31m" + s + "\033[0m" : s; }
std::string CLFTerminal::gray(const std::string& s)      { return s_ansiEnabled ? "\033[90m" + s + "\033[0m" : s; }
std::string CLFTerminal::bold(const std::string& s)      { return s_ansiEnabled ? "\033[1m" + s + "\033[0m" : s; }

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
// 终端控制
// ============================================================================

int CLFTerminal::getTerminalHeight() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return -1;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(hOut, &info)) return -1;
    return static_cast<int>(info.srWindow.Bottom - info.srWindow.Top + 1);
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return static_cast<int>(ws.ws_row);
    return -1;
#endif
}

int CLFTerminal::getTerminalWidth() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return -1;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(hOut, &info)) return -1;
    return static_cast<int>(info.srWindow.Right - info.srWindow.Left + 1);
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return static_cast<int>(ws.ws_col);
    return -1;
#endif
}

int CLFTerminal::textWidth(const std::string& text) {
    int width = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) ++width;
        else if ((c & 0xC0) == 0xC0) width += 2;
    }
    return width;
}

int CLFTerminal::wrappedLines(const std::string& text) {
    int W = getTerminalWidth();
    if (W <= 0) return 1;
    int lines = textWidth(text) / W + 1;
    return (lines > 0) ? lines : 1;
}

void CLFTerminal::moveCursor(int row, int col) {
    if (!s_ansiEnabled) return;
    std::cout << "\033[" << row << ";" << col << "H" << std::flush;
}

void CLFTerminal::clearLine() {
    if (!s_ansiEnabled) return;
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
    s_scrollBuffer.clear();

    int H = getTerminalHeight();
    int W = getTerminalWidth();
    if (H <= 0) H = 30;
    if (W <= 0) W = 80;

    if (!s_ansiEnabled || H < 10) {
        std::cout << "\033[2J\033[H" << std::flush;
        return;
    }

    // 清屏
    std::cout << "\033[2J\033[H" << std::flush;

    // 设置滚动区（内容区：1 ~ H-kFixedLines）
    int cb = contentBottom(H);
    setScrollRegion(1, cb);

    // 绘制固定区
    resetScrollRegion();
    redrawFixedArea(W, H, s_inputText);
    drawModeLine(s_modeLabel);

    // 恢复滚动区，光标到内容区顶部
    setScrollRegion(1, cb);
    std::cout << "\033[H" << std::flush;

    s_inputDrawn = true;
}

void CLFTerminal::setScrollCollapsed(bool) {} // 保留接口
bool CLFTerminal::isScrollCollapsed() { return false; }

void CLFTerminal::scrollPrint(const std::string& text) {
    // 缓冲维护
    std::string line;
    for (char c : text) {
        if (c == '\n') {
            s_scrollBuffer.push_back(line);
            line.clear();
        } else {
            line += c;
        }
    }
    if (!line.empty()) {
        s_scrollBuffer.push_back(line);
    }

    // 输出（滚动区内自然滚动）
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

    if (!s_ansiEnabled || H < 10) {
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
        std::string prefix = text.substr(0, static_cast<size_t>(cur));
        int col = 1 + textWidth("❯ ") + textWidth(prefix);
        std::cout << "\033[" << col << "G" << std::flush;
        return;
    }

    // ANSI 模式：固定区原地更新
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

    // 更新输入行（不重置滚动区——CUP 不受区域约束）
    std::cout << "\033[" << inputRow(H) << ";1H"
              << "\r❯ " << text << "\033[K" << std::flush;

    // 光标定位（CUP 直接指定行列，与滚动区无关）
    std::string prefix = text.substr(0, static_cast<size_t>(cur));
    int col = 1 + textWidth("❯ ") + textWidth(prefix);
    std::cout << "\033[" << inputRow(H) << ";" << col << "H" << std::flush;
}

void CLFTerminal::drawModeArea(const std::string& mode) {
    if (s_modeLabel == mode) return;
    s_modeLabel = mode;
    if (!s_inputDrawn || !s_ansiEnabled) return;

    int H = getTerminalHeight();
    if (H < 10) return;

    // DECSC 保存光标 → 更新模式行 → DECRC 恢复光标
    std::cout << "\0337"                                     // DECSC
              << "\033[" << modeRow(H) << ";1H\033[K"       // 移到模式行
              << std::flush;
    drawModeLine(mode);
    std::cout << "\0338"                                     // DECRC
              << std::flush;
}

void CLFTerminal::drawConfirmArea(const std::vector<std::string>& options, int selected) {
    // 确认区在滚动区内输出（自然位于内容区底部、固定区上方）
    if (s_confirmDrawn && s_ansiEnabled) {
        std::cout << "\033[2A" << std::flush; // 覆盖前次绘制
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

void CLFTerminal::clearConfirmArea() {
    // 滚动区内，后续内容自然覆盖
}

void CLFTerminal::toContentArea() {
    int H = getTerminalHeight();
    if (H <= 0) H = 30;

    if (!s_ansiEnabled || H < 10) {
        // 回退模式：清 3 行
        std::cout << "\r\033[K\n\033[K\n\033[K\n" << std::flush;
        s_inputDrawn = false;
        s_confirmDrawn = false;
        return;
    }

    // 移到内容区底部，空一行开始新内容
    // 光标在输入行 (H-2)，需要进入滚动区 (1 ~ H-5)
    std::cout << "\033[" << contentBottom(H) << ";1H" << std::flush;
    std::cout << "\n" << std::flush; // 滚动区上滚一行，为新内容腾出空间
    s_confirmDrawn = false;
}

void CLFTerminal::redrawAll() {
    int H = getTerminalHeight();
    int W = getTerminalWidth();
    if (H <= 0) H = 30;
    if (W <= 0) W = 80;

    if (!s_ansiEnabled || H < 10) {
        std::cout << "\033[2J\033[H" << std::flush;
        for (const auto& line : s_scrollBuffer) {
            std::cout << line << "\n" << std::flush;
        }
        s_inputDrawn = false;
        drawInputArea(s_inputText, s_inputCursor);
        return;
    }

    // 清屏
    std::cout << "\033[2J\033[H" << std::flush;

    // 设置新滚动区
    int cb = contentBottom(H);
    resetScrollRegion();
    setScrollRegion(1, cb);

    // 从缓冲重绘可见内容
    size_t visible = static_cast<size_t>(cb - 1);
    size_t start = (s_scrollBuffer.size() > visible)
                       ? s_scrollBuffer.size() - visible : 0;
    for (size_t i = start; i < s_scrollBuffer.size(); ++i) {
        const std::string& raw = s_scrollBuffer[i];
        std::string stripped = stripAnsi(raw);
        if (textWidth(stripped) <= W - 1) {
            std::cout << raw << "\n" << std::flush;
        } else {
            std::cout << truncateToWidth(stripped, W - 1) << "\n" << std::flush;
        }
    }

    // 重绘固定区
    resetScrollRegion();
    redrawFixedArea(W, H, s_inputText);
    drawModeLine(s_modeLabel);

    // 恢复滚动区，光标到内容区
    setScrollRegion(1, cb);
    std::cout << "\033[H" << std::flush;

    s_inputDrawn = true;

    // 光标移到输入行
    drawInputArea(s_inputText, s_inputCursor);
}

std::string CLFTerminal::diagnosticInfo() {
    int H = getTerminalHeight();
    int W = getTerminalWidth();
    return "终端: 高" + std::to_string(H) + " x 宽" + std::to_string(W)
         + ", ANSI: " + (s_ansiEnabled ? "开" : "关");
}

void CLFTerminal::thoughtMark(int seconds) {
    if (seconds <= 0) return;
    scrollPrint("\n" + gray("  Thought for " + std::to_string(seconds) + "s") + "\n");
}

void CLFTerminal::restoreScrollRegion() {
    resetScrollRegion();
    if (s_ansiEnabled) {
        std::cout << "\033[2J\033[H" << std::flush;
    }
}

} // namespace CLF::CLFCore
