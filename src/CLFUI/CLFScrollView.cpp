// CLFScrollView.cpp — 滚动视口实现

#include "CLFUI/CLFScrollView.hpp"

#include <algorithm>
#include <string>

namespace CLF::CLFUI {

void CLFScrollView::update(int totalLines, int termHeight, int reservedLines) {
    // 自动滚动：新内容到达 → 回底部
    if (totalLines != m_lastTotalLines) {
        m_lastTotalLines = totalLines;
        if (m_autoScroll) m_scrollOffset = 0;
    }

    // 计算可见窗口
    m_viewH  = std::max(8, termHeight - reservedLines);
    m_maxOff = std::max(0, totalLines - m_viewH);
    if (m_scrollOffset < 0)      m_scrollOffset = 0;
    if (m_scrollOffset > m_maxOff) m_scrollOffset = m_maxOff;
}

ftxui::Elements CLFScrollView::renderWindow(const ftxui::Elements& allLines) {
    const int totalLines = static_cast<int>(allLines.size());

    // 截取可见行
    const int startLine = totalLines - m_viewH - m_scrollOffset;
    ftxui::Elements visible;
    for (int i = std::max(0, startLine);
         i < totalLines && (int)visible.size() < m_viewH; ++i) {
        visible.push_back(allLines[i]);
    }

    // 滚动位置指示
    if (m_scrollOffset > 0) {
        auto hint = "↑ " + std::to_string(m_scrollOffset) + " lines above";
        visible.insert(visible.begin(),
            ftxui::dim(ftxui::text("  " + hint)));
    }
    if (m_scrollOffset < m_maxOff) {
        auto hint = "↓ " + std::to_string(m_maxOff - m_scrollOffset) + " lines below";
        visible.push_back(ftxui::dim(ftxui::text("  " + hint)));
    }

    return visible;
}

bool CLFScrollView::handleEvent(ftxui::Event e) {
    // 鼠标滚轮
    if (e.is_mouse()) {
        auto& mouse = e.mouse();
        if (mouse.button == ftxui::Mouse::WheelUp) {
            m_scrollOffset += 3;
            m_autoScroll = false;
            return true;
        }
        if (mouse.button == ftxui::Mouse::WheelDown) {
            m_scrollOffset -= 3;
            if (m_scrollOffset <= 0) {
                m_scrollOffset = 0;
                m_autoScroll = true;
            }
            return true;
        }
    }
    // 键盘翻页
    if (e == ftxui::Event::PageUp) {
        m_scrollOffset += 15;
        m_autoScroll = false;
        return true;
    }
    if (e == ftxui::Event::PageDown) {
        m_scrollOffset -= 15;
        if (m_scrollOffset <= 0) {
            m_scrollOffset = 0;
            m_autoScroll = true;
        }
        return true;
    }
    if (e == ftxui::Event::Home) {
        m_scrollOffset = 999999;  // 由 update() clamp
        m_autoScroll = false;
        return true;
    }
    if (e == ftxui::Event::End) {
        m_scrollOffset = 0;
        m_autoScroll = true;
        return true;
    }
    return false;
}

void CLFScrollView::reset() {
    m_scrollOffset = 0;
    m_autoScroll   = true;
    m_lastTotalLines = 0;
}

} // namespace CLF::CLFUI
