// CLFTodoStore.hpp — 待办清单存储（C2：AgentLoop 拆角色，2026-09-07）
// 持有 todos 数据 + 回合级展示生命周期状态（B3 语义定案），锁随对象。
// 线程模型（设计-任务清单UI显示 §3.9）：todo_write handler 在 asyncSubmit
// 工作线程写、UI 主线程渲染读——getTodos 锁内拷贝返回副本。
// 无文件依赖（落盘序列化由调用方编排）。
//
// example:
//   CLFTodoStore store;
//   store.setTodos(parsedTodos);
//   for (const auto& t : store.getTodos()) show(t);

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "CLFTypes/CLFTypes.hpp"

namespace CLF::CLFCore {

class CLFTodoStore {
public:
    // 待办清单读写（线程安全，§3.9）
    // ⚠️ getTodos 返回副本：禁止对结果元素取引用/指针后跨语句使用（临时即亡）；
    // range-for（const auto& t : store.getTodos()）安全（生命周期延长）
    std::vector<CLFTodoItem> getTodos() const;
    void setTodos(std::vector<CLFTodoItem> todos);

    // todo 面板显示开关（B3 语义定案 2026-09-03）：= 回合级 todo 面板展示
    // 生命周期状态——core 在回合边界维护、UI 只读消费：
    //   清（面板显示）：todo_write create/update、resume 非全完成快照
    //   置（面板隐藏）：finishTurn 全完成收尾、beginTurnSession 新回合、
    //                  closeSessionAndReset、resume 全完成快照
    void setTodoPanelDone(bool done);
    bool isTodoPanelDone() const;

    // 脏标记（仅 create/update/clear 置位，list 不调）：决定 turn 行是否带 todos 快照
    void markTodosDirty();
    // 读取并清除（appendTurnLine 轮末消费）
    bool exchangeDirty();

    // 全完成判定（restoreSession / finishTurn 共用收敛）：
    // 清单非空且全部 completed → 返回快照副本；否则（空/未全完成）返回空向量
    std::vector<CLFTodoItem> allDoneSnapshot() const;

private:
    std::vector<CLFTodoItem> m_todos;
    mutable std::mutex      m_todosMutex;       // 工作线程写 ↔ 主线程渲染读（§3.9）
    std::atomic<bool>       m_todoPanelDone{false};  // 面板显示开关（工作线程置位 ↔ 渲染读）
    std::atomic<bool>       m_todoDirty{false};      // 本轮操作过 create/update/clear
};

} // namespace CLF::CLFCore
