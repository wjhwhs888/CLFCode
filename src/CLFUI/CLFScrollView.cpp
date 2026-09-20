// CLFScrollView.cpp — 滚动视口实现

#include "CLFUI/CLFScrollView.hpp"

#include <algorithm>
#include <string>
#include <string_view>

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
    recalcWindow();
}

void CLFScrollView::recalcWindow() {
    // 滚动事件后立即重算可见区间（不等下一帧 update）——拖选期间滚轮
    // 滚动后随即 hitTest 扩展选区，visibleRange() 必须是滚动后的值
    // （2026-09-20 跨视口拖选需求）；首帧 update 前 m_viewH=0 防御跳过
    if (m_viewH <= 0) return;
    if (m_scrollOffset < 0)        m_scrollOffset = 0;
    if (m_scrollOffset > m_maxOff) m_scrollOffset = m_maxOff;

    // 可见内容渲染行区间（不含提示行——visibleRange() 口径）
    m_startLine = std::max(0, m_lastTotalLines - m_viewH - m_scrollOffset);
    m_endLine   = std::min(m_lastTotalLines, m_startLine + m_viewH);
}

ftxui::Elements CLFScrollView::renderWindow(const ftxui::Elements& allLines) {
    const int totalLines = static_cast<int>(allLines.size());

    // 截取可见行（区间由 update() 计算存储）
    ftxui::Elements visible;
    for (int i = m_startLine; i < m_endLine && i < totalLines; ++i) {
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

void CLFScrollView::stepScroll(bool up) {
    if (up) {
        m_scrollOffset += 3;
        m_autoScroll = false;
    } else {
        m_scrollOffset -= 3;
        if (m_scrollOffset <= 0) {
            m_scrollOffset = 0;
            m_autoScroll = true;
        }
    }
    recalcWindow();
}

bool CLFScrollView::handleEvent(ftxui::Event e) {
    // 鼠标滚轮（滚动后立即重算可见区间——拖选期间随后的 hitTest 需要新值）
    if (e.is_mouse()) {
        auto& mouse = e.mouse();
        if (mouse.button == ftxui::Mouse::WheelUp) {
            stepScroll(true);
            return true;
        }
        if (mouse.button == ftxui::Mouse::WheelDown) {
            stepScroll(false);
            return true;
        }
    }
    // 键盘翻页（input 字符串比较——不用 Event::PageUp 等静态常量：qa 在
    // boost::ut 套件构造期（静态初始化阶段）执行测试，静态 Event 常量尚未
    // 初始化（input 为空）致 == 恒 false/互相误匹配；实机主程序无此问题但
    // 与测试同码更可靠。序列与 FTXUI event.cpp 定义一致）
    if (e.input() == std::string_view("\x1B[5~")) {   // PageUp
        m_scrollOffset += 15;
        m_autoScroll = false;
        recalcWindow();
        return true;
    }
    if (e.input() == std::string_view("\x1B[6~")) {   // PageDown
        m_scrollOffset -= 15;
        if (m_scrollOffset <= 0) {
            m_scrollOffset = 0;
            m_autoScroll = true;
        }
        recalcWindow();
        return true;
    }
    if (e.input() == std::string_view("\x1B[H")) {    // Home
        m_scrollOffset = 999999;  // 由 recalcWindow() clamp
        m_autoScroll = false;
        recalcWindow();
        return true;
    }
    if (e.input() == std::string_view("\x1B[F")) {    // End
        m_scrollOffset = 0;
        m_autoScroll = true;
        recalcWindow();
        return true;
    }
    return false;
}

void CLFScrollView::keepLineVisible(int lineIndex) {
    // offset 为距底部行数：startLine = totalLines - viewH - offset
    // 令 lineIndex 落在窗口顶部 → offset = totalLines - viewH - lineIndex
    m_scrollOffset = m_lastTotalLines - m_viewH - lineIndex;
    if (m_scrollOffset < 0) m_scrollOffset = 0;  // 行已在底部窗口内
    m_autoScroll = false;
}

void CLFScrollView::reset() {
    m_scrollOffset = 0;
    m_autoScroll   = true;
    m_lastTotalLines = 0;
}

} // namespace CLF::CLFUI
