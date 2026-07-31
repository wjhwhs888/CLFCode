// CLFTerminal.cpp — 终端 UI 工具实现

#include "CLFCore/CLFTerminal.hpp"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace CLF::CLFCore {

bool CLFTerminal::s_ansiEnabled = false;
int  CLFTerminal::s_contentBottomRow = -1;

void CLFTerminal::enableAnsi() {
#ifdef _WIN32
    // 启用控制台 VT 处理（现代 Windows 终端支持）
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
    s_ansiEnabled = true; // POSIX 终端默认支持
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
            ++width; // ASCII 1 列
        } else if ((c & 0xC0) == 0xC0) {
            width += 2; // 多字节字符（中文等）2 列
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

void CLFTerminal::setupSplitScreen(int extraLines, const std::string& modeLabel) {
    int H = getTerminalHeight();
    if (H <= 0) {
        // 无法获取高度：退化为普通提示符
        std::cout << "> " << std::flush;
        return;
    }

    int W = getTerminalWidth();
    if (W <= 0) W = 80;

    int shift = extraLines - 1;

    // 1. 清除输入区（含折行）
    for (int i = 0; i < 3 + shift; ++i) {
        moveCursor(H - 4 - shift + i, 1);
        clearLine();
    }

    // 2. 画输入框
    //    H-4-shift  顶线（浅蓝 ASCII 横线）
    //    H-3-shift  输入行 > ...
    //    H-2-shift  底线（右侧显示 modeLabel）
    moveCursor(H - 4 - shift, 1);
    clearLine();
    std::cout << lightBlue(std::string(W - 1, '-')) << std::flush;

    moveCursor(H - 3 - shift, 1);
    clearLine();
    std::cout << "> " << std::flush;

    moveCursor(H - 2 - shift, 1);
    clearLine();
    std::string bottom = std::string(W - 1, '-');
    if (!modeLabel.empty()) {
        std::string label = " [" + modeLabel + "]";
        int labelWidth = textWidth(label);
        if (labelWidth < W) {
            bottom = bottom.substr(0, W - 1 - labelWidth) + label;
        }
    }
    std::cout << lightBlue(bottom) << std::flush;

    // 3. 光标回到输入位置
    moveCursor(H - 3 - shift, 3);
}

void CLFTerminal::toContentArea() {
    int H = getTerminalHeight();
    if (H <= 0) return;

    // 清除输入框区域（顶线/输入行/底线），光标停在内容区底部
    for (int i = 0; i < 3; ++i) {
        moveCursor(H - 4 + i, 1);
        clearLine();
    }
    // 光标到内容区底部（输入框顶线位置），后续输出自然向下滚动
    moveCursor(H - 4, 1);
}

bool CLFTerminal::confirmInput(const std::string& question, const std::string& modeLabel) {
    int H = getTerminalHeight();
    if (H <= 0) {
        // 无法获取高度：退化为普通提问
        std::cout << question << " " << std::flush;
        std::string ans;
        std::getline(std::cin, ans);
        return ans == "y" || ans == "Y" || ans == "yes";
    }

    int W = getTerminalWidth();
    if (W <= 0) W = 80;

    // 1. 清除输入区 + 画确认框（顶线/问题行/底线）
    for (int i = 0; i < 3; ++i) {
        moveCursor(H - 4 + i, 1);
        clearLine();
    }
    moveCursor(H - 4, 1);
    std::cout << lightBlue(std::string(W - 1, '-')) << std::flush;

    moveCursor(H - 3, 1);
    std::cout << question << std::flush;

    moveCursor(H - 2, 1);
    std::string bottom = std::string(W - 1, '-');
    if (!modeLabel.empty()) {
        std::string label = " [" + modeLabel + "]";
        int labelWidth = textWidth(label);
        if (labelWidth < W) {
            bottom = bottom.substr(0, W - 1 - labelWidth) + label;
        }
    }
    std::cout << lightBlue(bottom) << std::flush;

    // 2. 光标到问题文本后（输入位置）
    moveCursor(H - 3, textWidth(question) + 1);

    // 3. 读输入
    std::string ans;
    std::getline(std::cin, ans);

    // 4. 清除确认框，光标回内容区
    for (int i = 0; i < 3; ++i) {
        moveCursor(H - 4 + i, 1);
        clearLine();
    }
    moveCursor(H - 4, 1);

    return ans == "y" || ans == "Y" || ans == "yes";
}

void CLFTerminal::restoreScrollRegion() {
    if (s_ansiEnabled) {
        std::cout << "\033[2J\033[H" << std::flush; // 清屏 + 光标回原点
    }
}

} // namespace CLF::CLFCore
