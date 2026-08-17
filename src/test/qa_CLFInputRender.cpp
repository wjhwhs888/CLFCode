// qa_CLFInputRender.cpp — 临时诊断：FTXUI 多行 Input 渲染验证（验收期取证）
// 验证输入框对含 '\n' 内容的行渲染是否正常

#include <boost/ut.hpp>

#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace boost::ut;

const boost::ut::suite<"CLFInputRender"> tests = [] {

    "多行内容渲染为多行"_test = [] {
        std::string content = "L1\nL2\nL3\nL4\nL5";
        ftxui::InputOption opt;
        opt.multiline = true;
        auto input = ftxui::Input(&content, "", opt);
        auto element = input->Render();

        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element));
        ftxui::Render(screen, element);
        std::string out = screen.ToString();

        // 每一行应出现在独立的屏幕行
        expect(out.find("L1") != std::string::npos);
        expect(out.find("L5") != std::string::npos);
        // L1 和 L2 不得在同一行
        size_t p1 = out.find("L1");
        size_t p2 = out.find("L2");
        size_t nl1 = out.find('\n', p1);
        expect(p2 > nl1);  // L2 必须在 L1 所在行之后的换行之后
        std::cerr << "=== screen dump ===\n" << out << "=== end ===\n";
    };

    "\\r 字符在输入框中如何渲染（取证实验）"_test = [] {
        std::string content = "L1\rL2\nL3";
        ftxui::InputOption opt;
        opt.multiline = true;
        auto input = ftxui::Input(&content, "", opt);
        auto element = input->Render();
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                            ftxui::Dimension::Fixed(10));
        ftxui::Render(screen, element);
        std::string out = screen.ToString();
        std::cerr << "=== cr-test dump ===\n" << out << "=== end ===\n";
    };

    "精确事件流模拟：restore 直接赋值 + OnEvent 逐字符 + 渲染"_test = [] {
        std::string content;
        ftxui::InputOption opt;
        opt.multiline = true;
        // 引用型 Ref（指针构造）——与应用修复后一致；拥有型 Ref 是 Input 内部副本，
        // 直接赋值不同步（根因：Ref(T t) 拥有型构造 + 默认拷贝）
        int cursorPosValue = 0;
        ftxui::Ref<int> cursorPos(&cursorPosValue);
        opt.cursor_position = cursorPos;
        opt.transform = [](ftxui::InputState state) { return state.element; };
        auto input = ftxui::Input(&content, "❯ ", opt);

        std::string line1 = "  ⎿ 终端: 高30 x 宽120, ANSI: 关";
        std::string line2 = "  ⎿ 工作目录: F:\\wjh_work\\ProjectCliom\\testCLFCode";
        std::string line3 = "  ⎿ 配置: https://api.deepseek.com";
        std::string line4 = "  ⎿ 模型: deepseek-v4-flash";
        std::string line5 = "  ⎿ 知识库: 5 skills";

        // line1 字符 PassThrough
        for (size_t i = 0; i < line1.size(); ++i)
            input->OnEvent(ftxui::Event::Character(std::string(1, line1[i])));
        // Return₁ → PENDING 捕获
        std::string pending = content;
        // line2 首字符 ' ' → RestoreAndAppendChar（与 CLFRepl 相同的直接赋值；
        // 注意：必须用解引用赋值 *cursorPos——Ref 整体赋值会触发拥有型构造覆盖指针）
        content = pending + "\n" + " ";
        *cursorPos = static_cast<int>(content.size());
        // line2 剩余字符
        for (size_t i = 1; i < line2.size(); ++i)
            input->OnEvent(ftxui::Event::Character(std::string(1, line2[i])));
        // Return₂ → InsertNewline
        input->OnEvent(ftxui::Event::Character("\n"));
        for (size_t i = 0; i < line3.size(); ++i)
            input->OnEvent(ftxui::Event::Character(std::string(1, line3[i])));
        input->OnEvent(ftxui::Event::Character("\n"));
        for (size_t i = 0; i < line4.size(); ++i)
            input->OnEvent(ftxui::Event::Character(std::string(1, line4[i])));
        input->OnEvent(ftxui::Event::Character("\n"));
        for (size_t i = 0; i < line5.size(); ++i)
            input->OnEvent(ftxui::Event::Character(std::string(1, line5[i])));

        expect(std::count(content.begin(), content.end(), '\n') == 4);

        {
            std::string vis;
            for (char c : content)
                vis += (c == '\n' ? std::string("\\n") : std::string(1, c));
            std::cerr << "=== content bytes: [" << vis << "] cursor="
                      << *cursorPos
                      << " size=" << content.size() << " ===\n";
        }

        auto element = ftxui::vbox({
            ftxui::text("x") | ftxui::flex,
            ftxui::separator(),
            input->Render(),
            ftxui::separator(),
            ftxui::text("m"),
        });
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(352),
                                            ftxui::Dimension::Fixed(25));
        ftxui::Render(screen, element);
        std::cerr << "=== event-sim dump ===\n" << screen.ToString()
                  << "=== end ===\n";
    };

    "二分对照：全部字符走 OnEvent（无直接赋值）"_test = [] {
        std::string content;
        ftxui::InputOption opt;
        opt.multiline = true;
        ftxui::Ref<int> cursorPos = 0;
        opt.cursor_position = cursorPos;
        opt.transform = [](ftxui::InputState state) { return state.element; };
        auto input = ftxui::Input(&content, "❯ ", opt);

        std::string full = "  ⎿ 终端: 高30 x 宽120, ANSI: 关\n"
                           "  ⎿ 工作目录: F:\\wjh_work\\ProjectCliom\\testCLFCode\n"
                           "  ⎿ 配置: https://api.deepseek.com\n"
                           "  ⎿ 模型: deepseek-v4-flash\n"
                           "  ⎿ 知识库: 5 skills";
        for (size_t i = 0; i < full.size(); ++i)
            input->OnEvent(ftxui::Event::Character(std::string(1, full[i])));

        auto element = ftxui::vbox({
            ftxui::text("x") | ftxui::flex,
            ftxui::separator(),
            input->Render(),
            ftxui::separator(),
            ftxui::text("m"),
        });
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(352),
                                            ftxui::Dimension::Fixed(25));
        ftxui::Render(screen, element);
        std::cerr << "=== all-onevent dump ===\n" << screen.ToString()
                  << "=== end ===\n";
    };

    "应用布局上下文 + 应用 inputOpt：多行输入在 vbox 中渲染"_test = [] {
        // 精确复现应用场景：5 行粘贴内容 + 352x25 窗口 + 应用 transform/cursor
        std::string content = "  ⎿ 终端: 高30 x 宽120, ANSI: 关\n"
                             "  ⎿ 工作目录: F:\\wjh_work\\ProjectCliom\\testCLFCode\n"
                             "  ⎿ 配置: https://api.deepseek.com\n"
                             "  ⎿ 模型: deepseek-v4-flash\n"
                             "  ⎿ 知识库: 5 skills";
        ftxui::InputOption opt;
        opt.multiline = true;
        ftxui::Ref<int> cursorPos = static_cast<int>(content.size());
        opt.cursor_position = cursorPos;
        // 与应用一致的 transform（去除焦点背景）
        opt.transform = [](ftxui::InputState state) { return state.element; };
        auto input = ftxui::Input(&content, "❯ ", opt);

        // 模拟应用布局：flex 内容区在上，输入框在下（352x25 窗口）
        auto element = ftxui::vbox({
            ftxui::text("content-row-0") | ftxui::flex,
            ftxui::separator(),
            input->Render(),
            ftxui::separator(),
            ftxui::text("mode-line"),
        });

        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(352),
                                            ftxui::Dimension::Fixed(25));
        ftxui::Render(screen, element);
        std::string out = screen.ToString();
        std::cerr << "=== app-layout dump ===\n" << out << "=== end ===\n";
    };
};

int main() {}
