// CLFConsole.cpp — 控制台键盘输入实现
// Windows：_getch() 直接读（无缓冲、无 VT 干扰）
// Linux：POSIX read()

#include "CLFCore/CLFConsole.hpp"

#include <mutex>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>    // _getch
#else
#include <iostream>
#include <termios.h>
#include <unistd.h>
#endif

namespace CLF::CLFCore {

namespace {

#ifdef _WIN32
DWORD g_originalMode = 0;
bool  g_rawModeActive = false;

// _getch() 读取一个字节（控制台直接读取，无缓冲无回显）
char readByte() {
    int c = _getch();
    return (c == EOF) ? 0 : static_cast<char>(c);
}
#else
termios g_originalTermios;
bool    g_rawModeActive = false;

char readByte() {
    char c = 0;
    if (::read(STDIN_FILENO, &c, 1) == 1) {
        return c;
    }
    return 0;
}
#endif

// UTF-8 多字节拼装（从 readByte 逐字节读取续字节）
std::string readUtf8Char(char lead) {
    unsigned char uc = static_cast<unsigned char>(lead);
    int extra = 0;
    if      ((uc & 0xE0) == 0xC0) extra = 1;
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
    // 去 LINE_INPUT / ECHO_INPUT / PROCESSED_INPUT
    // _getch() 已自带无回显 + 逐字符，但设置原始模式可防止系统级缓冲干扰
    DWORD rawMode = g_originalMode;
    rawMode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    if (!SetConsoleMode(hIn, rawMode)) return false;
    g_rawModeActive = true;
    return true;
#else
    if (tcgetattr(STDIN_FILENO, &g_originalTermios) != 0) return false;
    termios raw = g_originalTermios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
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

#ifdef _WIN32

// ============ Windows：_getch() 实现 ============
// _getch() 直接读控制台，无 C++ 流缓冲，无 VT 翻译
// 方向键通过 0xE0 / 0x00 前缀识别（标准扩展键协议）

CLFKeyEvent CLFConsole::readKey() {
    CLFKeyEvent ev;

    int c = _getch();
    if (c == EOF) return ev;

    // —— 扩展键前缀（方向键、F 键等）——
    if (c == 0xE0 || c == 0x00) {
        int c2 = _getch();
        if (c2 == EOF) return ev;
        switch (c2) {
            case 0x48: ev.m_key = CLFKey::Up;    return ev;
            case 0x50: ev.m_key = CLFKey::Down;  return ev;
            case 0x4B: ev.m_key = CLFKey::Left;  return ev;
            case 0x4D: ev.m_key = CLFKey::Right; return ev;
            case 0x47: ev.m_key = CLFKey::Home;  return ev;
            case 0x4F: ev.m_key = CLFKey::End;   return ev;
            default: return ev;
        }
    }

    char ch = static_cast<char>(c);

    // —— Shift+Tab（Tab 字符 + Shift 状态检测）——
    if (ch == '\t' && (GetKeyState(VK_SHIFT) & 0x8000)) {
        ev.m_key = CLFKey::ShiftTab;
        return ev;
    }

    // —— Shift+Enter（输入换行符）——
    if ((ch == '\r' || ch == '\n') && (GetKeyState(VK_SHIFT) & 0x8000)) {
        ev.m_key = CLFKey::ShiftEnter;
        return ev;
    }

    // —— 回车（_getch 返回 \r）——
    if (ch == '\r' || ch == '\n') {
        ev.m_key = CLFKey::Enter;
        return ev;
    }

    // —— 退格 ——
    if (ch == '\b' || ch == 0x7F) {
        ev.m_key = CLFKey::Backspace;
        return ev;
    }

    // —— Escape ——
    if (ch == '\x1B') {
        ev.m_key = CLFKey::Esc;
        return ev;
    }

    // —— Ctrl 组合（_getch 直接返回控制字符 0x01-0x1A）——
    if (ch == 0x0F) { ev.m_key = CLFKey::CtrlO; return ev; }
    if (ch == 0x03) { ev.m_key = CLFKey::CtrlC; return ev; }

    // —— 普通字符 ——
    ev.m_key = CLFKey::Char;
    if (static_cast<unsigned char>(ch) < 0x80) {
        ev.m_ch   = ch;
        ev.m_utf8 = std::string(1, ch);
    } else {
        // 多字节 UTF-8（中文等）→ 读续字节拼装
        ev.m_utf8 = readUtf8Char(ch);
    }
    return ev;
}

bool CLFConsole::checkEscape() {
    // 互斥锁：防止 ThinkingIndicator 后台线程与 SSE 回调同时 _getch()
    static std::mutex s_checkMutex;
    std::lock_guard<std::mutex> lock(s_checkMutex);
    while (_kbhit()) {
        int c = _getch();
        if (c == 0x1B) return true;           // ESC 按下
        if (c == 0xE0 || c == 0x00) _getch(); // 扩展键 → 消费第二字节
    }
    return false;
}

#else // ============ Linux / macOS ============

CLFKeyEvent CLFConsole::readKey() {
    CLFKeyEvent ev;

    char c = readByte();
    if (c == 0) return ev;

    if (c == 0x0F) { ev.m_key = CLFKey::CtrlO; return ev; }
    if (c == 0x0D || c == 0x0A) { ev.m_key = CLFKey::Enter; return ev; }
    if (c == 0x03) { ev.m_key = CLFKey::CtrlC; return ev; }
    if (c == 0x1B) {
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
    if (c == 0x08 || c == 0x7F) { ev.m_key = CLFKey::Backspace; return ev; }

    ev.m_key = CLFKey::Char;
    if (static_cast<unsigned char>(c) < 0x80) {
        ev.m_ch   = c;
        ev.m_utf8 = std::string(1, c);
    } else {
        ev.m_utf8 = readUtf8Char(c);
    }
    return ev;
}

bool CLFConsole::checkEscape() {
    // Linux: 无 _kbhit 等价物，暂返回 false
    return false;
}

#endif

} // namespace CLF::CLFCore
