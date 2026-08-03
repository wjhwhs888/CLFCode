// CLFEvent.hpp — 事件类型定义
// 统一事件通道: 触发源→Event→主循环dispatch→CLFTerminal渲染

#pragma once

#include <string>
#include <vector>

namespace CLF::CLFCore {

enum class EventType {
    None = 0,

    // 输入
    KeyChar,           // { text=char } → 插入字符
    KeySubmit,         // {} → 提交输入
    KeyNewLine,        // {} → 插入换行
    KeyMoveLeft,       // {}
    KeyMoveRight,      // {}
    KeyMoveUp,         // {}
    KeyMoveDown,       // {}
    KeyHome,           // {}
    KeyEnd,            // {}
    KeyBackspace,      // {}
    KeyClearInput,     // {} → Esc
    KeyCycleMode,      // {} → Shift+Tab
    KeyInterrupt,      // {} → Ctrl+C (有输入时清空)
    KeyExit,           // {} → Ctrl+C (空输入时退出)

    // 内容
    ContentAppend,     // { text } → scrollPrint
    ContentNewline,    // {} → scrollPrint("\n")
    ContentThought,    // { i1=seconds, i2=searchCount, tree[0]=readCount } → thoughtMark

    // 状态
    StatusThinking,    // { i1=seconds }
    StatusClear,       // {}
    StatusWorking,     // { text }
    StatusTaskTree,    // { tree } → showTaskTree

    // 确认
    ConfirmShow,       // { tree=options, i1=selected }
    ConfirmHide,       // {}

    // 布局
    LayoutResize,      // { i1=width, i2=height }
    InputChanged,      // { text, i1=cursorPos }

    // 系统
    AppExit,
};

struct Event {
    EventType type = EventType::None;
    std::string text;
    int i1 = 0, i2 = 0;
    std::vector<std::string> tree;
};

} // namespace CLF::CLFCore
