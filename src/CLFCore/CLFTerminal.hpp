// CLFTerminal.hpp — 6 区终端 UI 工具
// 颜色/尺寸 → 委托 CLFAnsi；滚动缓冲 → 委托 CLFScrollBuffer
//
// 布局（自底向上）：
//   Row H:     ① ConfirmRegion   确认区 (1行, 空/菜单)
//   Row H-1:   ② ModeLine        模式区 (1行, auto/edit + 提示)
//   Row H-2:   ③ 下分隔线
//   Row H-3~H-2-inputLines: ④ InputRegion  输入区 (1+行, 底锚定, 向上扩展)
//   Row H-3-inputLines:     ⑤ 上分隔线      (随输入区联动)
//   Row H-4-inputLines~...: ⑥ StatusRegion 状态区 (1+行, 树形可扩展)
//   以上:      ⑦ ContentRegion  滚动区 (DECSTBM, 纯显示器)

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

    // —— 终端控制 ——
    static int getTerminalHeight();
    static int getTerminalWidth();
    static int textWidth(const std::string& text);
    static int wrappedLines(const std::string& text);
    static void moveCursor(int row, int col);
    static void clearLine();

    // —— 布局 ——
    static void initLayout(const std::string& modeLabel);

    // —— ContentRegion (纯显示器) ——
    static void scrollPrint(const std::string& text);
    static void redrawAll();

    // —— InputRegion ——
    static void drawInputArea(const std::string& text, int cursorPos = -1);

    // —— StatusRegion (上分隔线上方) ——
    static void drawStatusLine(const std::string& text);  // 单行: "· Thinking… (3s)"
    static void drawStatusTree(const std::vector<std::string>& lines); // 树形展开
    static void clearStatusLine();

    // —— ModeLine ——
    static void drawModeLine(const std::string& mode);

    // —— ConfirmRegion ——
    static void drawConfirmBar(const std::vector<std::string>& options, int selected);
    static void clearConfirmBar();

    // —— 工具输出 ——
    static void item(const std::string& text);
    static void sub(const std::string& text);
    static void sub2(const std::string& text);
    static void ok(const std::string& text);
    static void fail(const std::string& text);
    static void info(const std::string& text);
    static void thoughtMark(int seconds, int searchCount = 0, int readCount = 0);
    static void restoreScrollRegion();
    static std::string diagnosticInfo();

    // 兼容旧接口
    static void setScrollCollapsed(bool collapsed);
    static bool isScrollCollapsed();
    static void scrollAppend(const std::string& text);
    static void toContentArea();
    static void drawStatusArea(const std::string& title, const std::string& content);
    static void drawModeArea(const std::string& mode);
    static void drawConfirmArea(const std::vector<std::string>& options, int selected);
    static void clearConfirmArea();

private:
    // 布局计算
    static int inputLines(const std::string& text);
    static int statusLines();
    static int fixedHeight();
    static int contentBottom();

    static void recomputeLayout();
    static void renderFixedArea();

    static CLFScrollBuffer s_buffer;
    static std::string s_inputText;
    static std::string s_modeLabel;
    static std::string s_statusText;        // 状态区内容
    static std::vector<std::string> s_statusTree; // 状态区树形
    static int  s_inputCursor;
    static bool s_inputDrawn;
    static bool s_confirmDrawn;
};

} // namespace CLF::CLFCore
