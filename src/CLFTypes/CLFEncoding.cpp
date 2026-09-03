// CLFEncoding.cpp — 编码转换工具实现

#include "CLFTypes/CLFEncoding.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace CLF::CLFCore {

std::string CLFEncoding::toUtf8(const std::string& input) {
    if (input.empty()) return input;
#ifdef _WIN32
    int wideLen = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS,
                                       input.c_str(), static_cast<int>(input.size()),
                                       nullptr, 0);
    if (wideLen <= 0) return input; // 非 ACP 字符，原样返回（可能已是 UTF-8）

    std::wstring wide(wideLen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, input.c_str(), static_cast<int>(input.size()),
                        wide.data(), wideLen);

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideLen,
                                       nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return input;
    std::string utf8(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideLen,
                        utf8.data(), utf8Len, nullptr, nullptr);
    return utf8;
#else
    return input;
#endif
}

std::string CLFEncoding::fromUtf8(const std::string& utf8) {
    if (utf8.empty()) return utf8;
#ifdef _WIN32
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                       static_cast<int>(utf8.size()), nullptr, 0);
    if (wideLen <= 0) return utf8;

    std::wstring wide(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                        wide.data(), wideLen);

    int acpLen = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), wideLen,
                                      nullptr, 0, nullptr, nullptr);
    if (acpLen <= 0) return utf8;
    std::string acp(acpLen, '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), wideLen,
                        acp.data(), acpLen, nullptr, nullptr);
    return acp;
#else
    return utf8;
#endif
}

bool CLFEncoding::isValidUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { ++i; continue; }
        size_t extra;
        if      ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return false;
        if (i + extra >= s.size()) return false;
        for (size_t j = 1; j <= extra; ++j) {
            if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

// UTF-8 净化（批次 A2 自 CLFContext.cpp 归位，实现逐行搬移）：
// 从字节流中识别合法的 1~4 字节 UTF-8 序列，非法部分逐字节替换为 U+FFFD
std::string CLFEncoding::sanitizeUtf8(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        // ASCII
        if (c < 0x80) { out += static_cast<char>(c); ++i; continue; }
        // 2-byte sequence (C2..DF 80..BF)
        if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 < input.size() &&
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80)
            { out += input[i]; out += input[i+1]; i += 2; continue; }
        }
        // 3-byte sequence (E0..EF 80..BF 80..BF)
        else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 < input.size() &&
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80)
            {
                // 排除 overlong encodings
                if (c == 0xE0 && static_cast<unsigned char>(input[i+1]) < 0xA0) goto invalid;
                if (c == 0xED && static_cast<unsigned char>(input[i+1]) > 0x9F) goto invalid;
                out += input[i]; out += input[i+1]; out += input[i+2]; i += 3; continue;
            }
        }
        // 4-byte sequence (F0..F4 80..BF 80..BF 80..BF)
        else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 < input.size() &&
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+3]) & 0xC0) == 0x80)
            {
                if (c == 0xF0 && static_cast<unsigned char>(input[i+1]) < 0x90) goto invalid;
                if (c == 0xF4 && static_cast<unsigned char>(input[i+1]) > 0x8F) goto invalid;
                out += input[i]; out += input[i+1]; out += input[i+2]; out += input[i+3]; i += 4; continue;
            }
        }
    invalid:
        // 非法字节 → U+FFFD (3 bytes in UTF-8: EF BF BD)
        out += '\xEF'; out += '\xBF'; out += '\xBD';
        ++i;
    }
    return out;
}

} // namespace CLF::CLFCore
