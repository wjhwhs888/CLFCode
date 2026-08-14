// qa_CLFHeadTail.cpp — UI 信息展示借鉴 P0 单元测试
// T1:  headTailCapWithMarker 纯函数边界（设计 §4）
// T2'': renderDiff 截断形状（RenderEntry 形状模拟）
// T3:  emitError 首行化（P0-1）

#include <boost/ut.hpp>

#include <string>
#include <vector>

#include "CLFTypes/CLFTypes.hpp"
#include "CLFUI/CLFTerminal.hpp"

using namespace boost::ut;
using CLF::CLFCore::headTailCapWithMarker;

namespace {

// 模拟 renderDiff 的渲染条目形状（text + style）
struct RenderEntry {
    std::string text;
    int style = 0;
};

} // anonymous namespace

const boost::ut::suite<"CLFHeadTail"> tests = [] {
    // ========== T1: headTailCapWithMarker 纯函数 ==========

    "恰好 32 条（=2n）：原样返回，无标记"_test = [] {
        std::vector<std::string> items;
        for (int i = 0; i < 32; ++i) items.push_back("line-" + std::to_string(i));
        auto out = headTailCapWithMarker(items, std::string("MARK"));
        expect(out.size() == 32);
        expect(std::find(out.begin(), out.end(), "MARK") == out.end());
        expect(out.front() == "line-0" && out.back() == "line-31");
    };

    "33 条（2n+1）：前16 + 标记 + 后16"_test = [] {
        std::vector<std::string> items;
        for (int i = 0; i < 33; ++i) items.push_back("line-" + std::to_string(i));
        auto out = headTailCapWithMarker(items, std::string("MARK"));
        expect(out.size() == 33);
        expect(out[0] == "line-0");
        expect(out[15] == "line-15");
        expect(out[16] == "MARK");
        expect(out[17] == "line-17");
        expect(out[32] == "line-32");
    };

    "空输入 / 单条：原样返回"_test = [] {
        std::vector<std::string> empty;
        expect(headTailCapWithMarker(empty, std::string("M")).empty());
        std::vector<std::string> one{"only"};
        auto out = headTailCapWithMarker(one, std::string("M"));
        expect(out.size() == 1 && out[0] == "only");
    };

    "CJK 行完整保留（按条操作不劈半多字节）"_test = [] {
        std::vector<std::string> items;
        for (int i = 0; i < 40; ++i) items.push_back("你好世界第" + std::to_string(i) + "行");
        auto out = headTailCapWithMarker(items, std::string("MARK"));
        expect(out.size() == 33);
        expect(out[0] == "你好世界第0行");
        expect(out[32] == "你好世界第39行");
        for (const auto& l : out)
            if (l != "MARK") expect(l.find("你好世界") != std::string::npos);
    };

    // ========== T2'': renderDiff 截断形状（条目级） ==========

    "100 条渲染条目：前16 + 省略标记 + 后16，首尾保留"_test = [] {
        std::vector<RenderEntry> entries;
        for (int i = 0; i < 100; ++i) entries.push_back({"diff-line-" + std::to_string(i), i % 3});
        auto out = headTailCapWithMarker(entries, RenderEntry{"  … 其余 68 行", -1});
        expect(out.size() == 33);
        expect(out[0].text == "diff-line-0");
        expect(out[15].text == "diff-line-15");
        expect(out[16].text.find("其余 68 行") != std::string::npos);
        expect(out[16].style == -1);
        expect(out[17].text == "diff-line-84");
        expect(out[32].text == "diff-line-99");
    };

    "≤32 条渲染条目：不截断"_test = [] {
        std::vector<RenderEntry> entries;
        for (int i = 0; i < 32; ++i) entries.push_back({"d" + std::to_string(i), 0});
        auto out = headTailCapWithMarker(entries, RenderEntry{"M", -1});
        expect(out.size() == 32);
        expect(out[31].text == "d31");
    };

    // ========== T3: emitError 首行化（P0-1） ==========

    "多行错误：仅首行可见"_test = [] {
        CLF::CLFUI::CLFTerminal term;  // 无 screen，requestRefresh 安全跳过
        term.emitError("first line\nsecond line\nthird line");
        auto snap = term.contentSnapshot();
        // emitError 不带换行 → 内容在 pendingLine（与改造前行为一致），两处都要查
        auto contains = [&](const std::string& needle) {
            for (const auto& l : snap.lines)
                if (l.find(needle) != std::string::npos) return true;
            return snap.pendingLine.find(needle) != std::string::npos;
        };
        expect(contains("first line"));
        expect(!contains("second line"));
    };

    "超长单行错误：截断含省略号，尾行内容丢弃"_test = [] {
        CLF::CLFUI::CLFTerminal term;
        std::string longMsg(300, 'a');
        longMsg += "\ntail-must-not-appear";
        term.emitError(longMsg);
        auto snap = term.contentSnapshot();
        auto contains = [&](const std::string& needle) {
            for (const auto& l : snap.lines)
                if (l.find(needle) != std::string::npos) return true;
            return snap.pendingLine.find(needle) != std::string::npos;
        };
        expect(contains("…"));
        expect(!contains("tail-must-not-appear"));
    };

    // ========== T9: 本地时间戳（P2-3） ==========

    "T9a localTimeStamp 无日期：HH:mm 格式"_test = [] {
        auto ts = CLF::CLFCore::localTimeStamp();
        expect(ts.size() == 5);
        expect(ts[2] == ':');
        for (size_t i = 0; i < ts.size(); ++i)
            if (i != 2) expect(ts[i] >= '0' && ts[i] <= '9');
    };

    "T9b 带日期 MM-DD HH:mm + localDateStamp YYYY-MM-DD"_test = [] {
        auto ts = CLF::CLFCore::localTimeStamp(true);
        expect(ts.size() == 11);
        expect(ts[2] == '-' && ts[5] == ' ' && ts[8] == ':');
        auto ds = CLF::CLFCore::localDateStamp();
        expect(ds.size() == 10);
        expect(ds[4] == '-' && ds[7] == '-');
    };
};

int main() {}
