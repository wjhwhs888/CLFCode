// CLFScrollView.hpp — 滚动视口组件
// 管理内容区的滚动偏移、自动跟踪、可见窗口截取

#pragma once

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

namespace CLF::CLFUI {

class CLFScrollView {
public:
    // 每帧调用：根据 totalLines 更新内部状态
    void update(int totalLines, int termHeight, int reservedLines = 6);

    // 从 allLines 中截取可见窗口，返回带 scroll hint 的 Elements
    ftxui::Elements renderWindow(const ftxui::Elements& allLines);

    // 处理滚动相关事件（wheel/PageUp/PageDown/Home/End），返回 true=已消费
    bool handleEvent(ftxui::Event e);

    // P2-1/R5: 保持指定行可见（折叠块切换后防顶出视口）
    // lineIndex 为 allLines 下标；须在 update() 之后调用
    void keepLineVisible(int lineIndex);

    // 选区坐标映射：可见内容渲染行区间 [start, end)（不含滚动提示行）；
    // 提示行偏移由 topHintCount() 单独提供——两个口径分开，须在 update() 之后调用
    std::pair<int, int> visibleRange() const { return {m_startLine, m_endLine}; }
    int topHintCount() const { return m_scrollOffset > 0 ? 1 : 0; }

    // 重置（/clear 时调用）
    void reset();

private:
    int  m_scrollOffset   = 0;
    bool m_autoScroll     = true;
    int  m_lastTotalLines = 0;
    int  m_viewH          = 0;
    int  m_maxOff         = 0;
    int  m_startLine      = 0;   // 可见内容渲染行区间（update() 计算，renderWindow 消费）
    int  m_endLine        = 0;
};

} // namespace CLF::CLFUI
