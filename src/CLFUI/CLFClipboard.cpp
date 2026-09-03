// CLFClipboard.cpp — 系统剪贴板实现

#include "CLFUI/CLFClipboard.hpp"
#include "CLFTypes/CLFEncoding.hpp"  // sanitizeUtf8（A2 归位：UI 不再依赖 core 的 Context 头）

#ifdef _WIN32
#include <windows.h>
#undef min
#undef max
#endif

namespace CLF::CLFUI {

std::string CLFClipboard::read() {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return "";
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); return ""; }
    wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(h));
    if (!wstr) { CloseClipboard(); return ""; }
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    std::string raw(len > 0 ? len - 1 : 0, '\0');
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &raw[0], len, nullptr, nullptr);
    GlobalUnlock(h);
    CloseClipboard();
    return CLF::CLFCore::CLFEncoding::sanitizeUtf8(raw);  // 防止截断 UTF-8 导致半字光标
#else
    return "";
#endif
}

void CLFClipboard::write(const std::string& text) {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wlen <= 0) { CloseClipboard(); return; }
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
    if (!hMem) { CloseClipboard(); return; }
    wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(hMem));
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wstr, wlen);
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
#endif
}

} // namespace CLF::CLFUI
