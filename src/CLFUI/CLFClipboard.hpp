// CLFClipboard.hpp — 系统剪贴板读写工具
// Windows: CF_UNICODETEXT → UTF-8 / UTF-8 → CF_UNICODETEXT
// Linux: 返回空串 (未实现)

#pragma once

#include <string>

namespace CLF::CLFUI {

class CLFClipboard {
public:
    // 读取剪贴板文本（UTF-8），失败返回空串
    static std::string read();

    // 写入文本到剪贴板（UTF-8）
    static void write(const std::string& text);
};

} // namespace CLF::CLFUI
