// qa_CLFSelectionModel.cpp — 选区状态机单元测试（S1-S7）
// 设计：`.claude/plans/设计/归档/归档-复制粘贴功能修改.md` §3.6
// （S8 键盘移动用例随 Ctrl+S 键盘选区一并移除——验收收敛为纯鼠标拖选）

#include <boost/ut.hpp>

#include <string>
#include <vector>

#include "CLFUI/CLFScrollView.hpp"
#include "CLFUI/CLFSelectionModel.hpp"

using namespace boost::ut;
using CLF::CLFUI::CLFSelectionModel;
using CLF::CLFUI::RowInfo;
using CLF::CLFUI::RowKind;

namespace {

// 手工构造 rowMap/rowTexts 的辅助：单行添加
void addRow(std::vector<RowInfo>& map, std::vector<std::string>& texts,
            RowKind kind, size_t lineIdx, size_t partIdx, std::string text) {
    map.push_back(RowInfo{kind, lineIdx, partIdx});
    texts.push_back(std::move(text));
}

} // anonymous namespace

const boost::ut::suite<"CLFSelectionModel"> tests = [] {

    // ========== S1: 坐标映射（visibleRange/colToByte/提示行偏移） ==========

    "S1a visibleRange 与 topHintCount 口径分开"_test = [] {
        CLF::CLFUI::CLFScrollView sv;
        sv.update(100, 30, 7);   // 100 行，视口 23 行 → [77, 100)
        auto [s, e] = sv.visibleRange();
        expect(s == 77 && e == 100);
        expect(sv.topHintCount() == 0);  // 底部无滚动
        sv.handleEvent(ftxui::Event::PageUp);  // 上滚 15 → offset=15
        sv.update(100, 30, 7);
        auto [s2, e2] = sv.visibleRange();
        expect(s2 == 62 && e2 == 85);
        expect(sv.topHintCount() == 1);  // 有上方内容 → 顶部提示行
    };

    "S1b 鼠标坐标换算公式（0 基 + 提示行偏移 + clamp）"_test = [] {
        CLF::CLFUI::CLFScrollView sv;
        sv.handleEvent(ftxui::Event::PageUp);
        sv.update(100, 30, 7);
        auto [vs, ve] = sv.visibleRange();
        int topHints = sv.topHintCount();
        // 验收实测修正：本环境投递 0 基坐标（不做 -1）
        auto gRow = [&](int y) {
            int r = vs + y - topHints;
            return std::max(vs, std::min(r, ve - 1));
        };
        expect(vs == 62 && ve == 85 && topHints == 1);
        expect(gRow(0)  == 62);  // 提示行 → clamp 到首个内容行
        expect(gRow(1)  == 62);  // 首个内容行
        expect(gRow(23) == 84);  // 末个内容行
        expect(gRow(30) == 84);  // 超出 → clamp 到末行
    };

    "S1c colToByte：CJK 双宽与硬换行 part 边界"_test = [] {
        using Sel = CLFSelectionModel;
        std::string s = "a你好b";           // 宽 1+2+2+1=6
        expect(Sel::colToByte(s, 0) == 0);
        expect(Sel::colToByte(s, 1) == 1);  // 'a' 后
        expect(Sel::colToByte(s, 2) == 1);  // "你" 跨列 → 落在字符起始
        expect(Sel::colToByte(s, 3) == 4);  // col 3 是"你"的末列 → 落在"你"之后
        expect(Sel::colToByte(s, 4) == 4);  // "你"(1-3) 之后 → 字节 4（'好' 起始）
        expect(Sel::colToByte(s, 6) == 8);  // 总宽 6 → 行尾（'b' 之后）
        expect(Sel::colToByte(s, 99) == 8); // 超列 clamp 行尾
        // part 边界：substrByWidth 不劈半多字节
        expect(Sel::substrByWidth("你你你", 4) == "你你");
        // colToByteEnd：鼠标落在字符格内 → 含入该字符（拖选末字符不丢失，验收实证）
        expect(Sel::colToByteEnd("abc", 0) == 1);    // 落在 'a' 上 → 含入
        expect(Sel::colToByteEnd("abc", 2) == 3);    // 落在 'c' 上 → 含入
        expect(Sel::colToByteEnd("abc", 99) == 3);   // 超出行尾 → 行尾
        expect(Sel::colToByteEnd("a你好b", 2) == 4); // col 2 是 '你' 末列 → 含入 '你'
        expect(Sel::colToByteEnd("a你好b", 3) == 7); // col 3 是 '好' 首列 → 含入 '好'
        expect(Sel::colToByteEnd("a你好b", 6) == 8); // 总宽 6 → 行尾
    };

    // ========== S2: 反向选区归一化 ==========

    "S2 反向选区（右下→左上）自动交换"_test = [] {
        CLFSelectionModel m;
        m.startAt(10, 5);
        m.extendTo(2, 3);
        auto r = m.range();
        expect(r.fromRow == 2 && r.fromByte == 3);
        expect(r.toRow == 10 && r.toByte == 5);
        // 同行反向
        CLFSelectionModel m2;
        m2.startAt(4, 9);
        m2.extendTo(4, 2);
        auto r2 = m2.range();
        expect(r2.fromRow == 4 && r2.fromByte == 2);
        expect(r2.toRow == 4 && r2.toByte == 9);
    };

    // ========== S3: 提取换行规则（核心） ==========

    "S3a 同逻辑行多个 part 拼接无 \\n，跨逻辑行有 \\n"_test = [] {
        std::vector<RowInfo> map;
        std::vector<std::string> texts;
        // 逻辑行 0 拆 2 part："abcdef" → "abc" + "def"
        addRow(map, texts, RowKind::Content, 0, 0, "abc");
        addRow(map, texts, RowKind::Content, 0, 1, "def");
        // 逻辑行 1 单 part
        addRow(map, texts, RowKind::Content, 1, 0, "123");
        CLFSelectionModel m;
        m.startAt(0, 1);
        m.extendTo(2, 2);
        auto r = m.range();
        expect(CLFSelectionModel::extract(r, map, texts) == "bcdef\n12");
    };

    "S3b 反向选区提取与全行选区"_test = [] {
        std::vector<RowInfo> map;
        std::vector<std::string> texts;
        addRow(map, texts, RowKind::Content, 0, 0, "line0");
        addRow(map, texts, RowKind::Content, 1, 0, "line1");
        CLFSelectionModel m;
        m.startAt(1, 5);
        m.extendTo(0, 0);
        expect(CLFSelectionModel::extract(m.range(), map, texts) == "line0\nline1");
    };

    // ========== S4: 混合 RowKind 提取 ==========

    "S4 混合 pending/思考展开/折叠展开行提取"_test = [] {
        std::vector<RowInfo> map;
        std::vector<std::string> texts;
        addRow(map, texts, RowKind::Content,      0, 0, "reply");
        addRow(map, texts, RowKind::Pending,      0, 0, "pending...");
        addRow(map, texts, RowKind::ThinkingFold, 0, 0, "  Thought for 3s · ... (ctrl+t 展开)");
        addRow(map, texts, RowKind::ThinkingLine, 0, 0, "  thought-A");
        addRow(map, texts, RowKind::ThinkingLine, 1, 0, "  thought-B");
        addRow(map, texts, RowKind::FoldSummary,  0, 0, "  ▸ 恢复回显摘要");
        addRow(map, texts, RowKind::FoldLine,     0, 0, "  fold-A");
        CLFSelectionModel m;
        m.startAt(0, 0);
        m.extendTo(6, 20);
        auto out = CLFSelectionModel::extract(m.range(), map, texts);
        expect(out == "reply\npending...\n  Thought for 3s · ... (ctrl+t 展开)\n"
                      "  thought-A\n  thought-B\n  ▸ 恢复回显摘要\n  fold-A");
    };

    // ========== S5: 空选区 / 单击 / ScrollHint ==========

    "S5a 空选区提取空串；单击（anchor==cursor）判定 empty"_test = [] {
        CLFSelectionModel m;
        m.startAt(3, 4);
        expect(m.empty());  // 单击即 anchor==cursor
        m.extendTo(3, 4);
        expect(m.empty());
        std::vector<RowInfo> map{{RowKind::Content, 0, 0}};
        std::vector<std::string> texts{"abc"};
        expect(CLFSelectionModel::extract(m.range(), map, texts) == "");
        expect(m.rowSelection(0, 3) == std::nullopt);
        m.clear();
        expect(!m.active());
        expect(CLFSelectionModel::extract(m.range(), map, texts) == "");
    };

    "S5b ScrollHint 行跳过"_test = [] {
        std::vector<RowInfo> map;
        std::vector<std::string> texts;
        addRow(map, texts, RowKind::Content,    0, 0, "A");
        addRow(map, texts, RowKind::ScrollHint, 0, 0, "  ↑ 10 lines above");
        addRow(map, texts, RowKind::Content,    1, 0, "B");
        CLFSelectionModel m;
        m.startAt(0, 0);
        m.extendTo(2, 1);
        expect(CLFSelectionModel::extract(m.range(), map, texts) == "A\nB");
        // rowSelection 是纯区间计算（中间行=整行）；ScrollHint 跳过由 extract 的 kind 判断承担
        auto mid = m.rowSelection(1, 10);
        expect(mid && mid->first == 0 && mid->second == 10);
    };

    // ========== S6: 状态机行为 ==========

    "S6 active/clear/未激活 rowSelection 为空"_test = [] {
        CLFSelectionModel m;
        expect(!m.active());
        expect(m.rowSelection(0, 5) == std::nullopt);
        m.startAt(0, 1);
        m.extendTo(0, 4);
        expect(m.active());
        auto sel = m.rowSelection(0, 10);
        expect(sel && sel->first == 1 && sel->second == 4);
        m.clear();
        expect(!m.active());
        expect(m.range().fromRow == -1);
    };

    // ========== S7: 鼠标拖选 UTF-8 边界安全（colToByteEnd 含入不劈半） ==========

    "S7 拖选含入语义不劈半多字节（S1c 已覆盖列→字节换算）"_test = [] {
        using Sel = CLFSelectionModel;
        std::string s = "a你b";   // 字节: a(0) 你(1-3) b(4)
        expect(Sel::colToByteEnd(s, 0) == 1);  // 落在 'a' 上 → 含入
        expect(Sel::colToByteEnd(s, 1) == 4);  // 落在 '你' 首列 → 含入整字
        expect(Sel::colToByteEnd(s, 4) == 5);  // 落在 'b' 上 → 含入
        expect(Sel::colToByteEnd(s, 99) == 5); // 超列 → 行尾
    };
};

int main() {}
