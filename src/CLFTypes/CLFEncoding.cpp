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

} // namespace CLF::CLFCore
