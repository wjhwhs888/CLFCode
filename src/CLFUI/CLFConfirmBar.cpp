// CLFConfirmBar.cpp — 确认栏组件渲染实现

#include "CLFUI/CLFConfirmBar.hpp"
#include "CLFUI/CLFTerminal.hpp"

namespace CLF::CLFUI {

CLFConfirmBar::PromptParts CLFConfirmBar::splitPrompt(const std::string& prompt) {
    size_t nl = prompt.find('\n');
    if (nl == std::string::npos)
        return {prompt, ""};
    return {prompt.substr(0, nl), prompt.substr(nl + 1)};
}

ftxui::Element CLFConfirmBar::render(const CLFTerminal& terminal) const {
    if (!terminal.isConfirmActive())
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
        ftxui::dim(ftxui::text("← → 选择  Enter 确认/返回  Esc 返回"))
    );

    // P2-2: headline 琥珀加粗 / 参数 detail dim（dsh 审批卡模式）
    auto parts = splitPrompt(terminal.m_confirmPrompt);
    ftxui::Elements body;
    body.push_back(ftxui::hbox({
        ftxui::text("  ⚠ "),
        ftxui::text(parts.headline)
            | ftxui::bold
            | ftxui::color(ftxui::Color::Orange1),
    }));
    if (!parts.detail.empty())
        body.push_back(ftxui::dim(ftxui::paragraph("    " + parts.detail)));
    body.push_back(std::move(opts));

    ftxui::Elements all;
    all.push_back(ftxui::separator());
    all.insert(all.end(), body.begin(), body.end());
    return ftxui::vbox(std::move(all));
}

} // namespace CLF::CLFUI
