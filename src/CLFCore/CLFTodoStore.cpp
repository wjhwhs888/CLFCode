// CLFTodoStore.cpp — 待办清单存储实现（C2 拆分，逻辑自 AgentLoop 原样搬移）

#include "CLFCore/CLFTodoStore.hpp"

namespace CLF::CLFCore {

std::vector<CLFTodoItem> CLFTodoStore::getTodos() const {
    std::lock_guard<std::mutex> lock(m_todosMutex);
    return m_todos;
}

void CLFTodoStore::setTodos(std::vector<CLFTodoItem> todos) {
    std::lock_guard<std::mutex> lock(m_todosMutex);
    m_todos = std::move(todos);
}

void CLFTodoStore::setTodoPanelDone(bool done) {
    m_todoPanelDone.store(done);
}

bool CLFTodoStore::isTodoPanelDone() const {
    return m_todoPanelDone.load();
}

void CLFTodoStore::markTodosDirty() {
    m_todoDirty.store(true);
}

bool CLFTodoStore::exchangeDirty() {
    return m_todoDirty.exchange(false);
}

std::vector<CLFTodoItem> CLFTodoStore::allDoneSnapshot() const {
    std::lock_guard<std::mutex> lock(m_todosMutex);
    if (m_todos.empty()) return {};
    for (const auto& t : m_todos)
        if (t.m_status != "completed") return {};
    return m_todos;
}

} // namespace CLF::CLFCore
