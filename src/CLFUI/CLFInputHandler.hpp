// CLFInputHandler.hpp — REPL 事件处理器（批次 A1：自 CLFRepl::run() 的
// CatchEvent 闭包 516-855 纯搬移）
// 按键分发表：提交/粘贴/确认/选区/历史/Esc/Ctrl+C/Tab/滚动——事件消费语义
// （每分支 true/false）与搬移前逐一一致
//
// 纯搬移约定（设计-阶段1 §五 A1）：闭包捕获 → 构造注入引用，成员状态仍驻留
// CLFRepl（本类经 friend 访问）；选区命中测试委托 CLFReplView::hitTest。
//
// example:
//   CLFInputHandler inputHandler(repl, terminal, view, inputText, cursorPos,
//                                input, &screen, asyncSubmit, pasteCoalescer,
//                                dbgEvt, escDbg);
//   auto handler = ftxui::CatchEvent(ui, [&](ftxui::Event e) {
//       return inputHandler.handle(e);
//   });

#pragma once

#include <functional>
#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

namespace CLF::CLFUI {

class CLFRepl;
class CLFReplView;
class CLFTerminal;
class CLFAsyncSubmit;
class CLFPasteCoalescer;

// 拖选自动滚动定时事件（2026-09-20 记事本式边缘滚动——50ms tick；
// 每次构造 Special 而非静态常量：qa 静态初始化期执行教训，构造开销可忽略）
inline ftxui::Event dragTickEvent() {
    return ftxui::Event::Special("\x1B[DT");
}

class CLFInputHandler {
public:
    CLFInputHandler(CLFRepl& repl, CLFTerminal* terminal, CLFReplView& view,
                    std::string& inputText, ftxui::Ref<int> cursorPos,
                    ftxui::Component input, ftxui::ScreenInteractive* screen,
                    CLFAsyncSubmit& asyncSubmit, CLFPasteCoalescer& pasteCoalescer,
                    std::function<void(const std::string&)> dbgEvt,
                    std::function<std::string(const std::string&)> escDbg);

    // 事件分发主循环体（原 CatchEvent 闭包纯搬移；返回 true = 消费该事件）
    bool handle(ftxui::Event e);

private:
    CLFRepl& m_repl;
    CLFTerminal* m_terminal;
    CLFReplView& m_view;
    std::string& m_inputText;
    ftxui::Ref<int> m_cursorPos;
    ftxui::Component m_input;
    ftxui::ScreenInteractive* m_screen;
    CLFAsyncSubmit& m_asyncSubmit;
    CLFPasteCoalescer& m_pasteCoalescer;
    std::function<void(const std::string&)> m_dbgEvt;
    std::function<std::string(const std::string&)> m_escDbg;

    // 拖选自动滚动状态（2026-09-20 记事本式边缘滚动）：最后鼠标位置
    // （出终端窗口后事件停更、位置保持贴顶值 → 定时 tick 持续滚动）；
    // m_dragMoved 防误滚——单击第一行（Pressed 后无 Moved）不触发上滚
    int  m_dragLastX = -1;
    int  m_dragLastY = -1;
    bool m_dragMoved = false;
};

} // namespace CLF::CLFUI
