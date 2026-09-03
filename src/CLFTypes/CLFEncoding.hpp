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

    //校验字节序列是否为合法 UTF-8
    // 用于区分"UTF-8 文本"与"GBK/二进制内容"：前者直接采用，
    // 后者需转码或跳过（search 遇非法序列会污染 JSON 结果）
    // example:
    //   if (!CLFEncoding::isValidUtf8(raw)) skipFile();
    static bool isValidUtf8(const std::string& s);

    // UTF-8 净化：将非法字节序列替换为 U+FFFD (�)（批次 A2 自 CLFContext
    // 归位——剪贴板/输入边界防护等共享场景；与 isValidUtf8 配对：验证 vs 净化）
    // example:
    //   std::string clean = CLFEncoding::sanitizeUtf8(raw);
    static std::string sanitizeUtf8(const std::string& input);
};

} // namespace CLF::CLFCore
