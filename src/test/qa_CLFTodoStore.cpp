// qa_CLFTodoStore.cpp — 待办清单存储测试（C2，2026-09-07）
// T1-T5: 读写副本语义 / 面板开关 / 脏标记 / 全完成判定

#include <boost/ut.hpp>

#include <string>
#include <vector>

#include "CLFCore/CLFTodoStore.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFTodoItem;
using CLF::CLFCore::CLFTodoStore;

namespace {

CLFTodoItem makeItem(const std::string& id, const std::string& content,
                     const std::string& status = "pending") {
    CLFTodoItem item;
    item.m_id = id;
    item.m_content = content;
    item.m_status = status;
    return item;
}

} // anonymous namespace

const boost::ut::suite<"CLFTodoStore"> tests = [] {
    "T1 读写往返 + 副本语义"_test = [] {
        CLFTodoStore store;
        expect(store.getTodos().empty());

        std::vector<CLFTodoItem> todos{makeItem("1", "task a"), makeItem("2", "task b")};
        store.setTodos(todos);
        expect(store.getTodos().size() == 2u);

        // 副本语义：修改入参不影响已存数据
        todos.push_back(makeItem("3", "task c"));
        expect(store.getTodos().size() == 2u);
    };

    "T2 面板显示开关"_test = [] {
        CLFTodoStore store;
        expect(!store.isTodoPanelDone());   // 默认 false（面板显示）
        store.setTodoPanelDone(true);
        expect(store.isTodoPanelDone());
        store.setTodoPanelDone(false);
        expect(!store.isTodoPanelDone());
    };

    "T3 脏标记：exchange 读取并清除"_test = [] {
        CLFTodoStore store;
        expect(!store.exchangeDirty());     // 初始干净
        store.markTodosDirty();
        expect(store.exchangeDirty());      // 读到脏并清除
        expect(!store.exchangeDirty());     // 已清
    };

    "T4 全完成判定：空清单 → 空快照"_test = [] {
        CLFTodoStore store;
        expect(store.allDoneSnapshot().empty());
    };

    "T5 全完成判定：未全完成 → 空快照"_test = [] {
        CLFTodoStore store;
        store.setTodos({makeItem("1", "done task", "completed"),
                        makeItem("2", "open task", "pending")});
        expect(store.allDoneSnapshot().empty());
    };

    "T6 全完成判定：全完成 → 快照副本"_test = [] {
        CLFTodoStore store;
        store.setTodos({makeItem("1", "task a", "completed"),
                        makeItem("2", "task b", "completed")});
        auto snap = store.allDoneSnapshot();
        expect(snap.size() == 2u);
        expect(snap[0].m_content == std::string("task a"));
    };
};

int main() {}
