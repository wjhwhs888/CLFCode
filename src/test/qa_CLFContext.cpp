// qa_CLFContext.cpp — CLFContext 单元测试
// 覆盖：token 估算、system 永不截断、长工具结果截断
// （serialize/restore 用例随 A3 删除——覆盖式时代语义，jsonl 时代由
//   CLFSessionManager::load/loadJsonl 承担，restoreSession 分流调用）

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
};

// Boost.UT：测试在静态初始化时注册，cfg 析构时自动运行并输出报告
int main() {
    return 0;
}
