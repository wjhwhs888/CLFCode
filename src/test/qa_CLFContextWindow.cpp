// qa_CLFContextWindow.cpp — 消息窗口截断策略测试（C2b，2026-09-07）
// W1-W4: system 永不截断 / 尾部保留 / 超预算丢弃旧消息 / 空边界
// 另含 truncateToolResult（C2b 自 CLFContext 移出至 CLFTextUtil）

#include <boost/ut.hpp>

#include <string>
#include <vector>

#include "CLFCore/CLFContextWindow.hpp"
#include "CLFTypes/CLFTextUtil.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFContextWindow;
using CLF::CLFCore::CLFMessage;
using CLF::CLFCore::CLFTextUtil;

namespace {

CLFMessage makeMsg(const std::string& role, const std::string& content) {
    CLFMessage m;
    m.m_role = role;
    m.m_content = content;
    return m;
}

} // anonymous namespace

const boost::ut::suite<"CLFContextWindow"> tests = [] {
    "W1 system 消息永不截断"_test = [] {
        CLFContextWindow window(100);   // 极小窗口
        std::vector<CLFMessage> msgs;
        msgs.push_back(makeMsg("system", "system rules that must never be truncated"));
        for (int i = 0; i < 50; ++i) {
            msgs.push_back(makeMsg("user", std::string(50, 'x')));   // 每条 ~12 token
        }
        auto out = window.apply(msgs);
        expect(out.size() >= 1u);
        expect(out.front().m_role == "system");
        // system 内容原样保留
        expect(out.front().m_content == std::string("system rules that must never be truncated"));
    };

    "W2 尾部保留：超预算丢弃头部旧消息"_test = [] {
        // 每条 user 消息 50 字符 ≈ 12-13 token；窗口 50 token → 约 4 条
        CLFContextWindow window(50);
        std::vector<CLFMessage> msgs;
        for (int i = 0; i < 10; ++i) {
            msgs.push_back(makeMsg("user", std::string(50, 'x')));
        }
        auto out = window.apply(msgs);
        expect(out.size() >= 1u && out.size() < 10u);   // 有截断
        // 全部为 user 且数量 = 保留尾部若干条
        for (const auto& m : out) expect(m.m_role == "user");
    };

    "W3 全量消息不超预算：原样返回"_test = [] {
        CLFContextWindow window(100000);
        std::vector<CLFMessage> msgs = {
            makeMsg("user", "a"), makeMsg("assistant", "b")};
        auto out = window.apply(msgs);
        expect(out.size() == 2u);
    };

    "W4 空输入：空输出"_test = [] {
        CLFContextWindow window(100);
        std::vector<CLFMessage> empty;
        expect(window.apply(empty).empty());
    };

    "T1 truncateToolResult：未超阈值原样返回"_test = [] {
        std::string s(100, 'y');
        expect(CLFTextUtil::truncateToolResult(s) == s);
    };

    "T2 truncateToolResult：超 8000 截断 + 标记"_test = [] {
        std::string huge(20000, 'y');
        auto r = CLFTextUtil::truncateToolResult(huge);
        expect(r.size() < huge.size());
        expect(r.find("[truncated") != std::string::npos);
        expect(r.find("20000 chars") != std::string::npos);
    };
};

int main() {}
