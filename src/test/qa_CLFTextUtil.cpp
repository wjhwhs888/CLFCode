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

    // ---- 渲染宽度（2026-09-20 拖选列偏移根因修复）----
    // renderCharWidth 与 FTXUI g_full_width_characters 同表：渲染 1 宽的
    // 多字节符号（⎿/●/❯）必须返回 1——选区列→字节换算与布局同表。
    // 注意与 charWidth（项目规则：多字节恒 2）的口径差异是刻意的：
    // 前者对齐渲染布局（选区用），后者保持既有换行/截断语义（qa 钉子）。
    "renderCharWidth：与 FTXUI 布局同表"_test = [] {
        expect(CLFTextUtil::renderCharWidth("abc", 1) == 1);            // ASCII
        expect(CLFTextUtil::renderCharWidth("中文", 0) == 2);           // CJK（U+4E2D 在宽表）
        expect(CLFTextUtil::renderCharWidth("⎿", 0) == 1);              // U+23BF 渲染 1 宽（根因符号）
        expect(CLFTextUtil::renderCharWidth("●", 0) == 1);              // U+25CF 渲染 1 宽
        expect(CLFTextUtil::renderCharWidth("❯", 0) == 1);              // U+276F 渲染 1 宽
        expect(CLFTextUtil::renderCharWidth("✅", 0) == 2);             // U+2705 在宽表
        expect(CLFTextUtil::renderCharWidth("🔒", 0) == 2);             // U+1F512 在宽表
        expect(CLFTextUtil::renderCharWidth("abc", 3) == 0);            // 越界兜底 0
    };

    "utf8CharLen：字节长度与坏字节兜底"_test = [] {
        expect(CLFTextUtil::utf8CharLen("abc", 0) == 1);
        expect(CLFTextUtil::utf8CharLen("中文", 0) == 3);
        expect(CLFTextUtil::utf8CharLen("⎿", 0) == 3);
        expect(CLFTextUtil::utf8CharLen("🔒", 0) == 4);
        expect(CLFTextUtil::utf8CharLen("中文", 1) == 1);   // 续字节位置：兜底 1（调用方不传续字节）
    };

    // charWidth 项目规则口径保持不变（❯ 计 2 的既有语义钉子）
    "charWidth 项目规则口径不变"_test = [] {
        expect(CLFTextUtil::charWidth(static_cast<unsigned char>('a')) == 1);
        expect(CLFTextUtil::charWidth(static_cast<unsigned char>(0xE2)) == 2);  // ⎿/●/❯ 首字节仍计 2
        expect(CLFTextUtil::charWidth(static_cast<unsigned char>(0xAB)) == 0);  // 续字节 0
    };

    // ---- 渲染口径折行（2026-09-22 启动横幅批次）----
    // renderDisplayWidth/wrapLines 与 renderCharWidth 同表；charWidth 口径的
    // displayWidth/substrByWidth 本体不动（上方钉子保持绿）。
    "renderDisplayWidth：渲染口径与 charWidth 口径差异"_test = [] {
        expect(CLFTextUtil::renderDisplayWidth("abc") == 3);
        expect(CLFTextUtil::renderDisplayWidth("中文") == 4);   // 宽表 2×2（两口径一致）
        expect(CLFTextUtil::renderDisplayWidth("⎿") == 1);      // 非宽多字节符号渲染 1 宽
        expect(CLFTextUtil::displayWidth("⎿") == 2);            // charWidth 口径对照
        expect(CLFTextUtil::renderDisplayWidth("❯ a") == 3);    // ❯(1) + 空格 + a
        expect(CLFTextUtil::renderDisplayWidth("\033[36m⎿\033[0m") == 1);  // ANSI 跳过
    };

    "renderSubstrByWidth：渲染口径切分（不劈半多字节）"_test = [] {
        expect(CLFTextUtil::renderSubstrByWidth("abcdef", 3) == "abc");
        expect(CLFTextUtil::renderSubstrByWidth("中文abc", 4) == "中文");  // 2+2 不劈半
        expect(CLFTextUtil::renderSubstrByWidth("⎿ 配置", 2) == "⎿ ");    // 渲染口径 1+1
        expect(CLFTextUtil::substrByWidth("⎿ 配置", 2) == "⎿");           // charWidth 口径对照（钉子）
        expect(CLFTextUtil::renderSubstrByWidth("abc", 0) == "");  // maxW=0 首字符即空串
        expect(CLFTextUtil::renderSubstrByWidth("\033[36mabcdef\033[0m", 3)
               == "\033[36mabc");                                   // ANSI 整体跳过不劈开
    };

    "wrapLines：渲染口径折行切分"_test = [] {
        using L = std::vector<std::string>;
        expect(CLFTextUtil::wrapLines("abc", 10) == L{"abc"});          // 不超宽整行
        expect(CLFTextUtil::wrapLines("abcdef", 3) == L({"abc", "def"}));
        expect(CLFTextUtil::wrapLines("中文ab", 4) == L({"中文", "ab"}));  // CJK 不劈半
        expect(CLFTextUtil::wrapLines("⎿⎿⎿", 2) == L({"⎿⎿", "⎿"}));        // 渲染口径：每 ⎿ 1 宽
        expect(CLFTextUtil::wrapLines("abc", 0) == L{"abc"});           // wrapW<=0 免折行
        // 块字行（步骤 0 实测最宽 58）：80 列不折 / 40 列折行（验收 §8-3 断言）
        const std::string blockLine =
            "  ██████╗██╗     ███████╗ ██████╗ ██████╗ ██████╗ ███████╗";
        expect(CLFTextUtil::renderDisplayWidth(blockLine) == 58);   // 与 ftxui::string_width 同值
        expect(CLFTextUtil::wrapLines(blockLine, 80) == L{blockLine});
        expect(CLFTextUtil::wrapLines(blockLine, 40).size() == 2);
        // 单字符宽度超 wrapW → fallback 单字节段：不断言字节内容，只钉
        // "已切分 + 每段非空"性质（防空段死循环；生产 wrapW 恒 ≥78 此路径不可达）
        const auto fallbackParts = CLFTextUtil::wrapLines("中文", 1);
        expect(fallbackParts.size() > 1);
        for (const auto& p : fallbackParts) expect(!p.empty());
    };
};

int main() {}
