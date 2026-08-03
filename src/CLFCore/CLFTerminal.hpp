// CLFTerminal.hpp — 6 区终端 UI
// 颜色/尺寸 → CLFAnsi; 缓冲 → CLFScrollBuffer
//
// 布局(自底向上):
//   H:   ③ ConfirmRegion  1行
//   H-1: ④ ModeLine       1行
//   H-2: 下分隔线
//   H-3~H-2-N: ⑤ InputRegion  N行(1+换行数,底锚定)
//   H-3-N: 上分隔线(随输入联动)
//   H-4-N~H-4-N-M: ⑥ StatusRegion  M行(1+树形)
//   以上: ⑦ ContentRegion  DECSTBM滚动区(纯显示器)

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

    // —— ⑦ ContentRegion (纯显示器) ——
    static void scrollPrint(const std::string& text);
    static void thoughtMark(int seconds, int searchCount = 0, int readCount = 0);

    // —— ⑥ StatusRegion (上分隔线上方) ——
    static void showThinking(int seconds);
    static void showWorking(const std::string& title);
    static void showTaskTree(const std::vector<std::string>& phases);
    static void clearStatus();

    // —— ⑤ InputRegion ——
    static void drawInput(const std::string& text, int cursorPos = -1);

    // —— ④ ModeLine ——
    static void drawMode(const std::string& mode);

    // —— ③ ConfirmRegion ——
    static void showConfirm(const std::vector<std::string>& options, int selected);
    static void hideConfirm();

    // —— 布局 ——
    static void initLayout(const std::string& modeLabel);
    static void redrawAll();
    static void restoreScrollRegion();
    static std::string diagnosticInfo();

    // —— 兼容旧 API (逐步迁移) ——
    static void drawInputArea(const std::string& text, int cp = -1) { drawInput(text, cp); }
    static void drawModeArea(const std::string& m)   { drawMode(m); }
    static void drawStatusArea(const std::string& t, const std::string& c);
    static void drawConfirmArea(const std::vector<std::string>& o, int s) { showConfirm(o, s); }
    static void clearConfirmArea() { hideConfirm(); }
    static void toContentArea();

private:
    // 布局计算
    static int inputLineCount();
    static int statusLineCount();
    static int contentBottom();
    static int upperSepRow();
    static int inputTopRow();
    static int lowerSepRow() { return getTerminalHeight() - 2; }
    static int modeRow()     { return getTerminalHeight() - 1; }
    static int confirmRow()  { return getTerminalHeight(); }

    static void recomputeLayout();
    static void drawSeparators();
    static void renderFixedArea();

    static CLFScrollBuffer s_buffer;
    static std::string s_inputText;
    static std::string s_modeLabel;
    static std::string s_statusLine;          // ⑥ 单行状态
    static std::vector<std::string> s_statusTree; // ⑥ 树形
    static std::vector<std::string> s_confirmOpts;
    static int  s_confirmSel;
    static int  s_inputCursor;
    static bool s_layoutValid;
};

} // namespace CLF::CLFCore
