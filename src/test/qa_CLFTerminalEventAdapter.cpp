// qa_CLFTerminalEventAdapter.cpp — 总线适配器测试（事件化重构步骤 2，
// 分册 1 §4.1；R1 审查修正版：步骤 2 适配器只记录不渲染——本套件断言
// 记录保真；渲染映射保真（设计表 A1-A4 原版）在步骤 3 切轨时补测）

#include <boost/ut.hpp>

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFNetwork/CLFHttpClient.hpp"
#include "CLFTUI/CLFTerminalEventAdapter.hpp"
#include "CLFTypes/CLFEventBus.hpp"
#include "CLFTypes/CLFEvents.hpp"

using namespace boost::ut;
using CLF::CLFTypes::CLFAgentEvent;
using CLF::CLFTypes::CLFEventBus;
using CLF::CLFTypes::CLFEventType;
using CLF::CLFTypes::ICLFOutput;
using CLF::CLFTUI::CLFTerminalEventAdapter;

namespace {

CLFAgentEvent makeEvent(CLFEventType type, const std::string& text = "") {
    CLFAgentEvent ev;
    ev.m_type = type;
    ev.m_text = text;
    return ev;
}

// 渲染断言延后到步骤 3——步骤 2 传 nullptr output（只记录不渲染，R1）

// 双轨集成用最小 HTTP mock（同步路径单响应）
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

const boost::ut::suite<"CLFTerminalEventAdapter"> tests = [] {
    "A1 语义帧记录：完整收到且保序"_test = [] {
        CLFEventBus bus(false);
        CLFTerminalEventAdapter adapter(&bus, nullptr);
        bus.publish(makeEvent(CLFEventType::TurnStarted, "hi"));
        bus.publish(makeEvent(CLFEventType::StatusChanged));
        bus.publish(makeEvent(CLFEventType::ToolFinished));
        bus.drainAll();

        expect(adapter.semanticCount() == 3_u);
        const auto& got = adapter.received();
        expect(got.size() == 3_u);
        expect(got[0].m_type == CLFEventType::TurnStarted);
        expect(got[1].m_type == CLFEventType::StatusChanged);
        expect(got[2].m_type == CLFEventType::ToolFinished);
        expect(got[0].m_text == "hi");   // 载荷保真
        expect(got[0].m_seq < got[1].m_seq && got[1].m_seq < got[2].m_seq);
    };

    "A2 增量帧记录：ContentDelta 计数与载荷"_test = [] {
        CLFEventBus bus(false);
        CLFTerminalEventAdapter adapter(&bus, nullptr);
        bus.publish(makeEvent(CLFEventType::ContentDelta, "你"));
        bus.publish(makeEvent(CLFEventType::ContentDelta, "好"));
        bus.publish(makeEvent(CLFEventType::ReasoningDelta, "思考中"));
        bus.drainAll();

        expect(adapter.deltaCount() == 3_u);
        expect(adapter.semanticCount() == 0_u);
        const auto& got = adapter.received();
        expect(got[0].m_text == "你");
        expect(got[1].m_text == "好");
    };

    "A3 增量通道 Coalescing：溢出丢旧留新（适配器视角）"_test = [] {
        CLFEventBus bus(false);
        CLFTerminalEventAdapter adapter(&bus, nullptr);
        // 默认增量容量 256——发布 300 条，适配器应收到最新 256 条
        for (int i = 0; i < 300; ++i)
            bus.publish(makeEvent(CLFEventType::ContentDelta));
        bus.drainAll();
        expect(adapter.deltaCount() == 256_u);
        // 最新一条的 seq = 300（丢的只有最旧的）
        expect(adapter.received().back().m_seq == 300_u);
    };

    "A4 seq 连续：总线分配序在记录中按通道严格递增"_test = [] {
        CLFEventBus bus(false);
        CLFTerminalEventAdapter adapter(&bus, nullptr);
        bus.publish(makeEvent(CLFEventType::TurnStarted));
        bus.publish(makeEvent(CLFEventType::ContentDelta, "a"));
        bus.publish(makeEvent(CLFEventType::ContentDelta, "b"));
        bus.publish(makeEvent(CLFEventType::TurnFinished));
        bus.drainAll();
        const auto& got = adapter.received();
        expect(got.size() == 4_u);
        // 两通道队列独立——跨通道交错序无保证；同通道内 seq 严格递增
        // （seq 全局分配含增量帧穿插，故非连续 +1，只断言单调递增）
        auto strictIn = [&](bool delta) {
            uint64_t prev = 0;
            bool first = true;
            for (const auto& ev : got) {
                if (CLF::CLFTypes::isDeltaEvent(ev.m_type) != delta) continue;
                if (!first && ev.m_seq <= prev) return false;
                prev = ev.m_seq;
                first = false;
            }
            return true;
        };
        expect(strictIn(false));  // 语义帧：1 → 4
        expect(strictIn(true));   // 增量帧：2 → 3
    };

    "A5 双轨集成：AgentLoop runTurn → 总线事件序列 + turnId 传播"_test = [] {
        CLFEventBus bus(false);
        CLFTerminalEventAdapter adapter(&bus, nullptr);

        CLF::CLFCore::CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = false;   // 同步路径（MiniHttp 单响应）
        auto http = std::make_shared<MiniHttp>();
        CLF::CLFCore::CLFAgentLoop agent(config, http);
        agent.setEventBus(&bus);

        std::string result = agent.runTurn("你好");
        expect(result.find("好") != std::string::npos);   // 直调路径行为不变（双轨）
        bus.drainAll();

        const auto& got = adapter.received();
        // 同步路径事件全集：TurnStarted / ThinkingCleared（增量通道）/
        // StatusChanged / TurnFinished——通道队列独立，交错序无保证，按集合断言
        expect(got.size() == 4_u);
        auto findOf = [&](CLFEventType t) {
            for (const auto& ev : got)
                if (ev.m_type == t) return &ev;
            return static_cast<const CLFAgentEvent*>(nullptr);
        };
        const auto* started = findOf(CLFEventType::TurnStarted);
        const auto* cleared = findOf(CLFEventType::ThinkingCleared);
        const auto* status = findOf(CLFEventType::StatusChanged);
        const auto* finished = findOf(CLFEventType::TurnFinished);
        expect(started && cleared && status && finished);
        if (started && cleared && status && finished) {
            // 载荷保真
            expect(started->m_text == "你好");
            expect(status->m_statusKind ==
                   static_cast<int>(CLF::CLFTypes::ICLFOutput::StatusKind::Running));
            expect(!finished->m_capReached);   // 自然停非触顶
            // turnId 传播：同回合事件共用稳定 id（跨通道）
            expect(started->m_turnId == cleared->m_turnId);
            expect(started->m_turnId == status->m_turnId);
            expect(started->m_turnId == finished->m_turnId);
            expect(!started->m_turnId.empty());
        }
    };
};

int main() {}
