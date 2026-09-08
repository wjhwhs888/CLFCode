// qa_CLFTextUtil.cpp — 公共文本工具单元测试（A2 收敛工具，2026-09-08 首次独立套件）
// 覆盖：displayWidth/substrByWidth 的 ANSI 转义跳过（ANSI enable 修复的配套——
// 颜色包装进内容流后宽度计算必须感知转义，否则硬换行/选区坐标错位）

#include <boost/ut.hpp>
#include "CLFTypes/CLFTextUtil.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFTextUtil;

suite qa_CLFTextUtil = [] {
    "displayWidth 纯文本回归"_test = [] {
        expect(CLFTextUtil::displayWidth("abc") == 3);
        expect(CLFTextUtil::displayWidth("中文") == 4);   // CJK 全角计 2
        expect(CLFTextUtil::displayWidth("a中b") == 4);
    };

    "displayWidth 跳过 SGR 转义（颜色包装不占宽）"_test = [] {
        expect(CLFTextUtil::displayWidth("\033[36mabc\033[0m") == 3);
        // ❯（U+276F）按项目规则计 2 宽（>=0xC0 首字节）+ 空格 1 = 3
        expect(CLFTextUtil::displayWidth("\033[1m\033[36m❯ \033[0m") == 3);
        expect(CLFTextUtil::displayWidth("\033[1m❯ \033[0m中文") == 7);  // 转义 + CJK 混合：3+4
        expect(CLFTextUtil::displayWidth("\033[90m14:32\033[0m") == 5);
    };

    "displayWidth 多段转义（bold 嵌套 cyan 同 CLFAnsi 实际输出形态）"_test = [] {
        // CLFAnsi::cyan(bold("❯ ")) 的展开形态：\033[36m\033[1m❯ \033[0m\033[0m
        expect(CLFTextUtil::displayWidth("\033[36m\033[1m❯ \033[0m\033[0m") == 3);
    };

    "displayWidth 不完整转义按普通字符（防御）"_test = [] {
        // 无终止字节的截断序列：不识别为完整转义，按字节计宽（4 字节）
        expect(CLFTextUtil::displayWidth("\033[36") == 4);
        // 孤立 ESC
        expect(CLFTextUtil::displayWidth("a\033b") == 3);
    };

    "substrByWidth 转义不占宽且包含进前缀"_test = [] {
        // 转义序列完整包含在返回前缀中（样式不丢），宽度只算可见字符
        expect(CLFTextUtil::substrByWidth("\033[36mabcdef\033[0m", 3)
               == "\033[36mabc");
        // ❯(2) + 空格(1) = 3 宽，正好整段返回；中(2) 不劈半
        expect(CLFTextUtil::substrByWidth("\033[1m❯ \033[0m中文", 3)
               == "\033[1m❯ \033[0m");
    };

    "substrByWidth 纯文本回归"_test = [] {
        expect(CLFTextUtil::substrByWidth("abcdef", 3) == "abc");
        expect(CLFTextUtil::substrByWidth("中文内容", 4) == "中文");
    };
};

int main() {}
