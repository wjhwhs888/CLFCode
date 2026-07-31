// qa_CLFContext.cpp — CLFContext 单元测试
// 覆盖：token 估算、system 永不截断、长工具结果截断、serialize/restore round-trip

#include <boost/ut.hpp>
#include "CLFCore/CLFContext.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFContext;
using CLF::CLFCore::CLFMessage;
using CLF::CLFCore::CLFToolCall;

const boost::ut::suite<"CLFContext"> tests = [] {
    "token 估算：纯 ASCII 文本按 0.25 token/字"_test = [] {
        CLFContext ctx;
        ctx.addMessage("user", std::string(400, 'a')); // 400 字符 → ~100 token
        expect(ctx.estimateTokens() >= 90 && ctx.estimateTokens() <= 110);
    };

    "token 估算：纯中文按 1.5 token/字"_test = [] {
        CLFContext ctx;
        std::string chinese(100, '\xe6\x9d\x8e'); // 100 个中文字符（UTF-8 3字节）
        ctx.addMessage("user", chinese);
        // 100 字 × 1.5 = 150 token
        expect(ctx.estimateTokens() >= 140 && ctx.estimateTokens() <= 160);
    };

    "system 消息永不截断"_test = [] {
        CLFContext ctx(100); // 极小窗口
        ctx.addMessage("system", "system rules that must never be truncated");
        for (int i = 0; i < 50; ++i) {
            ctx.addMessage("user", std::string(50, 'x')); // 每条 ~12 token
        }
        auto messages = ctx.getMessages();
        expect(messages.size() >= 1);
        expect(messages.front().m_role == "system");
    };

    "长工具结果自动截断 + 标记"_test = [] {
        CLFContext ctx;
        std::string huge(20000, 'y'); // 超 8000 字符
        ctx.addToolResult("call_1", "read_file", huge);
        auto messages = ctx.getMessages();
        expect(messages.size() == 1);
        expect(messages[0].m_content.find("[truncated") != std::string::npos);
    };

    "serialize/restore round-trip 保留 tool_calls 全字段"_test = [] {
        CLFContext ctx;
        ctx.addMessage("system", "identity");
        ctx.addMessage("user", "hello");

        CLFToolCall tc;
        tc.m_id        = "call_abc";
        tc.m_name      = "get_time";
        tc.m_arguments = "{}";
        ctx.addAssistantToolCalls({tc});

        ctx.addToolResult("call_abc", "get_time", "2026-07-31");

        std::string json = ctx.serialize();

        CLFContext restored;
        expect(restored.restore(json));
        auto messages = restored.getMessages();

        // system 被跳过（身份由 Agent 重新注入）
        expect(messages.size() == 3);
        expect(messages[0].m_role == "user");
        expect(messages[1].m_role == "assistant");
        expect(messages[1].m_toolCalls.size() == 1);
        expect(messages[1].m_toolCalls[0].m_id == "call_abc");
        expect(messages[1].m_toolCalls[0].m_name == "get_time");
        expect(messages[2].m_role == "tool");
        expect(messages[2].m_toolCallId == "call_abc");
    };

    "restore 无效 JSON 返回 false"_test = [] {
        CLFContext ctx;
        expect(!ctx.restore("not valid json{{"));
    };
};

// Boost.UT：测试在静态初始化时注册，cfg 析构时自动运行并输出报告
int main() {
    return 0;
}
