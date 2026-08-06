// CLFConfirmBar.cpp — 确认栏组件渲染实现

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

} // namespace CLF::CLFUI
