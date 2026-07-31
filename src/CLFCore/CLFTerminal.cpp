// CLFTerminal.cpp — 终端 UI 工具实现（5 区布局）

#include "CLFCore/CLFTerminal.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
// 避免 windows.h 的 min/max 宏与 std::min/std::max 冲突
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

namespace {

// 布局行号（缩放自适应：每次调用重新获取 H）
constexpr int kStatusRows = 2;  // 区域2：标题 + 内容
constexpr int kInputRows = 1;   // 区域3
constexpr int kModeRows  = 1;   // 区域4
constexpr int kConfirmRows = 2; // 区域5：确认时显示

int statusTop(int H)    { return H - 8; }   // 状态区标题行
int inputLineTop(int H) { return H - 6; }   // 输入区上线（浅蓝分割线）
int inputTop(int H)     { return H - 5; }   // 输入行
int inputLineBottom(int H) { return H - 4; } // 输入区下线（浅蓝分割线）
int modeTop(int H)      { return H - 3; }   // 模式区
int confirmTop(int H)   { return H - 2; }   // 确认区（2 行）
int scrollBottom(int H) { return H - 9; }   // 滚动区底部（固定区之上）

// 滚动区可见行数（折叠功能暂缓：始终完整显示全部可见行）
int scrollVisibleLines(int H, bool /*collapsed*/) {
    return scrollBottom(H);
}

// 按完整 UTF-8 字符截断到指定显示宽度（防止截断产生非法 UTF-8 导致终端渲染错乱）
std::string truncateToWidth(const std::string& text, int maxWidth) {
    if (CLFTerminal::textWidth(text) <= maxWidth) return text;
    std::string result = text;
    while (CLFTerminal::textWidth(result) > maxWidth) {
        // 删除最后一个完整字符（lead byte + 续字节）
        size_t len = 1;
        while (len < result.size()
               && (static_cast<unsigned char>(result[result.size() - len]) & 0xC0) == 0x80) {
            ++len;
        }
        result.erase(result.size() - len);
    }
    return result;
}

} // anonymous namespace

void CLFTerminal::enableAnsi() {
#ifdef _WIN32
    // Win10 1809+ 的 conhost / Windows Terminal 均支持 VT，
    // SetConsoleMode 成功即可信（不采用 DSR 自检——响应时序不稳定会误判 + 泄漏）
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

std::string CLFTerminal::green(const std::string& s) {
    return s_ansiEnabled ? "\033[32m" + s + "\033[0m" : s;
}

std::string CLFTerminal::cyan(const std::string& s) {
    return s_ansiEnabled ? "\033[36m" + s + "\033[0m" : s;
}

std::string CLFTerminal::lightBlue(const std::string& s) {
    return s_ansiEnabled ? "\033[94m" + s + "\033[0m" : s;
}

std::string CLFTerminal::yellow(const std::string& s) {
    return s_ansiEnabled ? "\033[33m" + s + "\033[0m" : s;
}

std::string CLFTerminal::red(const std::string& s) {
    return s_ansiEnabled ? "\033[31m" + s + "\033[0m" : s;
}

std::string CLFTerminal::gray(const std::string& s) {
    return s_ansiEnabled ? "\033[90m" + s + "\033[0m" : s;
}

std::string CLFTerminal::bold(const std::string& s) {
    return s_ansiEnabled ? "\033[1m" + s + "\033[0m" : s;
}

void CLFTerminal::item(const std::string& text) {
    std::cout << "● " << text << std::endl;
}

void CLFTerminal::sub(const std::string& text) {
    std::cout << "  ⎿ " << text << std::endl;
}

void CLFTerminal::sub2(const std::string& text) {
    std::cout << "    ⎿ " << text << std::endl;
}

void CLFTerminal::ok(const std::string& text) {
    std::cout << "● " << green("✓") << " " << text << std::endl;
}

void CLFTerminal::fail(const std::string& text) {
    std::cout << "● " << red("✗") << " " << text << std::endl;
}

void CLFTerminal::info(const std::string& text) {
    std::cout << "● " << yellow("⚠") << " " << text << std::endl;
}

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
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
        return static_cast<int>(ws.ws_row);
    }
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
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return static_cast<int>(ws.ws_col);
    }
    return -1;
#endif
}

int CLFTerminal::textWidth(const std::string& text) {
    int width = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            ++width;
        } else if ((c & 0xC0) == 0xC0) {
            width += 2;
        }
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
// 5 区布局
// ============================================================================

void CLFTerminal::initLayout(const std::string& modeLabel) {
    s_modeLabel = modeLabel;
    s_inputText.clear();
    s_inputCursor = 0;

    int H = getTerminalHeight();
    if (H <= 0) return;

    // 清屏
    if (s_ansiEnabled) {
        std::cout << "\033[2J" << std::flush;
    }

    // 绘制固定区
    drawStatusArea("", "");
    drawInputArea("");
    drawModeArea(modeLabel);
    // 滚动区空（无内容），光标停到滚动区底部
    moveCursor(scrollBottom(H), 1);
}

// [折叠功能暂缓] 滚动区始终完整显示，Ctrl+O 展开/折叠待后续恢复
// void CLFTerminal::setScrollCollapsed(bool collapsed) { ... }
// bool CLFTerminal::isScrollCollapsed() { ... }
void CLFTerminal::setScrollCollapsed(bool) {} // 保留接口，无操作
bool CLFTerminal::isScrollCollapsed() { return false; } // 不折叠

void CLFTerminal::scrollPrint(const std::string& text) {
    // 按行拆分追加到缓冲
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

    int H = getTerminalHeight();
    if (H < 10 || !s_ansiEnabled) {
        // 无法获取高度 / 窗口过小（非交互终端如 CLion Run 面板）/ ANSI 无效：直接输出（降级）
        std::cout << text << std::flush;
        return;
    }

    // 重绘滚动区可见部分
    int lines = scrollVisibleLines(H, s_scrollCollapsed);
    size_t start = (s_scrollBuffer.size() > static_cast<size_t>(lines))
                 ? s_scrollBuffer.size() - lines : 0;
    for (int i = 0; i < lines; ++i) {
        int row = scrollBottom(H) - lines + 1 + i;
        moveCursor(row, 1);
        clearLine();
        size_t idx = start + i;
        if (idx < s_scrollBuffer.size()) {
            // 截断超宽行（按完整 UTF-8 字符）
            int W = getTerminalWidth();
            std::string display = (W > 0)
                ? truncateToWidth(s_scrollBuffer[idx], W - 1)
                : s_scrollBuffer[idx];
            std::cout << display << std::flush;
        }
    }

    // 重绘固定区（2-5 不被覆盖）
    drawStatusArea(s_statusTitle, s_statusContent);
    drawInputArea(s_inputText, s_inputCursor);
    drawModeArea(s_modeLabel);

    // 光标停在滚动区最后一行
    moveCursor(scrollBottom(H), 1);
}

void CLFTerminal::drawStatusArea(const std::string& title, const std::string& content) {
    s_statusTitle = title;
    s_statusContent = content;

    int H = getTerminalHeight();
    if (H < 10 || !s_ansiEnabled) return; // 降级：不显示状态区
    int W = getTerminalWidth();
    if (W <= 0) W = 80;

    // 标题行（浅蓝 + 时间风格），防超宽折行
    moveCursor(statusTop(H), 1);
    clearLine();
    if (!title.empty()) {
        std::string display = truncateToWidth(title, W - 3);
        std::cout << lightBlue("▍ ") << bold(display) << std::flush;
    }
    // 内容行
    moveCursor(statusTop(H) + 1, 1);
    clearLine();
    if (!content.empty()) {
        std::string display = truncateToWidth(content, W - 5);
        if (display != content) display += "...";
        std::cout << "  ⎿ " << gray(display) << std::flush;
    }
}

void CLFTerminal::drawInputArea(const std::string& text, int cursorPos) {
    s_inputText = text;
    s_inputCursor = (cursorPos < 0) ? static_cast<int>(text.size()) : cursorPos;

    int H = getTerminalHeight();
    if (H < 10 || !s_ansiEnabled) {
        std::cout << "\r❯ " << text << "\033[K" << std::flush;
        return;
    }

    int W = getTerminalWidth();
    if (W <= 0) W = 80;

    // 上线（浅蓝分割线）
    moveCursor(inputLineTop(H), 1);
    clearLine();
    std::cout << lightBlue(std::string(W - 1, '-')) << std::flush;

    // 输入行（❯ 提示符，Claude Code 风格）
    moveCursor(inputTop(H), 1);
    clearLine();
    std::cout << "❯ " << text << std::flush;

    // 下线（浅蓝分割线）
    moveCursor(inputLineBottom(H), 1);
    clearLine();
    std::cout << lightBlue(std::string(W - 1, '-')) << std::flush;

    // 光标定位到输入位置（text 中 cursorPos 字符索引 → 列）
    std::string prefix = text.substr(0, static_cast<size_t>(s_inputCursor));
    moveCursor(inputTop(H), 3 + textWidth(prefix));
}

void CLFTerminal::drawModeArea(const std::string& mode) {
    s_modeLabel = mode;

    int H = getTerminalHeight();
    if (H < 10 || !s_ansiEnabled) return; // 降级：不显示模式区（避免刷屏）

    int W = getTerminalWidth();
    if (W <= 0) W = 80;

    moveCursor(modeTop(H), 1);
    clearLine();
    std::string line = "模式: " + mode;
    // 右侧提示快捷键（总宽 ≤ W，含 "▍ " 前缀 2 宽）
    std::string hint = "Ctrl+N 切换 | /help";
    constexpr int kPrefixWidth = 2; // "▍ "
    int pad = W - kPrefixWidth - textWidth(line) - textWidth(hint);
    if (pad < 0) pad = 0;
    std::string display = line + std::string(pad, ' ') + hint;
    // 防超宽折行（按完整 UTF-8 字符截断尾部）
    display = truncateToWidth(display, W - kPrefixWidth);
    std::cout << gray("▍ ") << display << std::flush;
}

void CLFTerminal::drawConfirmArea(const std::vector<std::string>& options, int selected) {
    int H = getTerminalHeight();
    if (H <= 0) return;
    int W = getTerminalWidth();
    if (W <= 0) W = 80;

    // 第一行：问题提示
    moveCursor(confirmTop(H), 1);
    clearLine();
    std::cout << yellow("⚠ ") << "请确认操作：" << std::flush;

    // 第二行：选项（上下键选择，选中高亮）
    moveCursor(confirmTop(H) + 1, 1);
    clearLine();
    std::string display;
    for (size_t i = 0; i < options.size(); ++i) {
        if (i > 0) display += "    ";
        if (static_cast<int>(i) == selected) {
            display += "[" + green("●") + "] " + bold(options[i]);
        } else {
            display += "[ ] " + gray(options[i]);
        }
    }
    std::cout << display << std::flush;
}

void CLFTerminal::clearConfirmArea() {
    int H = getTerminalHeight();
    if (H <= 0) return;
    for (int i = 0; i < kConfirmRows; ++i) {
        moveCursor(confirmTop(H) + i, 1);
        clearLine();
    }
}

void CLFTerminal::scrollAppend(const std::string& text) {
    // 与 scrollPrint 相同（完整重绘，正确处理换行/超宽截断）
    // 保留接口兼容，流式输出统一走完整重绘
    scrollPrint(text);
}

std::string CLFTerminal::diagnosticInfo() {
    int H = getTerminalHeight();
    int W = getTerminalWidth();
    return "终端: 高" + std::to_string(H) + " x 宽" + std::to_string(W)
         + ", ANSI: " + (s_ansiEnabled ? "开" : "关");
}

void CLFTerminal::restoreScrollRegion() {
    if (s_ansiEnabled) {
        std::cout << "\033[2J\033[H" << std::flush;
    }
}

} // namespace CLF::CLFCore
