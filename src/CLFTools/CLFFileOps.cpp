// CLFFileOps.cpp — 文件操作工具实现

#include "CLFTools/CLFFileOps.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace CLF::CLFTools {

namespace {

// Windows 系统代码页 → UTF-8（用于命令输出/目录列表）
std::string toUtf8(const std::string& input) {
    if (input.empty()) return input;
#ifdef _WIN32
    int wideLen = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS,
                                       input.c_str(), static_cast<int>(input.size()),
                                       nullptr, 0);
    if (wideLen <= 0) return input;
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

// UTF-8 路径 → 系统原生路径（Windows：宽字符；Linux：原样）
fs::path toNativePath(const std::string& utf8Path) {
#ifdef _WIN32
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, nullptr, 0);
    if (wideLen <= 1) return fs::path(utf8Path); // 仅 \0
    // wideLen 含 \0，wstring 去掉它，否则 fs::path 内嵌 \0 导致无效路径
    std::wstring wide(wideLen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, wide.data(), wideLen - 1);
    return fs::path(wide);
#else
    return fs::path(utf8Path);
#endif
}

} // anonymous namespace

CLFFileResult readFile(const std::string& path) {
    CLFFileResult result;

    std::ifstream file(toNativePath(path), std::ios::in);
    if (!file.is_open()) {
        result.m_error = "Cannot open file: " + path;
        return result;
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    result.m_content = toUtf8(oss.str()); // GBK → UTF-8（已是 UTF-8 则原样）
    result.m_success = true;

    return result;
}

CLFFileResult writeFile(const std::string& path, const std::string& content) {
    CLFFileResult result;

    auto nativePath = toNativePath(path);
    // 确保父目录存在
    std::error_code ec;
    if (nativePath.has_parent_path()) {
        fs::create_directories(nativePath.parent_path(), ec);
    }

    std::ofstream file(nativePath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        result.m_error = "Cannot write file: " + path;
        return result;
    }

    file << content;
    file.close();
    result.m_success = true;

    return result;
}

CLFFileResult listDirectory(const std::string& path) {
    CLFFileResult result;
    std::error_code ec;

    auto nativePath = toNativePath(path);

    if (!fs::exists(nativePath, ec)) {
        result.m_error = "Directory not found: " + path;
        return result;
    }

    if (!fs::is_directory(nativePath, ec)) {
        result.m_error = "Not a directory: " + path;
        return result;
    }

    std::ostringstream oss;
    for (const auto& entry : fs::directory_iterator(nativePath, ec)) {
        oss << (entry.is_directory() ? "[DIR]  " : "[FILE] ")
            << toUtf8(entry.path().filename().string())
            << '\n';
    }

    result.m_content = oss.str();
    result.m_success = true;

    return result;
}

} // namespace CLF::CLFTools
