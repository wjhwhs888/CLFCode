// CLFDiff.hpp — 行级 LCS diff 计算（纯算法，无文件 IO，无终端渲染）
// 用于 write_file / edit_file 工具的事前改动预览
//
// example:
//   CLFDiffStats stats;
//   auto diff = CLF::CLFTools::computeDiff(oldContent, newContent, stats);
//   std::string ansi = CLF::CLFTools::formatDiffAnsi(diff, stats, "foo.cpp");

#pragma once

#include <string>
#include <vector>

namespace CLF::CLFTools {

enum class CLFDiffOp { Keep, Add, Remove };

struct CLFDiffLine {
    CLFDiffOp op;
    int oldLineNo = 0;
    int newLineNo = 0;
    std::string text;
};

struct CLFDiffStats {
    int added   = 0;
    int removed = 0;
    int hunks   = 0;
    bool truncated = false;
    std::string truncReason;
};

// 行级 LCS diff（超限时返回空结果 + truncated=true）
// normalizeEOL=true 时在深拷贝上做 \r\n→\n，不修改传入的原始字符串
std::vector<CLFDiffLine> computeDiff(const std::string& oldText,
                                     const std::string& newText,
                                     CLFDiffStats& stats,
                                     int contextLines = 5,
                                     bool normalizeEOL = true);

// 换行符标准化：\r\n → \n（仅用于 diff 比较，不影响写入内容）
std::string normalizeLineEndings(const std::string& text);

} // namespace CLF::CLFTools
