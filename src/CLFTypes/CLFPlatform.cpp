// CLFPlatform.cpp — 平台能力层实现（设计-平台层收敛 §四，2026-09-23）
// 实现均为自各调用点**纯搬移**（取数逻辑原样，算式留调用方）：
//   executableDir        ← CLFConfigLoader.cpp:139-153（u8string 口径）
//   enableVirtualTerminal ← CLFAnsi.cpp:16-30
//   consoleSize          ← CLFAnsi.cpp:38-66（两函数合一）
//   setRawInputMode      ← CLFRepl.cpp:243-257
//   initConsoleEncoding  ← main.cpp:52-55
//   consoleCursorPosition ← CLFReplView.cpp:529-533（取数部分，1 基）
//   read/writeClipboard  ← CLFClipboard.cpp 全文件
//   动态库后缀 / 临时文件 ← 新原语（.dll 硬编码 ×2、/tmp 硬编码收敛）
// 非 Windows 分支：[未验证]——Windows 构建不覆盖；第二期真编译（§十五）。

#include "CLFTypes/CLFPlatform.hpp"
#include "CLFTypes/CLFEncoding.hpp"   // readClipboard 的 sanitizeUtf8

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX   // windows.h min/max 宏污染（项目踩坑表 13.5）
#endif
#include <windows.h>
#else
// [未验证] 非 Windows 分支：Windows 构建不覆盖
#include <sys/ioctl.h>   // ioctl(TIOCGWINSZ)
#include <unistd.h>      // readlink / isatty / getpid / geteuid
#include <climits>       // PATH_MAX（E 类缺陷：原 ConfigLoader/PluginManager 缺显式包含）
#endif

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace CLF::CLFCore {

std::string CLFPlatform::executableDir() {
#ifdef _WIN32
    // W 版本 + u8string：A 版本按 ANSI 代码页读取路径，exe 位于中文目录时乱码
    wchar_t wbuf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, wbuf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return fs::path(wbuf).parent_path().u8string();
    }
    return "";
#else
    // [未验证] 非 Windows 分支：依赖 /proc/self/exe（Linux 专有——macOS 无
    // /proc，第二期需平台再分流）
    char buf[PATH_MAX];
    const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return fs::path(buf).parent_path().string();
    }
    return "";
#endif
}

const char* CLFPlatform::dynamicLibraryExtension() {
#ifdef _WIN32
    return ".dll";
#else
    return ".so";   // [未验证] 非 Windows 分支
#endif
}

unsigned CLFPlatform::suppressErrorDialogs() {
#ifdef _WIN32
    return static_cast<unsigned>(
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX));
#else
    return 0;   // [未验证] 非 Windows 无系统弹窗概念（no-op）
#endif
}

void CLFPlatform::restoreErrorDialogs(unsigned oldMode) {
#ifdef _WIN32
    SetErrorMode(oldMode);
#else
    (void)oldMode;   // [未验证] no-op
#endif
}

bool CLFPlatform::enableVirtualTerminal() {
#ifdef _WIN32
    const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return false;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return false;
    return SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    // [未验证] 非 Windows 分支：探测 isatty（§十.3 缺陷修正——原 CLFAnsi
    // 无条件置真，重定向到文件时仍注入转义码）
    return isatty(STDOUT_FILENO) != 0;
#endif
}

bool CLFPlatform::consoleSize(int& columns, int& rows) {
#ifdef _WIN32
    const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return false;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(hOut, &info)) return false;
    columns = static_cast<int>(info.srWindow.Right - info.srWindow.Left + 1);
    rows    = static_cast<int>(info.srWindow.Bottom - info.srWindow.Top + 1);
    return true;
#else
    // [未验证] 非 Windows 分支：依赖 ioctl(TIOCGWINSZ)
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) return false;
    columns = static_cast<int>(ws.ws_col);
    rows    = static_cast<int>(ws.ws_row);
    return true;
#endif
}

bool CLFPlatform::setRawInputMode() {
#ifdef _WIN32
    const HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE) return false;
    DWORD mode = 0;
    if (!GetConsoleMode(hIn, &mode)) return false;
    return SetConsoleMode(hIn, mode & ~ENABLE_PROCESSED_INPUT) != 0;
#else
    return true;   // [未验证] 非 Windows 无 ENABLE_PROCESSED_INPUT 概念（no-op）
#endif
}

bool CLFPlatform::initConsoleEncoding() {
#ifdef _WIN32
    // UTF-8 代码页：stdin 接收 UTF-8 输入、stdout 输出 UTF-8（GetACP 不变——
    // 仅影响控制台 I/O，08-31 编码修复结论）
    const bool okIn  = SetConsoleCP(CP_UTF8) != 0;
    const bool okOut = SetConsoleOutputCP(CP_UTF8) != 0;
    return okIn && okOut;
#else
    return true;   // [未验证] POSIX 本即 UTF-8（no-op）
#endif
}

bool CLFPlatform::consoleCursorPosition(int& x, int& y) {
#ifdef _WIN32
    const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return false;
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (!GetConsoleScreenBufferInfo(hOut, &csbi)) return false;
    x = static_cast<int>(csbi.dwCursorPosition.X) + 1;   // 0 基 → 1 基
    y = static_cast<int>(csbi.dwCursorPosition.Y) + 1;
    return true;
#else
    (void)x; (void)y;
    return false;   // [未验证] 非 Windows 无 csbi 概念——第二期按终端协议补
#endif
}

std::string CLFPlatform::readClipboard() {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return "";
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); return ""; }
    wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(h));
    if (!wstr) { CloseClipboard(); return ""; }
    const int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    std::string raw(len > 0 ? len - 1 : 0, '\0');
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &raw[0], len, nullptr, nullptr);
    GlobalUnlock(h);
    CloseClipboard();
    return CLFEncoding::sanitizeUtf8(raw);  // 防止截断 UTF-8 导致半字光标
#else
    return "";   // [未验证] 非 Windows 空实现（第二期补——xclip/wl-paste 等）
#endif
}

bool CLFPlatform::writeClipboard(const std::string& text) {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wlen <= 0) { CloseClipboard(); return false; }
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
    if (!hMem) { CloseClipboard(); return false; }
    wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(hMem));
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wstr, wlen);
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    return true;
#else
    (void)text;
    return false;   // [未验证] 非 Windows 空实现（第二期补）
#endif
}

std::string CLFPlatform::makeTempFilePath(const std::string& prefix) {
    // 绝对路径 + 前缀 + 随机后缀（不创建文件——是否创建由调用方决定）
    std::string dir;
#ifdef _WIN32
    wchar_t tmpBuf[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tmpBuf) > 0) {
        dir = fs::path(tmpBuf).u8string();
    }
#else
    // [未验证] 非 Windows 分支：读 TMPDIR（E 类修复——原 /tmp 硬编码）
    if (const char* tmp = std::getenv("TMPDIR")) {
        dir = tmp;
    } else {
        dir = "/tmp";
    }
#endif
    if (dir.empty()) return "";
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '\\';

    // 唯一后缀：高精度时钟 + 原子计数器。static 只允许平凡零初始化对象
    // （项目教训：文件级/函数局部非平凡静态对象在 boost::ut 静态期与
    // magic static 全局锁上两次踩雷——atomic 零初始化安全）
    static std::atomic<unsigned long long> counter{0};
    const auto now = std::chrono::high_resolution_clock::now()
                       .time_since_epoch().count();
    return dir + prefix + "_"
         + std::to_string(now) + "_" + std::to_string(counter.fetch_add(1));
}

} // namespace CLF::CLFCore
