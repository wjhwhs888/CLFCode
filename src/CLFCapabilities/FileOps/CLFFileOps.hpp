// CLFFileOps.hpp — 文件操作工具
// 提供 readFile / writeFile / listDirectory / editFile 功能
// writeFile 和 editFile 统一走原子写入路径

#pragma once

#include <cstdint>
#include <string>

namespace CLF::CLFTools {

struct CLFFileResult {
    bool        m_success = false;
    std::string m_content;
    std::string m_error;
};

// 文件快照（用于 TOCTOU 校验）
struct CLFFileSnapshot {
    std::string content;
    uint64_t mtime = 0;
    uint64_t size  = 0;
};

// 读取文件内容
CLFFileResult readFile(const std::string& path);

// 读取文件 + 返回快照（content + mtime + size）
CLFFileResult readFileWithSnapshot(const std::string& path,
                                   CLFFileSnapshot& snapshot);

// 写入文件（原子写入：临时文件 + flush + MoveFileEx/rename）
CLFFileResult writeFile(const std::string& path, const std::string& content);

// 精确字符串替换（原子写入，oldStr 必须唯一匹配）
// 0 次匹配 → m_error = "old_string not found in file..."
// >1 次匹配 → m_error = "old_string matches N times..."
CLFFileResult editFile(const std::string& path,
                       const std::string& oldStr,
                       const std::string& newStr);

// 在内存中模拟 oldStr→newStr 替换（不写盘，用于预览 diff）
// 返回替换后的新内容；匹配失败时 m_success=false
CLFFileResult previewEdit(const std::string& content,
                          const std::string& oldStr,
                          const std::string& newStr);

// 列出目录内容
CLFFileResult listDirectory(const std::string& path);

// 获取文件 mtime（失败返回 0）
uint64_t getFileMtime(const std::string& path);

// 获取文件 size（失败返回 0）
uint64_t getFileSize(const std::string& path);

} // namespace CLF::CLFTools
