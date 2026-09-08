// CLFAnsiParser.hpp — SGR 转义序列解析（渲染层样式分段）
// FTXUI 的 text() 经 Utf8ToGlyphs 会丢弃控制字符（ESC 被吞、参数字节残留乱码），
// 字符串内嵌 ANSI 转义无法直达终端——故在渲染层先把 CLFAnsi 生成的 SGR 转义
// 解析为样式分段，由 ReplView 映射为 ftxui::color/bold 装饰器（与状态点/modeLine
// 同机制，2026-09-08 根因修复 v2）
//
// example:
//auto segs = CLF::CLFUI::CLFAnsiParser::parse("\033[36m❯ \033[0m输入");
//// segs = [{text:"❯ ", bold:false, fg:36}, {text:"输入", bold:false, fg:-1}]

#pragma once

#include <string>
#include <vector>

namespace CLF::CLFUI {

struct CLFAnsiSegment {
    std::string text;
    bool bold = false;
    int fg = -1;   // ANSI 前景色码（31/36/90…）；-1 = 默认色
};

class CLFAnsiParser {
public:
    // 解析行内 SGR 转义为样式分段（不识别/截断的序列按普通字符保留）
    static std::vector<CLFAnsiSegment> parse(const std::string& line);

    // 移除全部 SGR 转义，返回纯文本（选区高亮用——字节偏移与显示宽度对齐）
    static std::string strip(const std::string& line);
};

} // namespace CLF::CLFUI
