// qa_CLFInputSelection.cpp — 输入框拖选复制渲染级测试（2026-09-23）
// 取证背景（用户 todo.md 三轮实测）：复制粘贴已正确，但**选区高亮与复制
// 区间不一致**（拖选"123"高亮显示整行"12345"、多行末行高亮多一个字符）。
// 本套件用 ftxui::Render + Screen 检查反色像素——把"高亮 == 选区"钉为
// 回归断言（渲染级，不依赖终端）。
//
// 测试基建注意（项目既有教训）：qa 运行在 boost::ut 静态初始化期——事件
// 构造用聚合初始化（Event::Mouse 显式 Mouse 结构，不用静态 Event 常量）。

#include <boost/ut.hpp>

#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

using namespace boost::ut;

namespace {

// 构造鼠标事件（x, y 为全局屏幕坐标）。
// ⚠ input 必须**非空**（SGR 鼠标转义序列形态）：qa 运行在 boost::ut 静态
// 初始化期，Event::Return 等静态常量 input 为空——空 input 的鼠标事件
// == 未初始化的 Event::Return（空==空）会误走回车分支（项目静态期教训）
ftxui::Event mouseEvent(ftxui::Mouse::Button button, ftxui::Mouse::Motion motion,
                        int x, int y) {
    return ftxui::Event::Mouse(
        "\x1B[<0;1;2M", ftxui::Mouse{button, motion, false, false, false, x, y});
}

ftxui::Event press(int x, int y) {
    return mouseEvent(ftxui::Mouse::Left, ftxui::Mouse::Pressed, x, y);
}
ftxui::Event move(int x, int y) {
    return mouseEvent(ftxui::Mouse::Left, ftxui::Mouse::Moved, x, y);
}
ftxui::Event release(int x, int y) {
    return mouseEvent(ftxui::Mouse::Left, ftxui::Mouse::Released, x, y);
}

// 渲染 fixture：content + on_select 收集 + Screen（固定 30×5）
struct SelectionSetup {
    std::string content = "12345";
    std::string selected;
    ftxui::Component input;

    SelectionSetup() {
        ftxui::InputOption opt;
        opt.content = &content;
        opt.on_select = [&](std::string s) { selected = std::move(s); };
        // 对齐生产装配（CLFRepl.cpp）：自定义 transform 剥离 focus 背景——
        // 否则 Default transform 的焦点背景整行反色会掩盖选区高亮
        opt.transform = [](ftxui::InputState state) {
            return state.element;
        };
        input = ftxui::Input(std::move(opt));
    }

    ftxui::Screen render() {
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(30), ftxui::Dimension::Fixed(5));
        ftxui::Render(screen, input->Render());   // 组件 → Element → Screen
        return screen;
    }

    // 行 row 中反色像素的列集合（"高亮区间"）
    std::string highlightedColumns(const ftxui::Screen& screen, int row) {
        std::string out;
        for (int col = 0; col < 30; ++col) {
            if (screen.PixelAt(col, row).inverted) {
                out += static_cast<char>('0' + col % 10);
            }
        }
        return out;
    }
};

} // namespace

suite<"CLFInputSelection"> selectionSuite = [] {
    // 文本行位置：transform 剥离（对齐生产装配）后静态渲染的文本在
    // row 0（诊断实证）；事件坐标与像素断言均以文本行为准
    constexpr int kTextRow = 0;

    "IS1 单行拖选：高亮区间与复制区间一致（拖选 123）"_test = [] {
        SelectionSetup s;
        s.render();
        // 按下第 1 字符 → 拖到第 3 字符（列 2）→ 松手
        expect(s.input->OnEvent(press(0, kTextRow)) == true);
        s.render();
        expect(s.input->OnEvent(move(2, kTextRow)) == true);
        auto mid = s.render();
        // 高亮 = 列 0..2（"123"），列 3/4 不高亮
        expect(mid.PixelAt(0, kTextRow).inverted);
        expect(mid.PixelAt(1, kTextRow).inverted);
        expect(mid.PixelAt(2, kTextRow).inverted);
        expect(!mid.PixelAt(3, kTextRow).inverted);
        expect(!mid.PixelAt(4, kTextRow).inverted);
        expect(s.input->OnEvent(release(2, kTextRow)) == true);
        expect(s.selected == "123");
    };

    "IS2 反向拖选：高亮与复制区间一致（从右往左全选）"_test = [] {
        SelectionSetup s;
        s.render();
        expect(s.input->OnEvent(press(4, kTextRow)) == true);
        s.render();
        expect(s.input->OnEvent(move(0, kTextRow)) == true);
        auto mid = s.render();
        for (int c = 0; c < 5; ++c) {
            expect(mid.PixelAt(c, kTextRow).inverted);
        }
        expect(s.input->OnEvent(release(0, kTextRow)) == true);
        expect(s.selected == "12345");
    };

    "IS3 多行末行部分选择：高亮与复制区间一致"_test = [] {
        SelectionSetup s;
        s.content = "12345\n12345\n12345";
        s.render();
        // 从第 1 行第 2 字符（列 1）拖到第 3 行第 4 字符（列 3）
        expect(s.input->OnEvent(press(1, kTextRow)) == true);
        s.render();
        expect(s.input->OnEvent(move(3, kTextRow + 2)) == true);
        auto mid = s.render();
        // 第 3 行高亮 = 列 0..3（"1234"），列 4（"5"）不高亮
        expect(mid.PixelAt(0, kTextRow + 2).inverted);
        expect(mid.PixelAt(3, kTextRow + 2).inverted);
        expect(!mid.PixelAt(4, kTextRow + 2).inverted);
        expect(s.input->OnEvent(release(3, kTextRow + 2)) == true);
        expect(s.selected == "2345\n12345\n1234");
    };
};

int main() {}
