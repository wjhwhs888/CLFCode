// qa_CLFContext.cpp — CLFContext 单元测试
// 覆盖：token 估算、纯容器语义（全量返回、无截断）
// C2b（2026-09-07）：窗口截断用例迁 qa_CLFContextWindow；
// 长工具结果截断用例迁 qa_CLFTextUtil 系（truncateToolResult 归位 CLFTextUtil）
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

    "C2b 纯容器：getMessages 全量返回（无截断）"_test = [] {
        CLFContext ctx;
        for (int i = 0; i < 50; ++i) {
            ctx.addMessage("user", std::string(50, 'x'));
        }
        auto messages = ctx.getMessages();
        expect(messages.size() == 50u);   // 全量，不做窗口截断
    };

    "C2b 纯容器：addToolResult 不做内容截断（策略归调用方）"_test = [] {
        CLFContext ctx;
        std::string huge(20000, 'y');
        ctx.addToolResult("call_1", "read_file", huge);
        auto messages = ctx.getMessages();
        expect(messages.size() == 1u);
        expect(messages[0].m_content.size() == huge.size());   // 原样入库
    };

    "setSystemPrompt 去重 + 单条语义"_test = [] {
        CLFContext ctx;
        ctx.setSystemPrompt("v1");
        ctx.setSystemPrompt("v1");   // 相同内容跳过
        ctx.setSystemPrompt("v2");   // 替换
        auto messages = ctx.getMessages();
        expect(messages.size() == 1u);
        expect(messages[0].m_role == "system");
        expect(messages[0].m_content == "v2");
    };

    "removeSystemMessages 仅移除 system"_test = [] {
        CLFContext ctx;
        ctx.setSystemPrompt("sys");
        ctx.addMessage("user", "keep");
        ctx.removeSystemMessages();
        auto messages = ctx.getMessages();
        expect(messages.size() == 1u);
        expect(messages[0].m_role == "user");
    };
};

// Boost.UT：测试在静态初始化时注册，cfg 析构时自动运行并输出报告
int main() {
    return 0;
}
