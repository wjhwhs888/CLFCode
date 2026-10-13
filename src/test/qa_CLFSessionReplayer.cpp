// qa_CLFSessionReplayer.cpp — 会话恢复事件重放器测试（事件化重构分册 2 §六）
// 覆盖：R1 回显含工具痕迹（event_log → HistoryToolTrace）/ R2 summary 回显 /
// R3 装饰剥离（老文件 worked 行 + 📋 段）/ R4 老文件无 event_log 兜底
// （设计 2.4 兼容硬约束：只剥已知、不误伤、不改原文件）

#include <boost/ut.hpp>

#include <fstream>
#include <map>
#include <vector>

#include "CLFTestTempDir.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSessionReplayer.hpp"
#include "CLFTypes/CLFEventBus.hpp"

using namespace boost::ut;
using CLF::CLFTypes::CLFAgentEvent;
using CLF::CLFTypes::CLFEventBus;
using CLF::CLFTypes::CLFEventType;
using CLF::CLFCore::CLFSessionReplayer;

namespace {

// 订阅收集全部事件（按类型分组断言）
struct EventCollector {
    std::vector<CLFAgentEvent> all;
    void on(const CLFAgentEvent& ev) { all.push_back(ev); }
    size_t count(CLFEventType t) const {
        size_t n = 0;
        for (const auto& e : all) if (e.m_type == t) ++n;
        return n;
    }
    std::vector<CLFAgentEvent> of(CLFEventType t) const {
        std::vector<CLFAgentEvent> out;
        for (const auto& e : all) if (e.m_type == t) out.push_back(e);
        return out;
    }
};

// 写 jsonl 测试文件（行数组），返回路径
std::string writeJsonl(CLFTest::CLFTestTempDir& dir, const char* name,
                       const std::vector<std::string>& lines) {
    std::string p = dir.string() + "/" + name;
    std::ofstream f(std::filesystem::u8path(p), std::ios::binary);
    for (const auto& l : lines) f << l << "\n";
    return p;
}

} // anonymous namespace

const boost::ut::suite<"CLFSessionReplayer"> tests = [] {
    "R1 回显含工具痕迹：event_log → HistoryToolTrace（callId/name/ok/summary）"_test = [] {
        CLFTest::CLFTestTempDir dir{"clf_replay_r1_"};
        const std::string path = writeJsonl(dir, "r1.jsonl", {
            R"({"type":"header","title":"t","session_id":"s","timestamp":"2026-09-07 10:00:00","model":"m","skills":[]})",
            R"({"type":"turn","turnId":"turn-1","messages":[{"role":"user","content":"q1"},{"role":"assistant","content":"a1"},{"role":"tool","tool_call_id":"call_1","name":"read_file","content":"file content"}]})",
            R"({"type":"event_log","turnId":"turn-1","callId":"call_1","name":"read_file","ok":true,"summary":"file content","seq":42})",
        });

        CLFEventBus bus(false);
        EventCollector col;
        bus.subscribe([&](const CLFAgentEvent& ev) { col.on(ev); },
                      CLF::CLFTypes::ChannelPolicy::Lossless);
        size_t n = CLFSessionReplayer::replay(path, &bus);
        bus.drainAll();

        expect(n >= 4u);   // header + user + assistant + tooltrace + ResumeDone
        expect(col.count(CLFEventType::HistoryHeader) == 1_u);
        expect(col.count(CLFEventType::HistoryUser) == 1_u);
        expect(col.count(CLFEventType::HistoryAssistant) == 1_u);
        // 新文件：工具痕迹由 event_log 权威重放（turn 内 tool 消息跳过防重复）
        const auto traces = col.of(CLFEventType::HistoryToolTrace);
        expect(traces.size() == 1_u);
        expect(traces[0].m_callId == "call_1");
        expect(traces[0].m_name == "read_file");
        expect(traces[0].m_ok);
        expect(traces[0].m_summary == "file content");
        expect(col.count(CLFEventType::ResumeDone) == 1_u);
    };

    "R2 summary 回显：HistorySummary 事件"_test = [] {
        CLFTest::CLFTestTempDir dir{"clf_replay_r2_"};
        const std::string path = writeJsonl(dir, "r2.jsonl", {
            R"({"type":"header","title":"t","session_id":"s","timestamp":"2026-09-07 10:00:00","model":"m","skills":[]})",
            R"({"type":"summary","ts":"2026-09-07 10:00:00","summary":{"text":"压缩摘要内容","method":"api"}})",
        });

        CLFEventBus bus(false);
        EventCollector col;
        bus.subscribe([&](const CLFAgentEvent& ev) { col.on(ev); },
                      CLF::CLFTypes::ChannelPolicy::Lossless);
        CLFSessionReplayer::replay(path, &bus);
        bus.drainAll();

        const auto summaries = col.of(CLFEventType::HistorySummary);
        expect(summaries.size() == 1_u);
        expect(summaries[0].m_text == "压缩摘要内容");
    };

    "R3 装饰剥离：worked 行 + 📋 段剥除，模型内容合法 ✻ 不动"_test = [] {
        CLFTest::CLFTestTempDir dir{"clf_replay_r3_"};
        const std::string path = writeJsonl(dir, "r3.jsonl", {
            R"({"type":"header","title":"t","session_id":"s","timestamp":"2026-09-07 10:00:00","model":"m","skills":[]})",
            // 老文件 assistant content 含程序拼的装饰：
            // 模型内容 "正文结果" + worked 行 + 📋 收尾段
            R"({"type":"turn","messages":[{"role":"user","content":"q"},{"role":"assistant","content":"正文结果\n \n✻ Worked for 38s\n \n📋 任务清单（全部完成）:\n  ✓ 任务A\n  ✓ 任务B\n"}]})",
        });

        CLFEventBus bus(false);
        EventCollector col;
        bus.subscribe([&](const CLFAgentEvent& ev) { col.on(ev); },
                      CLF::CLFTypes::ChannelPolicy::Lossless);
        CLFSessionReplayer::replay(path, &bus);
        bus.drainAll();

        const auto assistants = col.of(CLFEventType::HistoryAssistant);
        expect(assistants.size() == 1_u);
        const std::string& text = assistants[0].m_text;
        expect(text.find("✻") == std::string::npos);   // worked 行剥除
        expect(text.find("📋") == std::string::npos);   // 📋 段剥除
        expect(text.find("任务A") == std::string::npos);
        expect(text.find("正文结果") != std::string::npos);  // 模型内容保留
    };

    "R3b 不误伤：模型内容合法 ✻ 行保留"_test = [] {
        CLFTest::CLFTestTempDir dir{"clf_replay_r3b_"};
        const std::string path = writeJsonl(dir, "r3b.jsonl", {
            R"({"type":"header","title":"t","session_id":"s","timestamp":"2026-09-07 10:00:00","model":"m","skills":[]})",
            // ✻ 行但无 " for Ns" 结构（模型自然文本）→ 不剥
            R"({"type":"turn","messages":[{"role":"user","content":"q"},{"role":"assistant","content":"✻ 这是重点标记\n正常文本"}]})",
        });

        CLFEventBus bus(false);
        EventCollector col;
        bus.subscribe([&](const CLFAgentEvent& ev) { col.on(ev); },
                      CLF::CLFTypes::ChannelPolicy::Lossless);
        CLFSessionReplayer::replay(path, &bus);
        bus.drainAll();

        const auto assistants = col.of(CLFEventType::HistoryAssistant);
        expect(assistants.size() == 1_u);
        expect(assistants[0].m_text.find("✻ 这是重点标记") != std::string::npos);
    };

    "R4 老文件无 event_log：turn 内 tool 消息兜底重放"_test = [] {
        CLFTest::CLFTestTempDir dir{"clf_replay_r4_"};
        const std::string path = writeJsonl(dir, "r4.jsonl", {
            R"({"type":"header","title":"t","session_id":"s","timestamp":"2026-09-07 10:00:00","model":"m","skills":[]})",
            R"({"type":"turn","messages":[{"role":"user","content":"q"},{"role":"assistant","content":"a"},{"role":"tool","tool_call_id":"old_1","name":"get_time","content":"12:00"}]})",
        });

        CLFEventBus bus(false);
        EventCollector col;
        bus.subscribe([&](const CLFAgentEvent& ev) { col.on(ev); },
                      CLF::CLFTypes::ChannelPolicy::Lossless);
        CLFSessionReplayer::replay(path, &bus);
        bus.drainAll();

        const auto traces = col.of(CLFEventType::HistoryToolTrace);
        expect(traces.size() == 1_u);   // 老文件兜底：工具痕迹可见
        expect(traces[0].m_callId == "old_1");
        expect(traces[0].m_name == "get_time");
        expect(traces[0].m_summary == "12:00");
    };

    "R5 老文件无 turnId：临时 id 生成（仅显示用）"_test = [] {
        CLFTest::CLFTestTempDir dir{"clf_replay_r5_"};
        const std::string path = writeJsonl(dir, "r5.jsonl", {
            R"({"type":"header","title":"t","session_id":"s","timestamp":"2026-09-07 10:00:00","model":"m","skills":[]})",
            R"({"type":"turn","messages":[{"role":"user","content":"q"}]})",
        });

        CLFEventBus bus(false);
        EventCollector col;
        bus.subscribe([&](const CLFAgentEvent& ev) { col.on(ev); },
                      CLF::CLFTypes::ChannelPolicy::Lossless);
        CLFSessionReplayer::replay(path, &bus);
        bus.drainAll();

        const auto users = col.of(CLFEventType::HistoryUser);
        expect(users.size() == 1_u);
        expect(users[0].m_turnId.find("resume-turn-") == 0);   // 临时 id
    };
};

int main() {}
