// qa_CLFSessionWriter.cpp — 会话落库订阅者测试（事件化重构分册 2 §六）
// 覆盖：W1 TurnFinished→落库（纯消息无装饰）/ W2 落库失败不阻塞 /
// W3 手动模式缓冲不丢事件 / W4 turn 行 turnId 字段 + event_log 明细
//
// qa 线程纪律：startThread=false（pumpOnce 手动泵——静态期不起后台线程）。

#include <boost/ut.hpp>

#include <fstream>
#include <nlohmann/json.hpp>

#include "CLFTestTempDir.hpp"
#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFMessageCodec.hpp"
#include "CLFCore/CLFSessionFileCtx.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSessionWriter.hpp"
#include "CLFNetwork/CLFHttpClient.hpp"
#include "CLFTypes/CLFEventBus.hpp"

using namespace boost::ut;
using CLF::CLFTypes::CLFAgentEvent;
using CLF::CLFTypes::CLFEventBus;
using CLF::CLFTypes::CLFEventType;
using CLF::CLFCore::CLFSessionFileCtx;
using CLF::CLFCore::CLFSessionWriter;

namespace {

CLFAgentEvent makeEvent(CLFEventType type) {
    CLFAgentEvent ev;
    ev.m_type = type;
    return ev;
}

// 读 jsonl 全部行（nlohmann 对象列表）
std::vector<nlohmann::json> readLines(const std::string& path) {
    std::vector<nlohmann::json> lines;
    std::ifstream f(std::filesystem::u8path(path));
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        try { lines.push_back(nlohmann::json::parse(line)); } catch (...) {}
    }
    return lines;
}

// 端到端最小 HTTP mock（同步路径单响应，无工具）
struct MiniHttp : public CLF::CLFNetwork::ICLFHttpClient {
    CLF::CLFNetwork::CLFHttpResponse postJson(const std::string&, const std::string&) override {
        return {200,
                R"({"choices":[{"message":{"role":"assistant","content":"好"},"finish_reason":"stop"}]})",
                ""};
    }
    CLF::CLFNetwork::CLFHttpResponse postJsonStream(
        const std::string&, const std::string&,
        std::function<void(const std::string&)>) override {
        return {200, "", ""};
    }
    void abort() override {}
    void setTimeout(int) override {}
};

} // anonymous namespace

const boost::ut::suite<"CLFSessionWriter"> tests = [] {
    "W1 TurnFinished 落库：turn 行纯消息无装饰 + user/assistant/tool 齐全"_test = [] {
        CLFTest::CLFTestTempDir dir{"clf_writer_w1_"};
        CLFSessionFileCtx ctx;
        ctx.setHistoryDir(dir.string());
        const std::string path = ctx.beginSessionFile("标题测试", "test-model", {});

        CLFEventBus bus(false);
        CLFSessionWriter writer(&bus,
                              [&ctx] { return ctx.getActiveSessionFile(); },
                              /*startThread=*/false);

        // 事件序列：TurnStarted → ToolFinished×2 → TurnFinished
        CLFAgentEvent started = makeEvent(CLFEventType::TurnStarted);
        started.m_turnId = "turn-1";
        started.m_text = "帮我查一下";
        bus.publish(started);

        CLFAgentEvent tool1 = makeEvent(CLFEventType::ToolFinished);
        tool1.m_turnId = "turn-1";
        tool1.m_callId = "call_1";
        tool1.m_name = "read_file";
        tool1.m_ok = true;
        tool1.m_summary = "文件内容：hello";
        bus.publish(tool1);

        CLFAgentEvent tool2 = makeEvent(CLFEventType::ToolFinished);
        tool2.m_turnId = "turn-1";
        tool2.m_callId = "call_2";
        tool2.m_name = "get_time";
        tool2.m_ok = true;
        tool2.m_summary = "12:00";
        bus.publish(tool2);

        CLFAgentEvent finished = makeEvent(CLFEventType::TurnFinished);
        finished.m_turnId = "turn-1";
        finished.m_text = "查询完成";   // assistant 最终全文（无 ✻/📋 装饰）
        finished.m_workedSec = 3;
        bus.publish(finished);

        bus.drainAll();   // 双层队列：总线订阅队列 → writer 内部队列
expect(writer.pumpOnce() == 4_u);

        const auto lines = readLines(path);
        // header + turn + event_log×2
        expect(lines.size() == 4_u);
        const auto& turn = lines[1];
        expect(turn["type"] == "turn");
        expect(turn["turnId"] == "turn-1");          // 分册 2 新字段
        const auto& msgs = turn["messages"];
        expect(msgs.size() == 4_u);                   // user + assistant + tool×2
        expect(msgs[0]["role"] == "user");
        expect(msgs[0]["content"] == "帮我查一下");
        expect(msgs[1]["role"] == "assistant");
        expect(msgs[1]["content"] == "查询完成");
        expect(msgs[2]["role"] == "tool");
        expect(msgs[2]["content"] == "文件内容：hello");
        expect(msgs[3]["role"] == "tool");
        // 纯消息无装饰断言（W1 核心）：assistant content 不含 ✻/📋
        expect(msgs[1]["content"].get<std::string>().find("✻") == std::string::npos);
        expect(msgs[1]["content"].get<std::string>().find("📋") == std::string::npos);

        // event_log 明细：一行一工具（callId/name/ok/summary/seq 齐全）
        const auto& el1 = lines[2];
        expect(el1["type"] == "event_log");
        expect(el1["turnId"] == "turn-1");
        expect(el1["callId"] == "call_1");
        expect(el1["name"] == "read_file");
        expect(el1["ok"] == true);
        expect(el1["summary"] == "文件内容：hello");
        const auto& el2 = lines[3];
        expect(el2["callId"] == "call_2");
        expect(el2["seq"] > el1["seq"]);              // 总线 seq 保序
    };

    "W2 无活动文件：落库跳过不抛（回合不受影响）"_test = [] {
        CLFTest::CLFTestTempDir dir{"clf_writer_w2_"};
        CLFSessionFileCtx ctx;   // 未 beginSessionFile——无活动文件
        CLFEventBus bus(false);
        CLFSessionWriter writer(&bus,
                              [&ctx] { return ctx.getActiveSessionFile(); },
                              /*startThread=*/false);

        CLFAgentEvent finished = makeEvent(CLFEventType::TurnFinished);
        finished.m_turnId = "turn-1";
        finished.m_text = "内容";
        bus.publish(finished);
        // 不抛即过（warn 落日志）；同时 ToolFinished 累积状态应正常清空
        bus.drainAll();
expect(writer.pumpOnce() == 1_u);
    };

    "W3 手动模式缓冲：多事件泵一轮全处理不丢"_test = [] {
        CLFTest::CLFTestTempDir dir{"clf_writer_w3_"};
        CLFSessionFileCtx ctx;
        ctx.setHistoryDir(dir.string());
        const std::string path = ctx.beginSessionFile("t", "m", {});

        CLFEventBus bus(false);
        CLFSessionWriter writer(&bus,
                              [&ctx] { return ctx.getActiveSessionFile(); },
                              /*startThread=*/false);

        for (int i = 0; i < 20; ++i) {   // 20 个回合
            CLFAgentEvent s = makeEvent(CLFEventType::TurnStarted);
            s.m_turnId = "turn-" + std::to_string(i + 1);
            s.m_text = "问题" + std::to_string(i + 1);
            bus.publish(s);
            CLFAgentEvent f = makeEvent(CLFEventType::TurnFinished);
            f.m_turnId = s.m_turnId;
            f.m_text = "回答" + std::to_string(i + 1);
            bus.publish(f);
        }
        bus.drainAll();
expect(writer.pumpOnce() == 40_u);   // 全量处理不丢

        const auto lines = readLines(path);
        expect(lines.size() == 21_u);        // header + 20 turn
        expect(lines[1]["turnId"] == "turn-1");
        expect(lines[20]["turnId"] == "turn-20");
    };

    "W4 TodoUpdated 即时落库：快照行不等轮末"_test = [] {
        CLFTest::CLFTestTempDir dir{"clf_writer_w4_"};
        CLFSessionFileCtx ctx;
        ctx.setHistoryDir(dir.string());
        const std::string path = ctx.beginSessionFile("t", "m", {});

        CLFEventBus bus(false);
        CLFSessionWriter writer(&bus,
                              [&ctx] { return ctx.getActiveSessionFile(); },
                              /*startThread=*/false);

        CLFAgentEvent todo = makeEvent(CLFEventType::TodoUpdated);
        todo.m_turnId = "turn-1";
        CLF::CLFCore::CLFTodoItem item;
        item.m_id = "1";
        item.m_content = "任务A";
        item.m_status = "in_progress";
        todo.m_todos = {item};
        bus.publish(todo);
        bus.drainAll();
        expect(writer.pumpOnce() == 1_u);

        const auto lines = readLines(path);
        expect(lines.size() == 2_u);         // header + todo_snapshot
        expect(lines[1]["type"] == "todo_snapshot");
        expect(lines[1]["todos"][0]["id"] == "1");
    };

    "W5 端到端：AgentLoop + 总线 + writer → 新会话落库全链路"_test = [] {
        CLFTest::CLFTestTempDir dir{"clf_writer_w5_"};
        CLFSessionFileCtx ctx;
        ctx.setHistoryDir(dir.string());
        const std::string path = ctx.beginSessionFile("端到端", "test-model", {});

        CLFEventBus bus(false);
        CLFSessionWriter writer(&bus,
                                [&ctx] { return ctx.getActiveSessionFile(); },
                                /*startThread=*/false);

        CLF::CLFCore::CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = false;
        auto http = std::make_shared<MiniHttp>();
        CLF::CLFCore::CLFAgentLoop agent(config, http);
        agent.setEventBus(&bus);
        agent.setHistoryDir(dir.string());

        agent.runTurn("你好");
        bus.drainAll();
        writer.pumpOnce();

        const auto lines = readLines(path);
        expect(lines.size() == 2_u);         // header + turn
        const auto& turn = lines[1];
        expect(turn["type"] == "turn");
        expect(!turn["turnId"].get<std::string>().empty());
        const auto& msgs = turn["messages"];
        expect(msgs.size() == 2_u);          // user + assistant
        expect(msgs[0]["role"] == "user");
        expect(msgs[0]["content"] == "你好");
        expect(msgs[1]["role"] == "assistant");
        // 装饰断言（✻ 不在落库内容）留待分册 2 块 1 文案迁移完成后启用
        expect(msgs[1]["content"].get<std::string>().find("好") != std::string::npos);
    };

    "W6 中断回合落库：user 输入 + 已执行工具痕迹（无 assistant）"_test = [] {
        CLFTest::CLFTestTempDir dir{"clf_writer_w6_"};
        CLFSessionFileCtx ctx;
        ctx.setHistoryDir(dir.string());
        const std::string path = ctx.beginSessionFile("中断回合", "m", {});

        CLFEventBus bus(false);
        CLFSessionWriter writer(&bus,
                                [&ctx] { return ctx.getActiveSessionFile(); },
                                /*startThread=*/false);

        // TurnStarted → ToolFinished（已执行 1 个）→ Interrupted
        CLFAgentEvent s = makeEvent(CLFEventType::TurnStarted);
        s.m_turnId = "turn-9";
        s.m_text = "帮我跑命令";
        bus.publish(s);
        CLFAgentEvent t = makeEvent(CLFEventType::ToolFinished);
        t.m_turnId = "turn-9";
        t.m_callId = "call_x";
        t.m_name = "execute_command";
        t.m_ok = true;
        t.m_summary = "部分输出";
        bus.publish(t);
        CLFAgentEvent i = makeEvent(CLFEventType::Interrupted);
        i.m_turnId = "turn-9";
        bus.publish(i);

        bus.drainAll();
        writer.pumpOnce();

        const auto lines = readLines(path);
        expect(lines.size() == 3_u);          // header + turn + event_log
        const auto& turn = lines[1];
        expect(turn["turnId"] == "turn-9");   // 中断轮不跳号（实机实抓回归钉）
        const auto& msgs = turn["messages"];
        // resume 400 修根（2026-10-14）：中断轮也带合成 assistant + tool_calls
        // 声明——协议闭合（否则恢复上下文 tool 结果孤悬被 API 拒绝）
        expect(msgs.size() == 3_u);           // user + 合成 assistant + tool
        expect(msgs[0]["role"] == "user");
        expect(msgs[0]["content"] == "帮我跑命令");
        expect(msgs[1]["role"] == "assistant");
        expect(msgs[1]["content"] == "");
        expect(msgs[1]["tool_calls"].size() == 1_u);
        expect(msgs[1]["tool_calls"][0]["id"] == "call_x");
        expect(msgs[1]["tool_calls"][0]["name"] == "execute_command");
        expect(msgs[2]["role"] == "tool");
        expect(msgs[2]["content"] == "部分输出");
        expect(lines[2]["type"] == "event_log");
        expect(lines[2]["callId"] == "call_x");

        // 下一回合正常（状态已清空——TurnStarted 重新累积）
        CLFAgentEvent s2 = makeEvent(CLFEventType::TurnStarted);
        s2.m_turnId = "turn-10";
        s2.m_text = "继续";
        bus.publish(s2);
        CLFAgentEvent f2 = makeEvent(CLFEventType::TurnFinished);
        f2.m_turnId = "turn-10";
        f2.m_text = "完成";
        bus.publish(f2);
        bus.drainAll();
        writer.pumpOnce();
        const auto lines2 = readLines(path);
        expect(lines2.size() == 4_u);         // + turn（无工具 → 无 event_log）
        expect(lines2[3]["turnId"] == "turn-10");
        expect(lines2[3]["messages"].size() == 2_u);  // 仅 user + assistant
    };
};

int main() {}
