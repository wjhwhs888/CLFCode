// CLFTerminal.hpp — 终端 UI 工具（Claude Code 风格输出）
// 提供 ANSI 颜色 + 树状层级符号，统一 REPL 显示效果
//
// example:
//   CLF::CLFCore::CLFTerminal::enableAnsi();
//   CLF::CLFCore::CLFTerminal::item("CLFCode 启动");
//   CLF::CLFCore::CLFTerminal::sub("项目根: " + CLFTerminal::cyan(path));

#pragma once

#include <string>
#include <vector>

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

    // ============ 5 区布局 ============
    // 1 滚动显示区（折叠/展开）  2 工作状态区  3 提示词输入区
    // 4 工作模式区              5 交互确认区（确认时显示）

    // 初始化布局（清屏 + 绘制固定区）
    static void initLayout(const std::string& modeLabel);

    // 滚动区折叠/展开（Ctrl+O 切换）
    static void setScrollCollapsed(bool collapsed);
    static bool isScrollCollapsed();

    // 输出到滚动区（输出后自动重绘固定区，保证 2-5 不被覆盖）
    // 返回后光标停在滚动区输出位置
    static void scrollPrint(const std::string& text);

    // 轻量追加：只更新滚动区最后一行（流式输出用，减少重绘闪烁）
    // 结束后需调用 scrollPrint("") 或重绘固定区
    static void scrollAppend(const std::string& text);

    // 终端诊断信息（启动时显示，排查布局问题）
    static std::string diagnosticInfo();

    // 各区域绘制（重绘前自动刷新区域行号，缩放自适应）
    static void drawStatusArea(const std::string& title, const std::string& content);
    static void drawInputArea(const std::string& text, int cursorPos = -1);
    static void drawModeArea(const std::string& mode);
    static void drawConfirmArea(const std::vector<std::string>& options, int selected);
    static void clearConfirmArea();

    // 恢复终端状态并清屏（退出时调用）
    static void restoreScrollRegion();

private:
    static bool s_ansiEnabled;
    static bool s_scrollCollapsed;      // 滚动区折叠状态
    static std::vector<std::string> s_scrollBuffer; // 滚动区内容缓冲
    static std::string s_statusTitle;   // 状态区标题
    static std::string s_statusContent; // 状态区内容
    static std::string s_inputText;     // 输入区文本
    static std::string s_modeLabel;     // 模式区标签
    static int  s_inputCursor;          // 输入光标位置（UTF-8 字符索引）
};

} // namespace CLF::CLFCore
