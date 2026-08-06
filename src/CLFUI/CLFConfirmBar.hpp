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
};

} // namespace CLF::CLFUI
