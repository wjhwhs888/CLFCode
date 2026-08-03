// qa_CLFStreamAccumulator.cpp — SSE 流式累积器单元测试
// 覆盖：文本累积、tool_calls delta 合并、finish_reason 自动 finalize、
//       markDone 补 stop、index 乱序、reset、多 tool 并行

#include <boost/ut.hpp>
#include "CLFCore/CLFStreamAccumulator.hpp"

#include <nlohmann/json.hpp>

using namespace boost::ut;
using json = nlohmann::json;

suite qa_CLFStreamAccumulator = [] {
    "text_delta"_test = [] {
        CLF::CLFCore::CLFStreamAccumulator acc;
        auto chunk = acc.feedDelta(json::parse(R"({"content":"Hello"})"));
        expect(chunk == "Hello");
        expect(acc.getContent() == "Hello");
        expect(acc.getToolCalls().empty());
    };

    "tool_call_delta"_test = [] {
        CLF::CLFCore::CLFStreamAccumulator acc;
        acc.feedDelta(json::parse(R"({
            "tool_calls": [{"index":0, "id":"call_1", "function":{"name":"read_file","arguments":"{\"p"}}]
        })"));
        acc.feedDelta(json::parse(R"({
            "tool_calls": [{"index":0, "function":{"arguments":"ath\":\"test.txt\"}"}}]
        })"));
        expect(acc.getToolCalls().empty()); // 未 finalize
        acc.markDone();
        expect(acc.getToolCalls().size() == 1);
        expect(acc.getToolCalls()[0].m_name == "read_file");
        expect(acc.getToolCalls()[0].m_arguments == R"({"path":"test.txt"})");
    };

    "finish_reason_auto_finalize"_test = [] {
        CLF::CLFCore::CLFStreamAccumulator acc;
        acc.feedDelta(json::parse(R"({
            "tool_calls": [{"index":0, "id":"c1", "function":{"name":"echo","arguments":"{}"}}]
        })"));
        // finish_reason → 自动 finalize，无需额外 markDone
        acc.feedDelta(json::parse(R"({"finish_reason":"tool_calls"})"));
        expect(acc.getToolCalls().size() == 1);
        expect(acc.getFinishReason() == "tool_calls");
    };

    "markDone_no_finish_reason_defaults_stop"_test = [] {
        CLF::CLFCore::CLFStreamAccumulator acc;
        acc.markDone();
        expect(acc.isFinished());
        expect(acc.getFinishReason() == "stop");
    };

    "multi_tool_parallel"_test = [] {
        CLF::CLFCore::CLFStreamAccumulator acc;
        acc.feedDelta(json::parse(R"({
            "tool_calls": [
                {"index":0, "id":"a", "function":{"name":"read","arguments":"{}"}},
                {"index":1, "id":"b", "function":{"name":"write","arguments":"{}"}}
            ]
        })"));
        acc.markDone();
        expect(acc.getToolCalls().size() == 2);
        expect(acc.getToolCalls()[0].m_id == "a");
        expect(acc.getToolCalls()[1].m_id == "b");
    };

    "out_of_order_index"_test = [] {
        CLF::CLFCore::CLFStreamAccumulator acc;
        acc.feedDelta(json::parse(R"({
            "tool_calls": [{"index":2, "id":"c", "function":{"name":"third","arguments":"{}"}}]
        })"));
        acc.feedDelta(json::parse(R"({
            "tool_calls": [{"index":0, "id":"a", "function":{"name":"first","arguments":"{}"}}]
        })"));
        acc.markDone();
        expect(acc.getToolCalls().size() == 2);
        expect(acc.getToolCalls()[0].m_name == "first");
        expect(acc.getToolCalls()[1].m_name == "third");
    };

    "content_and_tool_calls"_test = [] {
        CLF::CLFCore::CLFStreamAccumulator acc;
        auto c1 = acc.feedDelta(json::parse(R"({"content":"Thinking"})"));
        expect(c1 == "Thinking");
        acc.feedDelta(json::parse(R"({
            "tool_calls": [{"index":0, "id":"x", "function":{"name":"search","arguments":"{}"}}]
        })"));
        auto c2 = acc.feedDelta(json::parse(R"({"content":"..."})"));
        expect(c2 == "...");
        acc.markDone();
        expect(acc.getContent() == "Thinking...");
        expect(acc.getToolCalls().size() == 1);
    };

    "reset"_test = [] {
        CLF::CLFCore::CLFStreamAccumulator acc;
        acc.feedDelta(json::parse(R"({"content":"old"})"));
        acc.markDone();
        acc.reset();
        expect(!acc.isFinished());
        expect(acc.getContent().empty());
        expect(acc.getToolCalls().empty());
        expect(acc.getFinishReason().empty());
    };

    "empty_delta"_test = [] {
        CLF::CLFCore::CLFStreamAccumulator acc;
        auto chunk = acc.feedDelta(json::parse("{}"));
        expect(chunk.empty());
        expect(acc.getContent().empty());
    };
};
