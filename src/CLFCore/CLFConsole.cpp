// CLFConsole.cpp — 控制台键盘输入实现

#include "CLFCore/CLFConsole.hpp"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace CLF::CLFCore {

namespace {

#ifdef _WIN32
DWORD g_originalMode = 0;
bool  g_rawModeActive = false;

// 读取一个原始字节
char readByte() {
    char c = 0;
    if (std::cin.get(c)) {
        return c;
    }
    return 0;
}
#else
#include <termios.h>
#include <unistd.h>
termios g_originalTermios;
bool g_rawModeActive = false;

char readByte() {
    char c = 0;
    if (::read(STDIN_FILENO, &c, 1) == 1) {
        return c;
    }
    return 0;
}
#endif

// UTF-8 多字节字符拼装（按前缀字节确定长度）
std::string readUtf8Char(char lead) {
    unsigned char uc = static_cast<unsigned char>(lead);
    int extra = 0;
    if ((uc & 0xE0) == 0xC0) extra = 1;
    else if ((uc & 0xF0) == 0xE0) extra = 2;
    else if ((uc & 0xF8) == 0xF0) extra = 3;

    std::string result(1, lead);
    for (int i = 0; i < extra; ++i) {
        result += readByte();
    }
    return result;
}

} // anonymous namespace

bool CLFConsole::enterRawMode() {
#ifdef _WIN32
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE) return false;
    if (!GetConsoleMode(hIn, &g_originalMode)) return false;
    // 去 LINE_INPUT / ECHO_INPUT / PROCESSED_INPUT（保留 ENABLE_VIRTUAL_TERMINAL_INPUT 以支持方向键序列）
    DWORD rawMode = g_originalMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    if (!SetConsoleMode(hIn, rawMode)) return false;
    g_rawModeActive = true;
    return true;
#else
    if (tcgetattr(STDIN_FILENO, &g_originalTermios) != 0) return false;
    termios raw = g_originalTermios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return false;
    g_rawModeActive = true;
    return true;
#endif
}

void CLFConsole::exitRawMode() {
#ifdef _WIN32
    if (g_rawModeActive) {
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        SetConsoleMode(hIn, g_originalMode);
        g_rawModeActive = false;
    }
#else
    if (g_rawModeActive) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_originalTermios);
        g_rawModeActive = false;
    }
#endif
}

CLFKeyEvent CLFConsole::readKey() {
    CLFKeyEvent ev;

    char c = readByte();
    if (c == 0) return ev;

    // Ctrl 组合（注意：Ctrl+M 与回车同为 0x0D，无法区分，故用 Ctrl+N 切换模式）
    if (c == 0x0F) { ev.m_key = CLFKey::CtrlO; return ev; }  // Ctrl+O
    if (c == 0x0E) { ev.m_key = CLFKey::CtrlN; return ev; }  // Ctrl+N
    if (c == 0x0D || c == 0x0A) { ev.m_key = CLFKey::Enter; return ev; } // 回车
    if (c == 0x03) { ev.m_key = CLFKey::CtrlC; return ev; }  // Ctrl+C
    if (c == 0x1B) {                                          // ESC（方向键序列或独立 Esc）
        char n1 = readByte();
        if (n1 == 0) { ev.m_key = CLFKey::Esc; return ev; }
        if (n1 == '[') {
            char n2 = readByte();
            switch (n2) {
                case 'A': ev.m_key = CLFKey::Up;    return ev;
                case 'B': ev.m_key = CLFKey::Down;  return ev;
                case 'C': ev.m_key = CLFKey::Right; return ev;
                case 'D': ev.m_key = CLFKey::Left;  return ev;
                default:  ev.m_key = CLFKey::Esc;   return ev;
            }
        }
        ev.m_key = CLFKey::Esc;
        return ev;
    }

    // 退格（0x08 或 0x7F）
    if (c == 0x08 || c == 0x7F) { ev.m_key = CLFKey::Backspace; return ev; }

    // 普通字符（UTF-8 多字节拼装）
    ev.m_key = CLFKey::Char;
    if (static_cast<unsigned char>(c) < 0x80) {
        ev.m_ch = c;
        ev.m_utf8 = std::string(1, c);
    } else {
        ev.m_utf8 = readUtf8Char(c);
    }
    return ev;
}

} // namespace CLF::CLFCore
