// CLFEncoding.hpp — 编码转换工具
// GBK/ACP ↔ UTF-8 转换，Windows 平台使用 MultiByteToWideChar/WideCharToMultiByte
// 合并自 CLFFileOps::toUtf8 与 CLFCommandExec::toUtf8 的重复实现
//
// example:
//   std::string utf8 = CLFEncoding::toUtf8(gbkString);

#pragma once

#include <string>

namespace CLF::CLFCore {

class CLFEncoding {
public:
    // 系统代码页（ACP/GBK）→ UTF-8
    static std::string toUtf8(const std::string& input);

    // UTF-8 → 系统代码页（ACP/GBK）
    static std::string fromUtf8(const std::string& utf8);
};

} // namespace CLF::CLFCore
