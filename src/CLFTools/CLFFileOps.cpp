// CLFFileOps.cpp — 文件操作工具实现

#include "CLFTools/CLFFileOps.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace CLF::CLFTools {

CLFFileResult readFile(const std::string& path) {
    CLFFileResult result;

    std::ifstream file(path, std::ios::in);
    if (!file.is_open()) {
        result.m_error = "Cannot open file: " + path;
        return result;
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    result.m_content = oss.str();
    result.m_success = true;

    return result;
}

CLFFileResult writeFile(const std::string& path, const std::string& content) {
    CLFFileResult result;

    // 确保父目录存在
    fs::path filePath(path);
    std::error_code ec;
    if (filePath.has_parent_path()) {
        fs::create_directories(filePath.parent_path(), ec);
    }

    std::ofstream file(path, std::ios::out | std::ios::trunc);
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

    if (!fs::exists(path, ec)) {
        result.m_error = "Directory not found: " + path;
        return result;
    }

    if (!fs::is_directory(path, ec)) {
        result.m_error = "Not a directory: " + path;
        return result;
    }

    std::ostringstream oss;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        oss << (entry.is_directory() ? "[DIR]  " : "[FILE] ")
            << entry.path().filename().string()
            << '\n';
    }

    result.m_content = oss.str();
    result.m_success = true;

    return result;
}

} // namespace CLF::CLFTools
