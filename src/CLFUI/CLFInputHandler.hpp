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
};

} // namespace CLF::CLFUI
