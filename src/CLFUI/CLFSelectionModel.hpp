// CLFSelectionModel.hpp — 选区状态机 + 行映射 + 文本提取（纯逻辑，可单测）
// 设计：`.claude/plans/设计/设计-复制粘贴功能修改.md` §三
// 实现定稿简化：选区坐标与提取均以"渲染行文本"（rowTexts，WYSIWYG 含前缀）
// 为源——提取直接截取渲染文本，逐行拼接；无需按 RowKind 解析源串。

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CLF::CLFUI {

// 渲染行来源类别（行映射表 RowMap 的每行记录）
enum class RowKind {
    Content,      // snapshot.lines[lineIdx] 的第 partIdx 个硬换行 part
    Pending,      // snapshot.pendingLine 按字节 wrap 的 part
    ThinkingFold, // 思考折叠摘要行
    ThinkingLine, // 思考展开行 [lineIdx]
    FoldSummary,  // 恢复回显折叠摘要行
    FoldLine,     // 恢复回显折叠展开行 [lineIdx]
    Progress,     // progressLines[lineIdx]（末行含动画帧）
    Status,       // 状态行
    ScrollHint,   // 滚动提示行（不可选；渲染器实际不产出，保留枚举完备性）
};

struct RowInfo {
    RowKind kind = RowKind::ScrollHint;
    size_t  lineIdx = 0;   // 源数组下标
    size_t  partIdx = 0;   // 硬换行 part 序号（0 起）
};

class CLFSelectionModel {
public:
    // ---- 显示宽度工具（渲染硬换行 / 列→字节换算 / 高亮拆分共用） ----
    static int  charWidth(unsigned char c);
    static int  displayWidth(const std::string& s);
    static std::string substrByWidth(const std::string& s, int maxW);
    static size_t colToByte(const std::string& s, int col);       // 显示列 → 字符起始字节偏移（不截断 UTF-8）
    static size_t colToByteEnd(const std::string& s, int col);    // 鼠标落在字符格内 → 该字符之后的偏移
                                                                  // （选区游标含入语义：拖到字符上即包含该字符）
    static size_t snapBack(const std::string& s, size_t byteOff); // 回退到 UTF-8 字符边界

    // ---- 选区状态 ----
    struct Range { int fromRow = -1, fromByte = 0, toRow = -1, toByte = 0; };

    void startAt(int row, int byteOff);        // 鼠标按下 / Ctrl+S 进入
    void extendTo(int row, int byteOff);       // 拖拽 Moved / 方向键 / PgUp/PgDn；未激活时等价 startAt
    enum class Dir { Up, Down, Left, Right, Home, End };
    void moveCursor(Dir d,
                    const std::vector<RowInfo>& rowMap,
                    const std::vector<std::string>& rowTexts);  // 键盘移动（跨行保留字节偏移并 clamp）
    void clear();
    bool active() const { return m_active; }
    bool empty() const;                       // anchor == cursor（单击无拖动判定）
    Range range() const;                      // 归一化区间（反向选区自动交换）

    // globalRow 行内的选区子区间（该行渲染文本的相对字节偏移）；无重叠返回 nullopt
    std::optional<std::pair<size_t, size_t>> rowSelection(int globalRow, size_t rowTextLen) const;

    // ---- 提取（纯函数） ----
    // 按行序遍历选区，同一逻辑行（kind+lineIdx 相同）的多个 part 直接拼接不加 '\n'，
    // 跨逻辑行加 '\n'；ScrollHint 行跳过；空/无效选区返回空串。
    static std::string extract(const Range& r,
                               const std::vector<RowInfo>& rowMap,
                               const std::vector<std::string>& rowTexts);

private:
    // 键盘移动后把游标字节偏移 clamp 到所在行尾（UTF-8 边界安全）
    void clampCursorToRow(const std::vector<std::string>& rowTexts);

    int  m_anchorRow = -1, m_anchorByte = 0;
    int  m_cursorRow = -1, m_cursorByte = 0;
    bool m_active = false;
};

} // namespace CLF::CLFUI
