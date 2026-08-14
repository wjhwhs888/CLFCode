// CLFConfirmBar.hpp — 确认栏组件（渲染）
// 与 CLFTerminal::confirm() 配合：后者阻塞等待，本组件渲染 UI

#pragma once

#include <ftxui/dom/elements.hpp>

namespace CLF::CLFUI {

class CLFTerminal;

class CLFConfirmBar {
public:
    // 渲染确认栏（active=false 时返回 emptyElement）
    ftxui::Element render(const CLFTerminal& terminal) const;

    // P2-2: 拆分 prompt——首行 headline（琥珀加粗），其余 detail（dim）
    // 纯函数可单测（T8）
    struct PromptParts {
        std::string headline;
        std::string detail;  // 可能为空
    };
    static PromptParts splitPrompt(const std::string& prompt);
};

} // namespace CLF::CLFUI
