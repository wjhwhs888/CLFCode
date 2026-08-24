// CLFFileOps.cpp — 文件操作工具实现
// 原子写入：临时文件 + flush + MoveFileEx/rename
// 编码转换 → 委托 CLFEncoding

#include "CLFTools/CLFFileOps.hpp"
#include "CLFTypes/CLFEncoding.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace CLF::CLFTools {

namespace {

// 判定字节序列是否为合法 UTF-8（文件内容编码探测用）
bool isValidUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { ++i; continue; }
        size_t extra;
        if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return false;
        if (i + extra >= s.size()) return false;
        for (size_t j = 1; j <= extra; ++j)
            if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}

// UTF-8 路径 → 系统原生路径（Windows：宽字符；Linux：原样）
fs::path toNativePath(const std::string& utf8Path) {
#ifdef _WIN32
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, nullptr, 0);
    if (wideLen <= 1) return fs::path(utf8Path);
    std::wstring wide(wideLen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, wide.data(), wideLen - 1);
    return fs::path(wide);
#else
    return fs::path(utf8Path);
#endif
}

// 获取文件 mtime（平台适配）
uint64_t getMtime(const fs::path& nativePath) {
#ifdef _WIN32
    HANDLE h = CreateFileW(nativePath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FILETIME ftWrite;
    uint64_t result = 0;
    if (GetFileTime(h, nullptr, nullptr, &ftWrite)) {
        result = (static_cast<uint64_t>(ftWrite.dwHighDateTime) << 32)
               | ftWrite.dwLowDateTime;
    }
    CloseHandle(h);
    return result;
#else
    struct stat st;
    if (stat(nativePath.c_str(), &st) != 0) return 0;
    return static_cast<uint64_t>(st.st_mtime);
#endif
}

// 获取文件 size
uint64_t getSize(const fs::path& nativePath) {
    std::error_code ec;
    uint64_t sz = fs::file_size(nativePath, ec);
    return ec ? 0 : sz;
}

// 原子写入实现（临时文件 + flush + 原子替换）
bool atomicWriteFile(const fs::path& nativePath, const std::string& content,
                     std::string& outError) {
    fs::path tmpPath = nativePath;
    tmpPath += L".clf_tmp";

    // 写入临时文件
    {
#ifdef _WIN32
        std::ofstream file(tmpPath, std::ios::out | std::ios::trunc | std::ios::binary);
#else
        std::ofstream file(tmpPath.string(), std::ios::out | std::ios::trunc);
#endif
        if (!file.is_open()) {
            outError = "Cannot create temp file";
            return false;
        }
        file << content;
        file.flush();
        if (!file.good()) {
            file.close();
            fs::remove(tmpPath);
            outError = "Write to temp file failed";
            return false;
        }
        file.close();
    }

    // 原子替换
#ifdef _WIN32
    if (!MoveFileExW(tmpPath.c_str(), nativePath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD err = GetLastError();
        fs::remove(tmpPath);
        outError = "MoveFileEx failed: error " + std::to_string(err);
        return false;
    }
#else
    if (rename(tmpPath.c_str(), nativePath.c_str()) != 0) {
        if (errno == EXDEV) {
            // 跨设备：降级为拷贝 + 删除（非原子但能完成写入）
            std::error_code ec;
            fs::copy_file(tmpPath, nativePath,
                          fs::copy_options::overwrite_existing, ec);
            if (ec) {
                fs::remove(tmpPath);
                outError = "Cross-device copy failed: " + ec.message();
                return false;
            }
            fs::remove(tmpPath);
            // 内部警告日志
            fprintf(stderr, "[CLF] WARNING: atomic rename EXDEV, fallback to copy+delete for %s\n",
                    nativePath.c_str());
            return true;
        }
        fs::remove(tmpPath);
        outError = "rename failed: " + std::string(strerror(errno));
        return false;
    }
#endif
    return true;
}

} // anonymous namespace

// ============================================================================
// 公开 API
// ============================================================================

CLFFileResult readFile(const std::string& path) {
    CLFFileResult result;
    std::ifstream file(toNativePath(path), std::ios::in);  // path(宽) 构造：MSVC 直接宽打开
    if (!file.is_open()) {
        result.m_error = "Cannot open file: " + path;
        return result;
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    std::string raw = oss.str();
    // 内容编码判定：合法 UTF-8 直接采用（项目文件多为 UTF-8——按 ACP 解释会
    // 产生乱码或抛 "No mapping" 转换异常）；否则按 ACP 转 UTF-8
    result.m_content = isValidUtf8(raw) ? raw : CLF::CLFCore::CLFEncoding::toUtf8(raw);
    result.m_success = true;
    return result;
}

CLFFileResult readFileWithSnapshot(const std::string& path,
                                   CLFFileSnapshot& snapshot) {
    auto nativePath = toNativePath(path);
    snapshot = {};
    auto result = readFile(path);
    if (result.m_success) {
        snapshot.content = result.m_content;
        snapshot.mtime   = getMtime(nativePath);
        snapshot.size    = getSize(nativePath);
    }
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

    if (!atomicWriteFile(nativePath, content, result.m_error)) {
        return result;
    }
    result.m_success = true;
    return result;
}

CLFFileResult editFile(const std::string& path,
                       const std::string& oldStr,
                       const std::string& newStr) {
    CLFFileResult result;

    // 空 old_string 提前拒绝：find("") 恒命中位置 0 且每个位置都算匹配，
    // 唯一性检查会遍历全文后误报 "matches N times"（N = 文件长度+1）
    if (oldStr.empty()) {
        result.m_error = "old_string must not be empty. "
                         "Hint: provide the exact text to be replaced.";
        return result;
    }

    auto nativePath = toNativePath(path);

    // 读取原文件
    auto readResult = readFile(path);
    if (!readResult.m_success) {
        result.m_error = readResult.m_error;
        return result;
    }

    const std::string& content = readResult.m_content;

    // 查找 oldStr（精确匹配，唯一）
    size_t pos = content.find(oldStr);
    if (pos == std::string::npos) {
        result.m_error = "old_string not found in file. "
                         "Hint: use read_file to verify the exact content, "
                         "including whitespace and line endings.";
        return result;
    }

    // 检查是否唯一匹配
    size_t nextPos = content.find(oldStr, pos + 1);
    if (nextPos != std::string::npos) {
        // 统计总匹配次数
        int count = 1;
        while (nextPos != std::string::npos) {
            ++count;
            nextPos = content.find(oldStr, nextPos + 1);
        }
        result.m_error = "old_string matches " + std::to_string(count)
                       + " times in file. "
                         "Hint: include more surrounding context to make it unique.";
        return result;
    }

    // 替换
    std::string modified = content;
    modified.replace(pos, oldStr.size(), newStr);

    // 确保父目录存在
    std::error_code ec;
    if (nativePath.has_parent_path()) {
        fs::create_directories(nativePath.parent_path(), ec);
    }

    // 原子写入
    if (!atomicWriteFile(nativePath, modified, result.m_error)) {
        return result;
    }
    result.m_success = true;
    // 返回修改后的内容供 diff 使用
    result.m_content = std::move(modified);
    return result;
}

CLFFileResult previewEdit(const std::string& content,
                          const std::string& oldStr,
                          const std::string& newStr) {
    CLFFileResult result;

    size_t pos = content.find(oldStr);
    if (pos == std::string::npos) {
        result.m_error = "old_string not found in file. "
                         "Hint: use read_file to verify the exact content, "
                         "including whitespace and line endings.";
        return result;
    }

    size_t nextPos = content.find(oldStr, pos + 1);
    if (nextPos != std::string::npos) {
        int count = 1;
        while (nextPos != std::string::npos) {
            ++count;
            nextPos = content.find(oldStr, nextPos + 1);
        }
        result.m_error = "old_string matches " + std::to_string(count)
                       + " times in file. "
                         "Hint: include more surrounding context to make it unique.";
        return result;
    }

    std::string modified = content;
    modified.replace(pos, oldStr.size(), newStr);
    result.m_success = true;
    result.m_content = std::move(modified);
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
            << entry.path().filename().u8string()  // 直接 UTF-8（窄 string() 按 ACP 解释会乱码/抛异常）
            << '\n';
    }
    result.m_content = oss.str();
    result.m_success = true;
    return result;
}

uint64_t getFileMtime(const std::string& path) {
    return getMtime(toNativePath(path));
}

uint64_t getFileSize(const std::string& path) {
    return getSize(toNativePath(path));
}

} // namespace CLF::CLFTools
