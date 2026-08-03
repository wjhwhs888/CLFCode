// CLFAnsi.cpp — ANSI 终端控制原语实现

#include "CLFCore/CLFAnsi.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace CLF::CLFCore {

bool CLFAnsi::s_enabled = false;

void CLFAnsi::enable() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            if (SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
                s_enabled = true;
            }
        }
    }
#else
    s_enabled = true;
#endif
}

std::string CLFAnsi::green(const std::string& s)     { return s_enabled ? "\033[32m" + s + "\033[0m" : s; }
std::string CLFAnsi::cyan(const std::string& s)      { return s_enabled ? "\033[36m" + s + "\033[0m" : s; }
std::string CLFAnsi::lightBlue(const std::string& s) { return s_enabled ? "\033[94m" + s + "\033[0m" : s; }
std::string CLFAnsi::yellow(const std::string& s)    { return s_enabled ? "\033[33m" + s + "\033[0m" : s; }
std::string CLFAnsi::red(const std::string& s)       { return s_enabled ? "\033[31m" + s + "\033[0m" : s; }
std::string CLFAnsi::gray(const std::string& s)      { return s_enabled ? "\033[90m" + s + "\033[0m" : s; }
std::string CLFAnsi::bold(const std::string& s)      { return s_enabled ? "\033[1m" + s + "\033[0m" : s; }

int CLFAnsi::terminalHeight() {
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

int CLFAnsi::terminalWidth() {
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

int CLFAnsi::textWidth(const std::string& text) {
    int width = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) ++width;
        else if ((c & 0xC0) == 0xC0) width += 2;
    }
    return width;
}

int CLFAnsi::wrappedLines(const std::string& text) {
    int W = terminalWidth();
    if (W <= 0) return 1;
    int lines = textWidth(text) / W + 1;
    return (lines > 0) ? lines : 1;
}

} // namespace CLF::CLFCore
