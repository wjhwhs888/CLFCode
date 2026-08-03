// CLFTerminal.hpp — 终端 UI 工具（Claude Code 风格输出）
// 颜色/尺寸 → 委托 CLFAnsi；滚动缓冲 → 委托 CLFScrollBuffer
//
// example:
//   CLFTerminal::enableAnsi();
//   CLFTerminal::scrollPrint(CLFTerminal::cyan("hello"));

#pragma once

#include <string>
#include <vector>

namespace CLF::CLFCore {

class CLFScrollBuffer;

class CLFTerminal {
public:
    static void enableAnsi();

    // —— 颜色 ——
    static std::string green(const std::string& s);
    static std::string cyan(const std::string& s);
    static std::string lightBlue(const std::string& s);
    static std::string yellow(const std::string& s);
    static std::string red(const std::string& s);
    static std::string gray(const std::string& s);
    static std::string bold(const std::string& s);

    // —— 树状输出 ——
    static void item(const std::string& text);
    static void sub(const std::string& text);
    static void sub2(const std::string& text);
    static void ok(const std::string& text);
    static void fail(const std::string& text);
    static void info(const std::string& text);

    // —— 终端控制 ——
    static int getTerminalHeight();
    static int getTerminalWidth();
    static int textWidth(const std::string& text);
    static int wrappedLines(const std::string& text);
    static void moveCursor(int row, int col);
    static void clearLine();

    // —— 5 区布局 ——
    static void initLayout(const std::string& modeLabel);
    static void setScrollCollapsed(bool collapsed);
    static bool isScrollCollapsed();
    static void scrollPrint(const std::string& text);
    static void scrollAppend(const std::string& text);
    static void toContentArea();
    static void redrawAll();
    static std::string diagnosticInfo();
    static void drawStatusArea(const std::string& title, const std::string& content);
    static void drawInputArea(const std::string& text, int cursorPos = -1);
    static void drawModeArea(const std::string& mode);
    static void drawConfirmArea(const std::vector<std::string>& options, int selected);
    static void clearConfirmArea();
    static void thoughtMark(int seconds, int searchCount = 0, int readCount = 0);
    static void restoreScrollRegion();

private:
    static CLFScrollBuffer s_buffer;
    static std::string s_statusTitle;
    static std::string s_statusContent;
    static std::string s_inputText;
    static std::string s_modeLabel;
    static int  s_inputCursor;
    static bool s_inputDrawn;
    static bool s_confirmDrawn;
};

} // namespace CLF::CLFCore
