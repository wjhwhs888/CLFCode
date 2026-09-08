// CLFSelectionModel.cpp — 选区状态机实现

#include "CLFUI/CLFSelectionModel.hpp"
#include "CLFTypes/CLFTextUtil.hpp"   // A2：宽度工具实现收敛至此

#include <algorithm>

namespace CLF::CLFUI {
using CLF::CLFCore::CLFTextUtil;   // A2

// ============================================================================
// 显示宽度工具（A2：实现收敛至 CLFTextUtil，本类静态方法保持 API 转发——
// 调用方零改动；与 Terminal cjkWidth 的两套等价实现已合并）
// ============================================================================

int CLFSelectionModel::charWidth(unsigned char c) {
    return CLFTextUtil::charWidth(c);
}

int CLFSelectionModel::displayWidth(const std::string& s) {
    return CLFTextUtil::displayWidth(s);
}

std::string CLFSelectionModel::substrByWidth(const std::string& s, int maxW) {
    return CLFTextUtil::substrByWidth(s, maxW);
}

size_t CLFSelectionModel::colToByte(const std::string& s, int col) {
    if (col <= 0) return 0;
    int w = 0;
    size_t i = 0;
    while (i < s.size()) {
        int cw = charWidth(static_cast<unsigned char>(s[i]));
        if (cw == 0) { ++i; continue; }
        if (w + cw > col) break;   // 宽字符跨列 → 落在字符起始
        w += cw;
        if (cw == 2) { ++i; while (i < s.size()
            && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i; }
        else { ++i; }
    }
    return i;
}

size_t CLFSelectionModel::colToByteEnd(const std::string& s, int col) {
    if (col < 0) return 0;
    int w = 0;
    size_t i = 0;
    while (i < s.size()) {
        int cw = charWidth(static_cast<unsigned char>(s[i]));
        if (cw == 0) { ++i; continue; }
        if (col < w + cw) {
            // 鼠标落在本字符格内 → 含入该字符
            if (cw == 2) { ++i; while (i < s.size()
                && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i; }
            else { ++i; }
            return i;
        }
        w += cw;
        if (cw == 2) { ++i; while (i < s.size()
            && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i; }
        else { ++i; }
    }
    return s.size();
}

// ============================================================================
// 选区状态
// ============================================================================

void CLFSelectionModel::startAt(int row, int byteStart, int byteEnd) {
    m_active = true;
    m_anchorRow = row;  m_anchorByteS = byteStart;  m_anchorByteE = byteEnd;
    m_cursorRow = row;  m_cursorByteS = byteStart;  m_cursorByteE = byteEnd;
}

void CLFSelectionModel::extendTo(int row, int byteStart, int byteEnd) {
    if (!m_active) { startAt(row, byteStart, byteEnd); return; }
    m_cursorRow = row;
    m_cursorByteS = byteStart;
    m_cursorByteE = byteEnd;
}

void CLFSelectionModel::clear() {
    m_active = false;
    m_anchorRow = -1; m_anchorByteS = 0; m_anchorByteE = 0;
    m_cursorRow = -1; m_cursorByteS = 0; m_cursorByteE = 0;
}

bool CLFSelectionModel::empty() const {
    // v3.1：单击判定用起始偏移（同字符格内 S 相同即视为未拖动——
    // 同 S 必同 E：bEnd 对同字符格内的任意列返回同一含入偏移）
    return !m_active
        || (m_anchorRow == m_cursorRow && m_anchorByteS == m_cursorByteS);
}

CLFSelectionModel::Range CLFSelectionModel::range() const {
    if (!m_active) return {};
    // v3.1 双值归一化：起点行取"字符起始"（含落点字符），终点行取"含入结束"
    // （含落点字符）——自上而下/自下而上两方向均不丢端部字符
    if (m_anchorRow < m_cursorRow
        || (m_anchorRow == m_cursorRow && m_anchorByteS <= m_cursorByteS))
        return {m_anchorRow, m_anchorByteS, m_cursorRow, m_cursorByteE};
    return {m_cursorRow, m_cursorByteS, m_anchorRow, m_anchorByteE};
}

std::optional<std::pair<size_t, size_t>>
CLFSelectionModel::rowSelection(int globalRow, size_t rowTextLen) const {
    if (!m_active) return std::nullopt;
    Range r = range();
    if (globalRow < r.fromRow || globalRow > r.toRow) return std::nullopt;
    size_t a = 0, b = rowTextLen;
    if (globalRow == r.fromRow) a = static_cast<size_t>(std::max(0, r.fromByte));
    if (globalRow == r.toRow)   b = std::min(rowTextLen,
                                             static_cast<size_t>(std::max(0, r.toByte)));
    if (a >= b) return std::nullopt;
    return std::make_pair(a, b);
}

// ============================================================================
// 提取
// ============================================================================

std::string CLFSelectionModel::extract(const Range& r,
                                       const std::vector<RowInfo>& rowMap,
                                       const std::vector<std::string>& rowTexts) {
    if (r.fromRow < 0 || r.toRow < 0 || rowMap.size() != rowTexts.size())
        return {};
    std::string out;
    bool havePrev = false;
    RowKind prevKind = RowKind::ScrollHint;
    size_t prevIdx = 0;

    for (int row = r.fromRow; row <= r.toRow; ++row) {
        if (row >= static_cast<int>(rowMap.size())) break;
        const RowInfo& info = rowMap[row];
        if (info.kind == RowKind::ScrollHint) continue;
        const std::string& text = rowTexts[row];

        size_t a = (row == r.fromRow) ? static_cast<size_t>(std::max(0, r.fromByte)) : 0;
        size_t b = (row == r.toRow) ? static_cast<size_t>(std::max(0, r.toByte))
                                    : text.size();
        a = std::min(a, text.size());
        b = std::min(b, text.size());
        if (a >= b) continue;  // 该行空选区

        bool sameLine = havePrev && prevKind == info.kind && prevIdx == info.lineIdx;
        if (havePrev && !sameLine) out += '\n';
        out += text.substr(a, b - a);
        prevKind = info.kind;
        prevIdx = info.lineIdx;
        havePrev = true;
    }
    return out;
}

} // namespace CLF::CLFUI
