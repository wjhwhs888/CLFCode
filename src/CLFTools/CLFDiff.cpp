// CLFDiff.cpp — 行级 LCS diff 计算实现

#include "CLFTools/CLFDiff.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

namespace CLF::CLFTools {

// ============================================================================
// 换行符标准化
// ============================================================================

std::string normalizeLineEndings(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            result += '\n';
            ++i;  // skip \n
        } else {
            result += text[i];
        }
    }
    return result;
}

// ============================================================================
// 辅助：按行切分（保留行内容，不含换行符）
// ============================================================================

namespace {

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    current.reserve(256);
    for (char ch : text) {
        if (ch == '\n') {
            lines.push_back(std::move(current));
            current.clear();
            current.reserve(256);
        } else {
            current += ch;
        }
    }
    // 最后一行（即使没有 \n 结尾）
    if (!current.empty() || !text.empty() && text.back() == '\n') {
        lines.push_back(std::move(current));
    }
    return lines;
}

} // anonymous namespace

// ============================================================================
// LCS diff 主函数
// ============================================================================

std::vector<CLFDiffLine> computeDiff(const std::string& oldText,
                                     const std::string& newText,
                                     CLFDiffStats& stats,
                                     int contextLines,
                                     bool normalizeEOL) {
    stats = {};
    std::vector<CLFDiffLine> empty;

    // 超限检查：字节数
    if (oldText.size() > 500 * 1024 || newText.size() > 500 * 1024) {
        stats.truncated = true;
        stats.truncReason = "file too large (>500KB), diff skipped";
        return empty;
    }

    // 深拷贝 + 换行符标准化（仅用于 diff 比较，不修改原始数据）
    std::string oldForDiff = normalizeEOL ? normalizeLineEndings(oldText) : oldText;
    std::string newForDiff = normalizeEOL ? normalizeLineEndings(newText) : newText;

    // 切分为行
    std::vector<std::string> oldLines = splitLines(oldForDiff);
    std::vector<std::string> newLines = splitLines(newForDiff);

    int oldSize = static_cast<int>(oldLines.size());
    int newSize = static_cast<int>(newLines.size());

    // 超限检查：行数
    if (oldSize > 3000 || newSize > 3000) {
        stats.truncated = true;
        stats.truncReason = "file too large (>" + std::to_string(3000) + " lines), diff skipped";
        return empty;
    }

    // 空文件比较
    if (oldSize == 0 && newSize == 0) {
        return empty;
    }

    // ================================================================
    // LCS 动态规划：dir[i][j] 记录方向，长度存于一维滚动数组
    // dir: 'm'=match(对角线), 'u'=up(删除), 'l'=left(插入)
    // ================================================================
    std::vector<std::vector<char>> dir(oldSize + 1,
                                        std::vector<char>(newSize + 1, '\0'));
    // dpPrev / dpCurr 只存长度值（用于推导方向）
    std::vector<int> dpPrev(newSize + 1, 0);
    std::vector<int> dpCurr(newSize + 1, 0);

    for (int i = 1; i <= oldSize; ++i) {
        dpCurr[0] = 0;
        for (int j = 1; j <= newSize; ++j) {
            if (oldLines[i - 1] == newLines[j - 1]) {
                dpCurr[j] = dpPrev[j - 1] + 1;
                dir[i][j] = 'm';  // match
            } else if (dpPrev[j] >= dpCurr[j - 1]) {
                dpCurr[j] = dpPrev[j];
                dir[i][j] = 'u';  // up (old line removed)
            } else {
                dpCurr[j] = dpCurr[j - 1];
                dir[i][j] = 'l';  // left (new line added)
            }
        }
        std::swap(dpPrev, dpCurr);
    }

    // ================================================================
    // 回溯：从 (oldSize, newSize) 回到 (0, 0)
    // 生成原始的 diff 序列（不含上下文）
    // ================================================================
    struct RawDiff {
        CLFDiffOp op;
        int oldIdx = -1;  // 0-based
        int newIdx = -1;
    };
    std::vector<RawDiff> raw;
    {
        int i = oldSize, j = newSize;
        while (i > 0 || j > 0) {
            if (i > 0 && j > 0 && dir[i][j] == 'm') {
                raw.push_back({CLFDiffOp::Keep, i - 1, j - 1});
                --i; --j;
            } else if (i > 0 && (j == 0 || dir[i][j] == 'u')) {
                raw.push_back({CLFDiffOp::Remove, i - 1, -1});
                --i;
            } else {
                raw.push_back({CLFDiffOp::Add, -1, j - 1});
                --j;
            }
        }
    }
    std::reverse(raw.begin(), raw.end());

    // ================================================================
    // 上下文窗口：仅保留变更行 ± contextLines
    // ================================================================
    int totalRaw = static_cast<int>(raw.size());
    std::vector<bool> keepFlag(totalRaw, false);

    for (int k = 0; k < totalRaw; ++k) {
        if (raw[k].op != CLFDiffOp::Keep) {
            // 标记变更行及其上下文窗口
            int lo = std::max(0, k - contextLines);
            int hi = std::min(totalRaw - 1, k + contextLines);
            for (int x = lo; x <= hi; ++x) {
                keepFlag[x] = true;
            }
        }
    }

    // ================================================================
    // 组装输出 + hunk 统计
    // ================================================================
    std::vector<CLFDiffLine> result;
    result.reserve(totalRaw);

    int oldLineNo = 1;
    int newLineNo = 1;
    bool inHunk = false;
    int renderedLines = 0;
    const int maxRenderedLines = 200;
    int skippedHunks = 0;
    int skippedLines = 0;
    bool lastWasKeep = false;

    for (int k = 0; k < totalRaw; ++k) {
        if (!keepFlag[k]) {
            // 跳过非上下文行（增加折叠标记）
            if (inHunk && !lastWasKeep) {
                lastWasKeep = true;  // 标记折叠开始
            }
            // 更新行号
            if (raw[k].op == CLFDiffOp::Keep) {
                ++oldLineNo; ++newLineNo;
            } else if (raw[k].op == CLFDiffOp::Remove) {
                ++oldLineNo;
            } else {
                ++newLineNo;
            }
            continue;
        }

        if (lastWasKeep && inHunk && k > 0 && keepFlag[k]) {
            // 从跳过区域回到显示区域：插入折叠标记
            CLFDiffLine fold;
            fold.op = CLFDiffOp::Keep;
            fold.text = "...";
            fold.oldLineNo = 0;
            fold.newLineNo = 0;
            result.push_back(fold);
            ++skippedHunks;
            renderedLines++;
        }
        lastWasKeep = false;

        CLFDiffLine line;
        line.op = raw[k].op;
        line.oldLineNo = (raw[k].op != CLFDiffOp::Add) ? oldLineNo : 0;
        line.newLineNo = (raw[k].op != CLFDiffOp::Remove) ? newLineNo : 0;
        line.text = (raw[k].op == CLFDiffOp::Remove)
                        ? oldLines[raw[k].oldIdx]
                        : newLines[raw[k].newIdx];

        result.push_back(line);
        inHunk = true;
        ++renderedLines;

        if (raw[k].op == CLFDiffOp::Keep) {
            ++oldLineNo; ++newLineNo;
        } else if (raw[k].op == CLFDiffOp::Remove) {
            ++oldLineNo;
            ++stats.removed;
        } else if (raw[k].op == CLFDiffOp::Add) {
            ++newLineNo;
            ++stats.added;
        }

        // 渲染行数超限 → 截断
        if (renderedLines >= maxRenderedLines && k + 1 < totalRaw) {
            // 计算剩余行数
            for (int r = k + 1; r < totalRaw; ++r) {
                if (keepFlag[r]) ++skippedLines;
            }
            CLFDiffLine trunc;
            trunc.op = CLFDiffOp::Keep;
            trunc.text = "... (" + std::to_string(skippedLines) + " lines omitted)";
            trunc.oldLineNo = 0;
            trunc.newLineNo = 0;
            result.push_back(trunc);
            stats.truncated = true;
            stats.truncReason = "rendered lines exceeded " + std::to_string(maxRenderedLines);
            break;
        }
    }

    // hunk 计数（连续的变更区域）
    int consecutiveChanges = 0;
    for (const auto& l : result) {
        if (l.op == CLFDiffOp::Add || l.op == CLFDiffOp::Remove) {
            ++consecutiveChanges;
        } else if (consecutiveChanges > 0) {
            ++stats.hunks;
            consecutiveChanges = 0;
        }
    }
    if (consecutiveChanges > 0) ++stats.hunks;
    stats.hunks += skippedHunks;

    return result;
}

} // namespace CLF::CLFTools
