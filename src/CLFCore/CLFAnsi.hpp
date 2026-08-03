// CLFAnsi.hpp — ANSI 终端控制原语
// 颜色包装、树状符号输出、终端尺寸查询
//
// example:
//   CLFAnsi::enable();
//   std::cout << CLFAnsi::green("OK") << std::endl;
//   int w = CLFAnsi::terminalWidth();

#pragma once

#include <string>

namespace CLF::CLFCore {

class CLFAnsi {
public:
    // 启用 ANSI 转义序列（Windows 需一次，Unix 默认开启）
    static void enable();

    // 颜色
    static std::string green(const std::string& s);
    static std::string cyan(const std::string& s);
    static std::string lightBlue(const std::string& s);
    static std::string yellow(const std::string& s);
    static std::string red(const std::string& s);
    static std::string gray(const std::string& s);
    static std::string bold(const std::string& s);

    // 终端尺寸
    static int terminalHeight();
    static int terminalWidth();

    // 文本宽度（中文按 2 列）
    static int textWidth(const std::string& text);

    // 折行数估算
    static int wrappedLines(const std::string& text);

    static bool isEnabled() { return s_enabled; }

private:
    static bool s_enabled;
};

} // namespace CLF::CLFCore
