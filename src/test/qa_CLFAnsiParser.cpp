// qa_CLFAnsiParser.cpp — SGR 转义解析单元测试（ANSI 颜色修复 v2 的渲染层解析器）
// 覆盖：单段/多段解析、嵌套形态、重置、非 SGR 序列防御、strip

#include <boost/ut.hpp>
#include "CLFUI/CLFAnsiParser.hpp"

using namespace boost::ut;
using CLF::CLFUI::CLFAnsiParser;

namespace {
// 断言分段序列的简化 helper：text 列表
std::vector<std::string> texts(const std::vector<CLF::CLFUI::CLFAnsiSegment>& segs) {
    std::vector<std::string> out;
    for (const auto& s : segs) out.push_back(s.text);
    return out;
}
} // anonymous namespace

suite qa_CLFAnsiParser = [] {
    "无转义直通：单段无样式"_test = [] {
        auto segs = CLFAnsiParser::parse("hello 世界");
        expect(segs.size() == 1);
        expect(segs[0].text == "hello 世界");
        expect(segs[0].fg == -1);
        expect(!segs[0].bold);
    };

    "单段青色（CLFAnsi::cyan 形态）"_test = [] {
        auto segs = CLFAnsiParser::parse("\033[36mCLFCode\033[0m");
        expect(texts(segs) == std::vector<std::string>{"CLFCode"});
        expect(segs[0].fg == 36);
        expect(!segs[0].bold);
    };

    "两段：青色前缀 + 默认正文（❯ 输入行实际形态）"_test = [] {
        auto segs = CLFAnsiParser::parse("\033[36m\033[1m❯ \033[0m\033[0m输入内容");
        expect(segs.size() == 2);
        expect(segs[0].text == "❯ ");
        expect(segs[0].fg == 36);
        expect(segs[0].bold);
        expect(segs[1].text == "输入内容");
        expect(segs[1].fg == -1);
        expect(!segs[1].bold);
    };

    "四段：青前缀 + 加粗正文 + 空格 + 灰时间戳"_test = [] {
        auto segs = CLFAnsiParser::parse(
            "\033[36m\033[1m❯ \033[0m\033[0m\033[1m内容\033[0m  \033[90m14:32\033[0m");
        // 空格在灰转义之前累积 → 独立无样式段（渲染 hbox 无视觉差异）
        expect(segs.size() == 4);
        expect(segs[0].text == "❯ " && segs[0].fg == 36 && segs[0].bold);
        expect(segs[1].text == "内容" && segs[1].fg == -1 && segs[1].bold);
        expect(segs[2].text == "  " && segs[2].fg == -1 && !segs[2].bold);
        expect(segs[3].text == "14:32" && segs[3].fg == 90 && !segs[3].bold);
    };

    "red（错误行）"_test = [] {
        auto segs = CLFAnsiParser::parse("\033[31m[Error]\033[0m");
        expect(segs.size() == 1);
        expect(segs[0].fg == 31);
    };

    "不完整转义按普通字符保留（不吞内容）"_test = [] {
        // 无终止字节：ESC 保留，其余按普通字符（防御：不丢内容）
        auto segs = CLFAnsiParser::parse("a\033[36b");
        expect(segs.size() == 1);
        expect(segs[0].text == "a\033[36b");
        expect(segs[0].fg == -1);
    };

    "空转义串"_test = [] {
        expect(CLFAnsiParser::parse("").empty());
    };

    "strip 移除全部 SGR"_test = [] {
        expect(CLFAnsiParser::strip("\033[36m\033[1m❯ \033[0m\033[0m输入")
               == "❯ 输入");
        expect(CLFAnsiParser::strip("无转义") == "无转义");
        // 不完整序列不剥离（与 parse 同防御）
        expect(CLFAnsiParser::strip("a\033[36b") == "a\033[36b");
    };

    "isSgrSequence 判定（CLFTerminal 内容流过滤白名单）"_test = [] {
        expect(CLFAnsiParser::isSgrSequence("\033[36m"));
        expect(CLFAnsiParser::isSgrSequence("\033[0m"));
        expect(CLFAnsiParser::isSgrSequence("\033[1;36m"));
        expect(CLFAnsiParser::isSgrSequence("\033[m"));   // 空参 = 0
        expect(!CLFAnsiParser::isSgrSequence("\033]0;title\007"));  // OSC 标题
        expect(!CLFAnsiParser::isSgrSequence("\033[2J"));  // 清屏 CSI 非 m 终止
        expect(!CLFAnsiParser::isSgrSequence("\033[36"));  // 截断
        expect(!CLFAnsiParser::isSgrSequence("abc"));
        expect(!CLFAnsiParser::isSgrSequence(""));
    };

    // ---- 嵌套语义固化（2026-09-22 启动横幅批次，Q3 裁决：立书写约定）----
    // F2 裁决：flush 先于 applyCode + 落段值快照 → 单段包裹双序等价；
    // 真失效形态 = 外层包装内拼接多段（内层全清 reset 抹掉外层属性，
    // 与真实终端一致）。
    // ⚠ qa 静态期陷阱：套件在静态初始化期执行，CLFAnsi::s_enabled 恒 false，
    // 生产包装会退化为裸串——必须写字面转义序列（CLFAnsi 包装的展开形态，
    // 与上方"两段"用例注释一致；probe 运行时验证已走真机生产链路）。
    "双序等价：cyan(bold(x)) ≡ bold(cyan(x))"_test = [] {
        const std::string seqA = "\033[36m\033[1mx\033[0m\033[0m";  // cyan(bold(x)) 展开
        const std::string seqB = "\033[1m\033[36mx\033[0m\033[0m";  // bold(cyan(x)) 展开
        auto segsA = CLFAnsiParser::parse(seqA);
        auto segsB = CLFAnsiParser::parse(seqB);
        expect(segsA.size() == segsB.size());
        if (segsA.size() == segsB.size()) {
            for (size_t i = 0; i < segsA.size(); ++i) {
                expect(segsA[i].text == segsB[i].text);
                expect(segsA[i].bold == segsB[i].bold);
                expect(segsA[i].fg == segsB[i].fg);
            }
        }
        expect(segsA.size() == 1);
        expect(segsA[0].text == "x" && segsA[0].bold && segsA[0].fg == 36);
    };

    "跨段拼接：外层属性不跨 reset 恢复（与真实终端一致）"_test = [] {
        // bold(cyan(A)+gray(B)) 展开：内层全清 reset 抹掉外层 bold → B 丢 bold。
        // 钉住现有行为 = 真实终端语义（修法是书写约定：包裹内不拼接多段，
        // 而非改 parser——F2 裁决）
        auto segs = CLFAnsiParser::parse(
            "\033[1m\033[36mA\033[0m\033[90mB\033[0m\033[0m");
        expect(segs.size() == 2);
        expect(segs[0].text == "A" && segs[0].bold && segs[0].fg == 36);
        expect(segs[1].text == "B" && !segs[1].bold && segs[1].fg == 90);
    };
};

int main() {}
