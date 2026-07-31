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

int CLFTerminal::drawPromptBox(int extraLines) {
    int H = getTerminalHeight();
    if (H <= 0) {
        // 无法获取高度：退化为普通提示符
        std::cout << "> " << std::flush;
        return 0;
    }

    // 布局（从底往上）：
    //   H-2     空行（底部留白）
    //   H-3     输入行 > ...
    //   H-4     分隔线（浅蓝，ASCII 横线）
    //   extraLines>1 时整体上移 (extraLines-1) 行
    int shift = extraLines - 1;
    int topRow    = H - 4 - shift;
    int inputRow  = H - 3 - shift;

    // 分隔线（浅蓝 ASCII 横线，避免框线字符乱码）
    int W = getTerminalWidth();
    if (W <= 0) W = 80;
    moveCursor(topRow, 1);
    clearLine();
    std::cout << lightBlue(std::string(W - 1, '-')) << std::flush;

    // 输入行
    moveCursor(inputRow, 1);
    clearLine();
    std::cout << "> " << std::flush;

    // 光标回到输入位置
    moveCursor(inputRow, 3);
    return inputRow;
}

void CLFTerminal::clearPromptBox(int extraLines) {
    int H = getTerminalHeight();
    if (H <= 0) return;

    int shift = extraLines - 1;
    for (int i = 0; i < 2 + shift; ++i) {
        int row = H - 4 - shift + i;
        moveCursor(row, 1);
        clearLine();
    }
}

} // namespace CLF::CLFCore
