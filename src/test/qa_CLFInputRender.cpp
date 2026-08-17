// qa_CLFInputRender.cpp — FTXUI 多行输入渲染回归测试
// 背景：粘贴首两行合并 bug 的根因定位（ftxui::Ref<int> 拥有型构造致光标不同步），
// 本套件钉死两条回归：① 多行内容渲染为多行；② 光标引用型 Ref 同步下，
// "restore 直接赋值 + OnEvent 逐字符"事件流产生正确换行内容；③ '\r' 在输入中
// 不产生换行（渲染为空格——警惕 \r\n 源粘贴混入 \r）。

#include <boost/ut.hpp>

#include <algorithm>
#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace boost::ut;

namespace {

// 渲染并提取屏幕各行（去掉 ANSI 序列，便于断言）
std::string renderToString(const ftxui::Element& element, int w, int h) {
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(w),
                                        ftxui::Dimension::Fixed(h));
    ftxui::Render(screen, element);
    return screen.ToString();
}

} // anonymous namespace

const boost::ut::suite<"CLFInputRender"> tests = [] {

    "多行内容渲染为多行"_test = [] {
        std::string content = "L1\nL2\nL3\nL4\nL5";
        ftxui::InputOption opt;
        opt.multiline = true;
        auto input = ftxui::Input(&content, "", opt);
        std::string out = renderToString(input->Render(), 40, 10);
        // L2 必须在 L1 所在行之后的换行之后
        size_t p1 = out.find("L1");
        size_t p2 = out.find("L2");
        expect(p1 != std::string::npos && p2 != std::string::npos);
        expect(p2 > out.find('\n', p1));
    };

    "restore 直接赋值 + OnEvent 事件流：内容换行位置正确（光标引用型 Ref）"_test = [] {
        std::string content;
        ftxui::InputOption opt;
        opt.multiline = true;
        // 引用型 Ref（指针构造）：直接赋值后光标与 Input 共享——
        // 拥有型 Ref(T t) 是 Input 内部副本，光标停在旧位置导致
        // 后续字符插在 '\n' 之前（验收实证 bug）
        int cursorPosValue = 0;
        ftxui::Ref<int> cursorPos(&cursorPosValue);
        opt.cursor_position = cursorPos;
        auto input = ftxui::Input(&content, "❯ ", opt);

        std::string line1 = "  ⎿ 终端: 高30 x 宽120, ANSI: 关";
        std::string line2 = "  ⎿ 工作目录: F:\\wjh_work\\ProjectCliom\\testCLFCode";
        std::string line3 = "  ⎿ 配置: https://api.deepseek.com";
        std::string line4 = "  ⎿ 模型: deepseek-v4-flash";
        std::string line5 = "  ⎿ 知识库: 5 skills";

        for (size_t i = 0; i < line1.size(); ++i)
            input->OnEvent(ftxui::Event::Character(std::string(1, line1[i])));
        std::string pending = content;                 // Return → PENDING 捕获
        content = pending + "\n" + " ";                // RestoreAndAppendChar 直接赋值
        *cursorPos = static_cast<int>(content.size()); // 必须同步（解引用赋值）
        for (size_t i = 1; i < line2.size(); ++i)
            input->OnEvent(ftxui::Event::Character(std::string(1, line2[i])));
        input->OnEvent(ftxui::Event::Character("\n")); // InsertNewline
        for (size_t i = 0; i < line3.size(); ++i)
            input->OnEvent(ftxui::Event::Character(std::string(1, line3[i])));
        input->OnEvent(ftxui::Event::Character("\n"));
        for (size_t i = 0; i < line4.size(); ++i)
            input->OnEvent(ftxui::Event::Character(std::string(1, line4[i])));
        input->OnEvent(ftxui::Event::Character("\n"));
        for (size_t i = 0; i < line5.size(); ++i)
            input->OnEvent(ftxui::Event::Character(std::string(1, line5[i])));

        // 字节级断言：4 个 '\n' 且各在正确位置（首行尾后、非被推到末尾）
        expect(std::count(content.begin(), content.end(), '\n') == 4);
        expect(content.find('\n') == line1.size());
        expect(*cursorPos == static_cast<int>(content.size()));  // 光标同步到末尾
    };

    "\\r 在输入中渲染为空格（不产生换行，警惕 \\r\\n 源混入）"_test = [] {
        std::string content = "L1\rL2\nL3";
        ftxui::InputOption opt;
        opt.multiline = true;
        auto input = ftxui::Input(&content, "", opt);
        std::string out = renderToString(input->Render(), 40, 10);
        // '\r' 不换行：L1 与 L2 同屏行；'\n' 正常换行：L3 独立
        size_t p1 = out.find("L1");
        size_t p2 = out.find("L2");
        size_t p3 = out.find("L3");
        expect(p2 < out.find('\n', p1));   // L2 在 L1 同一行
        expect(p3 > out.find('\n', p1));   // L3 在下一行
    };
};

int main() {}
