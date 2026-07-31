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
bool CLFTerminal::s_inputDrawn = false;

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

// 输出模式行（无标志检查，供首绘与模式区绘制共用）
void drawModeLine(const std::string& mode) {
    int W = CLFTerminal::getTerminalWidth();
    if (W <= 0) W = 80;

    std::string line = "模式: " + mode;
    std::string hint = "Ctrl+N 切换 | /help";
    constexpr int kPrefixWidth = 2; // "▍ "
    int pad = W - kPrefixWidth - CLFTerminal::textWidth(line) - CLFTerminal::textWidth(hint);
    if (pad < 0) pad = 0;
    std::string display = truncateToWidth(line + std::string(pad, ' ') + hint, W - kPrefixWidth);
    std::cout << CLFTerminal::gray("▍ ") << display << "\n" << std::flush;
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
    s_inputDrawn = false;

    // 只清屏（固定区由 drawInputArea 在交互时顺序绘制，不依赖绝对定位）
    if (s_ansiEnabled) {
        std::cout << "\033[2J\033[H" << std::flush;
    }
}

// [折叠功能暂缓] 滚动区始终完整显示，Ctrl+O 展开/折叠待后续恢复
// void CLFTerminal::setScrollCollapsed(bool collapsed) { ... }
// bool CLFTerminal::isScrollCollapsed() { ... }
void CLFTerminal::setScrollCollapsed(bool) {} // 保留接口，无操作
bool CLFTerminal::isScrollCollapsed() { return false; } // 不折叠

void CLFTerminal::scrollPrint(const std::string& text) {
    // 按行拆分追加到缓冲（供状态查询；显示走增量输出）
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

    // 增量输出：从光标当前位置顺序打印（终端自然滚动）
    // 固定区（2-5）在每轮交互时重绘，不在流式输出中反复重绘（避免闪烁/错乱）
    std::cout << text << std::flush;
}

void CLFTerminal::drawStatusArea(const std::string& title, const std::string& content) {
    s_statusTitle = title;
    s_statusContent = content;

    int W = getTerminalWidth();
    if (W <= 0) W = 80;

    // 顺序输出到内容区末尾（状态区信息流）
    if (!title.empty()) {
        std::cout << lightBlue("▍ ") << bold(truncateToWidth(title, W - 3)) << "\n" << std::flush;
    }
    if (!content.empty()) {
        std::string display = truncateToWidth(content, W - 5);
        if (display != content) display += "...";
        std::cout << "  ⎿ " << gray(display) << "\n" << std::flush;
    }
}

void CLFTerminal::drawInputArea(const std::string& text, int cursorPos) {
    s_inputText = text;
    s_inputCursor = (cursorPos < 0) ? static_cast<int>(text.size()) : cursorPos;

    int W = getTerminalWidth();
    if (W <= 0) W = 80;

    if (!s_inputDrawn) {
        // 首次绘制：让位 2 行 + 上线 + 输入行 + 下线 + 模式行
        std::cout << "\n\n" << std::flush;
        std::cout << lightBlue(std::string(W - 1, '-')) << "\n" << std::flush;
        std::cout << "❯ " << text << "\n" << std::flush;
        std::cout << lightBlue(std::string(W - 1, '-')) << "\n" << std::flush;
        drawModeLine(s_modeLabel); // 模式行（内部输出，无标志检查）
        s_inputDrawn = true;
    } else {
        // 更新：光标已在输入行，\r 回行首重写（不增加空行）
        std::cout << "\r❯ " << text << "\033[K" << std::flush;
    }

    // 光标定位输入位置（\r 已在输入行行首，水平定位）
    if (s_ansiEnabled) {
        std::string prefix = text.substr(0, static_cast<size_t>(s_inputCursor));
        int col = 3 + textWidth(prefix);
        std::cout << "\033[" << col << "G" << std::flush;
    }
}

void CLFTerminal::drawModeArea(const std::string& mode) {
    s_modeLabel = mode;

    // 输入区已绘制时跳过（模式行已在首绘时输出；模式切换提示走滚动区）
    if (s_inputDrawn) return;
    drawModeLine(mode);
}

void CLFTerminal::drawConfirmArea(const std::vector<std::string>& options, int selected) {
    // 顺序输出到内容区末尾（确认区信息流，上下两行）
    std::cout << "\n" << yellow("⚠ ") << "请确认操作：" << "\n" << std::flush;

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
    // 顺序模型：确认框已被后续内容自然覆盖，无需清除
}

void CLFTerminal::scrollAppend(const std::string& text) {
    scrollPrint(text);
}

void CLFTerminal::toContentArea() {
    // 清除输入行（\r 回行首 + 清行），换行进入内容区
    // （下线/模式行随后被内容滚动覆盖，下次交互重新绘制完整输入区）
    std::cout << "\r\033[K\n" << std::flush;
    s_inputDrawn = false;
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
