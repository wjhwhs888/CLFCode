// CLFConfirmBar.cpp — 确认栏组件实现

#include "CLFUI/CLFConfirmBar.hpp"
#include "CLFUI/CLFTerminal.hpp"

namespace CLF::CLFUI {

ftxui::Element CLFConfirmBar::render(const CLFTerminal& terminal) const {
    if (!terminal.m_confirmActive)
        return ftxui::emptyElement();

    using namespace ftxui;

    auto opts = ftxui::hbox();
    for (size_t i = 0; i < terminal.m_confirmOpts.size(); ++i) {
        bool sel = (static_cast<int>(i) == terminal.m_confirmSel);
        auto marker = sel
            ? ftxui::bold(ftxui::text("●") | ftxui::color(ftxui::Color::Green))
            : ftxui::dim(ftxui::text("○"));
        opts = ftxui::hbox(
            std::move(opts),
            ftxui::text("  [") | ftxui::dim,
            marker,
            ftxui::text("] " + terminal.m_confirmOpts[i])
        );
    }
    opts = ftxui::hbox(
        std::move(opts),
        ftxui::filler(),
        ftxui::dim(ftxui::text("← → 选择  Enter 确认  Esc 取消"))
    );

    return ftxui::vbox({
        ftxui::separator(),
        ftxui::color(ftxui::Color::Yellow,
                     ftxui::paragraph("  ⚠ " + terminal.m_confirmPrompt)),
        opts,
    });
}

bool CLFConfirmBar::handleEvent(ftxui::Event e, CLFTerminal& terminal) const {
    if (!terminal.m_confirmActive) return false;

    if (e == ftxui::Event::Return) {
        terminal.m_confirmResult = true;
        terminal.m_confirmActive = false;
        terminal.m_confirmCv.notify_one();
        return true;
    }
    if (e == ftxui::Event::Escape) {
        terminal.m_confirmResult = false;
        terminal.m_confirmActive = false;
        terminal.m_confirmCv.notify_one();
        return true;
    }
    if (e == ftxui::Event::ArrowLeft || e == ftxui::Event::ArrowRight) {
        terminal.m_confirmSel = 1 - terminal.m_confirmSel;
        return true;
    }
    // confirm 期间屏蔽其他按键
    return false;
}

} // namespace CLF::CLFUI
