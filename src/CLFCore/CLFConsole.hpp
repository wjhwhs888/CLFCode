// CLFConsole.hpp — 控制台键盘输入（原始模式）
// 逐字符读取 + 快捷键解析（方向键 / Ctrl 组合 / UTF-8 多字节）
//
// example:
//   CLF::CLFCore::CLFConsole::enterRawMode();
//   auto key = CLF::CLFCore::CLFConsole::readKey();
//   if (key.m_key == CLF::CLFCore::CLFKey::Enter) { ... }
//   CLF::CLFCore::CLFConsole::exitRawMode();

#pragma once

#include <string>

namespace CLF::CLFCore {

enum class CLFKey {
    None = 0,
    Char,       // 普通字符（m_utf8 有效）
    Enter,      // 回车（提交）
    Backspace,  // 退格
    Up, Down, Left, Right, // 方向键
    CtrlO,      // Ctrl+O（折叠/展开）
    CtrlN,      // Ctrl+N（切换模式，替代 Ctrl+Tab——系统级不可捕获；Ctrl+M 与回车同为 0x0D 无法区分）
    CtrlC,      // Ctrl+C（退出）
    Esc         // Esc（取消）
};

struct CLFKeyEvent {
    CLFKey  m_key  = CLFKey::None;
    char    m_ch   = 0;    // ASCII 字符时有效
    std::string m_utf8;    // 多字节字符（UTF-8 完整拼装）
};

class CLFConsole {
public:
    // 进入/退出原始模式（Windows: SetConsoleMode 去 LINE_INPUT/ECHO）
    static bool enterRawMode();
    static void exitRawMode();

    // 读取一个按键（阻塞），解析方向键序列（ESC [ A）、Ctrl 组合
    static CLFKeyEvent readKey();
};

} // namespace CLF::CLFCore
