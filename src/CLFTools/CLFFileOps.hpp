// CLFFileOps.hpp — 文件操作工具
// 提供 readFile / writeFile / listDirectory 功能

#pragma once

#include <string>

namespace CLF::CLFTools {

struct CLFFileResult {
    bool        m_success = false;
    std::string m_content;
    std::string m_error;
};

// 读取文件内容
CLFFileResult readFile(const std::string& path);

// 写入文件（覆盖模式）
CLFFileResult writeFile(const std::string& path, const std::string& content);

// 列出目录内容
CLFFileResult listDirectory(const std::string& path);

} // namespace CLF::CLFTools
