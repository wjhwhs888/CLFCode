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

    // 分屏初始化：设置内容区滚动区域（DECSTBM）+ 绘制底部输入框
    // 顶线 + 输入行 + 底线（右侧显示 modeLabel，如当前安全模式）
    // 光标停在输入位置。extraLines = 输入折行数（折行时输入区上移）
    static void setupSplitScreen(int extraLines = 1, const std::string& modeLabel = "");

    // 光标移到内容区（滚动区最后一行），内容输出从这里开始、自动滚动
    // 输入区固定不动
    static void toContentArea();

    // 恢复滚动区域并清屏（退出时调用）
    static void restoreScrollRegion();

private:
    static bool s_ansiEnabled;
    static int  s_contentBottomRow; // 滚动区最后一行（setupSplitScreen 记录）
};

} // namespace CLF::CLFCore
