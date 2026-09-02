// qa_CLFTodoPanel.cpp — 任务面板行构建单元测试（设计-任务清单UI显示 §4.2，2026-09-02）
// P1: 空清单 → 空行集（零占用）
// P2: panelDone=true（已收尾）→ 空行集
// P3: 标题行计数（完成/总数）
// P4: 三态图标与颜色映射
// P5: 溢出截断（>10 项）
// P6: 未知状态按 pending 兜底 + 空内容兜底

#include <boost/ut.hpp>

#include <string>
#include <vector>

#include "CLFUI/CLFRepl.hpp"

using namespace boost::ut;
using CLF::CLFUI::buildTodoPanelLines;
using CLF::CLFUI::CLFTodoPanelLine;
using CLF::CLFCore::CLFTodoItem;

const boost::ut::suite<"CLFTodoPanel"> tests = [] {
    "P1 空清单 → 空行集（零占用）"_test = [] {
        expect(buildTodoPanelLines({}, false).empty());
    };

    "P2 panelDone=true → 空行集（已收尾不显示）"_test = [] {
        const std::vector<CLFTodoItem> todos{{"1", "任务", "completed"}};
        expect(buildTodoPanelLines(todos, true).empty());
    };

    "P3 标题行计数正确"_test = [] {
        const std::vector<CLFTodoItem> todos{
            {"1", "任务1", "completed"},
            {"2", "任务2", "in_progress"},
            {"3", "任务3", "pending"},
        };
        const auto lines = buildTodoPanelLines(todos, false);
        expect(lines.size() == 4_ul);          // 标题 + 3 项
        expect(lines[0].text == std::string("📋 任务清单 1/3"));
        expect(lines[0].color == ftxui::Color::Default);
    };

    "P4 三态图标与颜色映射"_test = [] {
        const std::vector<CLFTodoItem> todos{
            {"1", "进行中", "in_progress"},
            {"2", "已完成", "completed"},
            {"3", "待办", "pending"},
        };
        const auto lines = buildTodoPanelLines(todos, false);
        expect(lines[1].text.find("⏳") != std::string::npos);
        expect(lines[1].color == ftxui::Color::CyanLight);
        expect(lines[2].text.find("✓") != std::string::npos);
        expect(lines[2].color == ftxui::Color::GreenLight);
        expect(lines[3].text.find("○") != std::string::npos);
        expect(lines[3].color == ftxui::Color::GrayDark);
    };

    "P5 溢出截断：>10 项末行省略"_test = [] {
        std::vector<CLFTodoItem> todos;
        for (int i = 1; i <= 15; ++i)
            todos.push_back({std::to_string(i), "任务" + std::to_string(i), "pending"});
        const auto lines = buildTodoPanelLines(todos, false);
        expect(lines.size() == 12_ul);         // 标题 + 10 项 + 省略行
        expect(lines.back().text.find("还有 5 项") != std::string::npos);
        expect(lines.back().color == ftxui::Color::GrayDark);
    };

    "P6 未知状态按 pending 兜底 + 空内容兜底"_test = [] {
        const std::vector<CLFTodoItem> todos{
            {"1", "", "weird_status"},
            {"2", "正常", "pending"},
        };
        const auto lines = buildTodoPanelLines(todos, false);
        expect(lines[1].text.find("○") != std::string::npos);        // 未知 → pending 图标
        expect(lines[1].text.find("(无内容)") != std::string::npos); // 空内容兜底
        expect(lines[2].text.find("正常") != std::string::npos);
    };
};

int main() {
    return 0;
}
