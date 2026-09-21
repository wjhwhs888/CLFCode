// CLFReplView.hpp — REPL 渲染器（批次 A1：自 CLFRepl::run() 的 Renderer 闭包纯搬移）
// 行映射/wrap/高亮/spinner/状态点/状态栏/todo 面板/Tips——每帧渲染主循环体
//
// 纯搬移约定（设计-阶段1 §五 A1）：闭包捕获 → 构造注入引用，成员状态仍驻留
// CLFRepl（本类经 friend 访问）；m_scrollView 自 run() 局部搬入本类（值成员）。
// C4 才做状态封装收敛，本类不越界。
//
// example:
//   CLFReplView view(repl, terminal, inputText, input, confirmBar, asyncSubmit, dbgEvt, escDbg);
//   auto ui = ftxui::Renderer(root, [&] { return view.render(); });

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include "CLFUI/CLFScrollView.hpp"

namespace CLF::CLFUI {

class CLFRepl;
class CLFTerminal;
class CLFConfirmBar;
class CLFAsyncSubmit;

// CPR/ANSI 残留剥离（批次 A1 抽取：原 Repl.cpp 渲染侧/事件侧两处逐字同构——
// \033 被 CatchEvent 吃掉后残留 [n;mR 序列，统一在此剥离；返回自身引用便于链用）
std::string& stripCprResidual(std::string& inputText);

class CLFReplView {
public:
    CLFReplView(CLFRepl& repl, CLFTerminal* terminal, std::string& inputText,
                ftxui::Component input, CLFConfirmBar& confirmBar,
                CLFAsyncSubmit& asyncSubmit,
                std::function<void(const std::string&)> dbgEvt,
                std::function<std::string(const std::string&)> escDbg);

    // 每帧渲染主循环体（原 Renderer 闭包 178-484 纯搬移）
    ftxui::Element render();

    // 鼠标坐标 → (全局渲染行, 字符起始字节, 字符结尾字节)（原 hitTest lambda 搬移）
    // 顶提示行 clamp 到首行；内容区以下返回 nullopt 放行给 Input
    std::optional<std::tuple<int, int, int>> hitTest(int x, int y);

    // 滚动事件转发（CatchEvent §8 滚动段用；m_scrollView 保持私有）
    bool scrollHandleEvent(ftxui::Event e) { return m_scrollView.handleEvent(e); }

    // 拖选自动滚动（2026-09-20 记事本式边缘滚动）：
    // 贴顶判定（y<=0：内容区顶 = frame 顶，鼠标贴顶/拖出终端窗口后
    // 事件停更、最后位置保持贴顶 → 持续上滚）；下边缘 = 内容区底
    // （可见行 + 提示行，以下即输入框侧）。判定前先过 calibratePoint
    // 校准——JediTerm 组件层 y 含行号偏移（≈4），贴顶时原始 y≈4>0，
    // 不校准则上滚永不触发（2026-09-20 用户实机抓出）
    bool aboveContent(int y) const;
    bool belowContent(int y) const;
    // 单步滚动（±3 行与滚轮一致）+ 可见区间立即重算
    void autoScrollStep(bool up) { m_scrollView.stepScroll(up); }

private:
    // 命令候选面板（唯一维护定案 2026-09-21）：输入框内容以 '/' 开头且不含
    // 空格（纯命令前缀阶段）时，在输入框上方列出注册表前缀匹配的命令；
    // 纯展示不劫持按键（用户定案）。数据源 = CLFCommandDispatcher 注册表
    // 唯一权威源——新增命令只改 registerBuiltinCommands 一处。
    // 条件不满足 / 无匹配 → 空 Element 零占用（Tips 行同款惯例）。
    ftxui::Element buildCommandHintPanel(const std::string& inputText);

    // 拖选坐标校准（hitTest 与边缘判定共用）：Windows 自校准 + JediTerm
    // 行号偏移补偿——组件层坐标（已减 cursor 偏移）加回偏移再减 frame
    // 原点，输出 frame 内坐标（2026-09-09 引入，2026-09-20 抽出复用）
    void calibratePoint(int& x, int& y) const;
    CLFRepl&   m_repl;
    CLFTerminal* m_terminal;
    std::string& m_inputText;
    ftxui::Component m_input;
    CLFConfirmBar& m_confirmBar;
    CLFAsyncSubmit& m_asyncSubmit;
    std::function<void(const std::string&)> m_dbgEvt;
    std::function<std::string(const std::string&)> m_escDbg;
    CLFScrollView m_scrollView;   // 原 run() 局部 scrollView
};

} // namespace CLF::CLFUI
