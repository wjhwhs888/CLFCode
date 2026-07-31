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

    // 1. 重置滚动区域（允许在输入区绘制）
    std::cout << "\033[r" << std::flush;

    // 2. 清除输入区（含折行）
    for (int i = 0; i < 3 + shift; ++i) {
        moveCursor(H - 4 - shift + i, 1);
        clearLine();
    }

    // 3. 画输入框
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

    // 4. 光标回到输入位置（必须在设置滚动区之前——滚动区外的定位会被忽略）
    moveCursor(H - 3 - shift, 3);

    // 5. 设置滚动区域：内容区 = 1 .. H-5-shift（输入区不参与滚动）
    //    DECSTBM 不移动光标，输入位置保留
    s_contentBottomRow = H - 5 - shift;
    if (s_ansiEnabled) {
        std::cout << "\033[1;" << s_contentBottomRow << "r" << std::flush;
    }
}

void CLFTerminal::toContentArea() {
    if (s_contentBottomRow <= 0) {
        int H = getTerminalHeight();
        s_contentBottomRow = (H > 0) ? H - 5 : 1;
    }
    moveCursor(s_contentBottomRow, 1); // 滚动区最后一行
}

void CLFTerminal::restoreScrollRegion() {
    if (s_ansiEnabled) {
        std::cout << "\033[r" << std::flush; // 重置滚动区域
        std::cout << "\033[2J\033[H" << std::flush; // 清屏 + 光标回原点
    }
}

} // namespace CLF::CLFCore
