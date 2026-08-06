// CLFConfirmBar.hpp — 确认栏组件（渲染 + 按键处理）
// 与 CLFTerminal::confirm() 配合：后者阻塞等待，本组件处理 UI 交互

#pragma once

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <string>
#include <vector>

namespace CLF::CLFUI {

class CLFTerminal;

class CLFConfirmBar {
public:
    // 渲染确认栏（active=false 时返回 emptyElement）
    ftxui::Element render(const CLFTerminal& terminal) const;

    // 处理 confirm 相关按键（Return/Esc/ArrowLeft/ArrowRight）
    // 返回 true 表示已消费
    bool handleEvent(ftxui::Event e, CLFTerminal& terminal) const;
};

} // namespace CLF::CLFUI
