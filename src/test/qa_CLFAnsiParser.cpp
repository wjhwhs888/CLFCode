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
};

int main() {}
