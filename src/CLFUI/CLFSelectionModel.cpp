// CLFSelectionModel.cpp — 选区状态机实现

#include "CLFUI/CLFSelectionModel.hpp"

#include <algorithm>

namespace CLF::CLFUI {

// ============================================================================
// 显示宽度工具
// ============================================================================

int CLFSelectionModel::charWidth(unsigned char c) {
    if (c < 0x80) return 1;   // ASCII
    if (c >= 0xC0) return 2;  // UTF-8 多字节首字节（CJK/全角计 2）
    return 0;                 // UTF-8 续字节
}

int CLFSelectionModel::displayWidth(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size(); ++i)
        w += charWidth(static_cast<unsigned char>(s[i]));
    return w;
}

std::string CLFSelectionModel::substrByWidth(const std::string& s, int maxW) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        int cw = charWidth(static_cast<unsigned char>(s[i]));
        if (cw == 0) { ++i; continue; }           // UTF-8 续字节，不单独算
        if (w + cw > maxW) return s.substr(0, i);
        w += cw;
        if (cw == 2) { ++i; while (i < s.size()
            && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i; }
        else { ++i; }
    }
    return s;
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

size_t CLFSelectionModel::snapBack(const std::string& s, size_t byteOff) {
    while (byteOff > 0 && byteOff <= s.size()
           && (static_cast<unsigned char>(s[byteOff]) & 0xC0) == 0x80)
        --byteOff;
    return byteOff;
}

// ============================================================================
// 选区状态
// ============================================================================

void CLFSelectionModel::startAt(int row, int byteOff) {
    m_active = true;
    m_anchorRow = row;  m_anchorByte = byteOff;
    m_cursorRow = row;  m_cursorByte = byteOff;
}

void CLFSelectionModel::extendTo(int row, int byteOff) {
    if (!m_active) { startAt(row, byteOff); return; }
    m_cursorRow = row;
    m_cursorByte = byteOff;
}

void CLFSelectionModel::clear() {
    m_active = false;
    m_anchorRow = -1; m_anchorByte = 0;
    m_cursorRow = -1; m_cursorByte = 0;
}

bool CLFSelectionModel::empty() const {
    return !m_active || (m_anchorRow == m_cursorRow && m_anchorByte == m_cursorByte);
}

CLFSelectionModel::Range CLFSelectionModel::range() const {
    if (!m_active) return {};
    if (m_anchorRow < m_cursorRow
        || (m_anchorRow == m_cursorRow && m_anchorByte <= m_cursorByte))
        return {m_anchorRow, m_anchorByte, m_cursorRow, m_cursorByte};
    return {m_cursorRow, m_cursorByte, m_anchorRow, m_anchorByte};
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

void CLFSelectionModel::clampCursorToRow(const std::vector<std::string>& rowTexts) {
    if (m_cursorRow < 0 || m_cursorRow >= static_cast<int>(rowTexts.size())) return;
    size_t len = rowTexts[m_cursorRow].size();
    if (static_cast<size_t>(m_cursorByte) > len) m_cursorByte = static_cast<int>(len);
    // 行尾或跨行落点可能位于 UTF-8 多字节字符中间 → 回退到字符边界
    m_cursorByte = static_cast<int>(snapBack(rowTexts[m_cursorRow], m_cursorByte));
}

void CLFSelectionModel::moveCursor(Dir d,
                                   const std::vector<RowInfo>& rowMap,
                                   const std::vector<std::string>& rowTexts) {
    if (!m_active || rowMap.empty() || rowMap.size() != rowTexts.size()) return;
    auto isCont = [](char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; };

    switch (d) {
    case Dir::Left:
        if (m_cursorByte > 0)
            m_cursorByte = static_cast<int>(snapBack(rowTexts[m_cursorRow], m_cursorByte - 1));
        break;
    case Dir::Right: {
        const auto& rowText = rowTexts[m_cursorRow];
        size_t pos = static_cast<size_t>(m_cursorByte) + 1;
        while (pos < rowText.size() && isCont(rowText[pos])) ++pos;  // 越过续字节
        m_cursorByte = static_cast<int>(std::min(pos, rowText.size()));
        break;
    }
    case Dir::Up:
        if (m_cursorRow > 0) { --m_cursorRow; clampCursorToRow(rowTexts); }
        break;
    case Dir::Down:
        if (m_cursorRow + 1 < static_cast<int>(rowMap.size())) {
            ++m_cursorRow;
            clampCursorToRow(rowTexts);
        }
        break;
    case Dir::Home:
        m_cursorByte = 0;
        break;
    case Dir::End:
        m_cursorByte = static_cast<int>(rowTexts[m_cursorRow].size());
        clampCursorToRow(rowTexts);
        break;
    }
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
