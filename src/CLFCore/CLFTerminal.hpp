// CLFTerminal.hpp — 终端 UI 工具（Claude Code 风格输出）
// 提供 ANSI 颜色 + 树状层级符号，统一 REPL 显示效果
//
// example:
//   CLF::CLFCore::CLFTerminal::enableAnsi();
//   CLF::CLFCore::CLFTerminal::item("CLFCode 启动");
//   CLF::CLFCore::CLFTerminal::sub("项目根: " + CLFTerminal::cyan(path));

#pragma once

#include <string>

namespace CLF::CLFCore {

class CLFTerminal {
public:
    // Windows 启用 ANSI VT 处理（main 启动时调用一次）
    static void enableAnsi();

    // —— 颜色 ——
    static std::string green(const std::string& s);      // 成功/主标题
    static std::string cyan(const std::string& s);       // 信息/路径
    static std::string lightBlue(const std::string& s);  // 浅蓝（分隔线等）
    static std::string yellow(const std::string& s);     // 警告
    static std::string red(const std::string& s);        // 错误
    static std::string gray(const std::string& s);       // 次要信息
    static std::string bold(const std::string& s);       // 加粗

    // —— 树状输出（直接打印到 stdout）——
    static void item(const std::string& text);  // ● text
    static void sub(const std::string& text);   // ⎿ text
    static void sub2(const std::string& text);  //   ⎿ text
    static void ok(const std::string& text);    // ● ✓ text（成功）
    static void fail(const std::string& text);  // ● ✗ text（失败）
    static void info(const std::string& text);  // ● ⓘ text（提示）

    // —— 终端控制 ——

    // 获取终端行数（失败返回 -1）
    static int getTerminalHeight();

    // 获取终端列数（失败返回 -1）
    static int getTerminalWidth();

    // 估算文本显示宽度（中文等宽字符按 2 列计）
    static int textWidth(const std::string& text);

    // 计算文本在终端宽度下占用的行数（至少 1）
    static int wrappedLines(const std::string& text);

    // 移动光标到指定行/列（1-based，ANSI）
    static void moveCursor(int row, int col);

    // 清空当前行光标右侧
    static void clearLine();

    // 绘制底部输入框。extraLines = 输入折行数（默认 1，折行时框上移）
    // 顶线/底线之间留输入行，光标停在输入位置
    // 返回输入行行号（供读取输入后重绘）
    static int drawPromptBox(int extraLines = 1);

    // 清除输入框区域（含 extraLines 折行），恢复内容区
    static void clearPromptBox(int extraLines = 1);

private:
    static bool s_ansiEnabled;
};

} // namespace CLF::CLFCore
