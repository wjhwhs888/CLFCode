// CLFSearchContent.cpp — 文件内容搜索实现
// 递归遍历目录，逐行用 std::string::find 匹配
// 跳过忽略目录、超大文件，限制结果行数

#include "CLFTools/CLFSearchContent.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace CLF::CLFTools {

namespace {

constexpr size_t kMaxFileSize   = 1 * 1024 * 1024;  // 1MB
constexpr int    kMaxResults    = 500;
constexpr int    kMaxDepth      = 20;
// P0-2: head/tail 截断（dsh 模式）——head 固定 + tail 环形缓冲，总预算不变
constexpr int    kHeadLines     = 240;
constexpr int    kTailLines     = 240;

const std::set<std::string> kIgnoreDirs = {
    ".git", "node_modules", "__pycache__", "build", "dist",
    ".cache", "vendor", ".svn", ".hg"
};

// 按 , 分割并 trim 空格，统一转小写，过滤不带点的无效项
std::vector<std::string> parseFileTypes(const std::string& fileTypes) {
    std::vector<std::string> result;
    if (fileTypes.empty()) return result;
    std::istringstream iss(fileTypes);
    std::string ext;
    while (std::getline(iss, ext, ',')) {
        // trim
        size_t s = ext.find_first_not_of(" \t");
        size_t e = ext.find_last_not_of(" \t");
        if (s == std::string::npos) continue;
        ext = ext.substr(s, e - s + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext.empty() || ext.front() != '.') continue; // 无效，跳过
        result.push_back(ext);
    }
    return result;
}

bool isIgnoredDir(const fs::path& p) {
    std::string name = p.filename().string();
    return kIgnoreDirs.count(name) > 0;
}

bool isFileTooLarge(const fs::path& p) {
    std::error_code ec;
    return fs::file_size(p, ec) > kMaxFileSize || ec;
}

bool matchesExtension(const fs::path& p, const std::vector<std::string>& exts) {
    if (exts.empty()) return true;
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

void searchDir(const fs::path& dir, const std::string& pattern,
               const std::vector<std::string>& fileTypes,
               int depth, int& resultCount,
               std::vector<std::string>& headLines,
               std::vector<std::string>& tailRing,
               std::vector<std::pair<std::string, std::string>>& skippedLarge) {
    if (depth > kMaxDepth || resultCount >= kMaxResults) return;

    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); it != fs::directory_iterator(); ++it) {
        if (resultCount >= kMaxResults) return;

        const auto& entry = *it;
        if (entry.is_directory(ec)) {
            if (!isIgnoredDir(entry.path())) {
                searchDir(entry.path(), pattern, fileTypes,
                          depth + 1, resultCount, headLines, tailRing, skippedLarge);
            }
        } else if (entry.is_regular_file(ec)) {
            if (!matchesExtension(entry.path(), fileTypes)) continue;
            if (isFileTooLarge(entry.path())) {
                skippedLarge.emplace_back(entry.path().string(),
                    std::to_string(fs::file_size(entry.path(), ec)) + " bytes");
                continue;
            }
            // 读取文件并逐行匹配
            std::ifstream file(entry.path());
            if (!file.is_open()) continue;
            std::string relative = fs::relative(entry.path(), dir).string();
            std::string line;
            int lineNum = 0;
            while (std::getline(file, line) && resultCount < kMaxResults) {
                ++lineNum;
                if (line.find(pattern) != std::string::npos) {
                    // P0-2: 前 kHeadLines 条进 head；其余进 tail 环形（容量 kTailLines，挤掉最旧）
                    std::string entry = relative + ":" + std::to_string(lineNum) + ": " + line;
                    if (resultCount < kHeadLines) {
                        headLines.push_back(std::move(entry));
                    } else {
                        tailRing.push_back(std::move(entry));
                        if (static_cast<int>(tailRing.size()) > kTailLines)
                            tailRing.erase(tailRing.begin());
                    }
                    ++resultCount;
                }
            }
        }
    }
}

} // anonymous namespace

std::string searchContent(const std::string& pattern,
                          const std::string& directory,
                          const std::string& fileTypes) {
    if (pattern.empty()) return "[Error] pattern is required";

    fs::path dir(directory);
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        return "[Error] directory not found: " + directory;
    }

    auto exts = parseFileTypes(fileTypes);
    int resultCount = 0;
    std::ostringstream output;
    std::vector<std::string> headLines;
    std::vector<std::string> tailRing;
    std::vector<std::pair<std::string, std::string>> skippedLarge;

    searchDir(dir, pattern, exts, 0, resultCount, headLines, tailRing, skippedLarge);

    if (resultCount == 0 && headLines.empty()) {
        output << "(no matches)";
    } else {
        for (const auto& h : headLines) output << h << "\n";
        int omitted = resultCount - static_cast<int>(headLines.size())
                    - static_cast<int>(tailRing.size());
        if (omitted > 0) {
            // 行尾已带换行，不加前导 \n（否则产生多余空行）
            output << "[中间省略 " << omitted << " 行]\n";
        }
        for (const auto& t : tailRing) output << t << "\n";
    }
    if (resultCount >= kMaxResults) {
        output << "[结果超过 " << kMaxResults << " 行，已截断]";
    }
    if (!skippedLarge.empty()) {
        output << "\n[跳过 " << skippedLarge.size() << " 个大文件:";
        for (size_t i = 0; i < skippedLarge.size() && i < 5; ++i) {
            output << "\n  " << skippedLarge[i].first
                   << " (" << skippedLarge[i].second << ")";
        }
        if (skippedLarge.size() > 5) {
            output << "\n  ... 共 " << skippedLarge.size() << " 个";
        }
        output << "]";
    }

    return output.str();
}

} // namespace CLF::CLFTools
