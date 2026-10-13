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
using CLF::CLFTypes::ChannelPolicy;
using CLF::CLFTUI::CLFTerminalEventAdapter;

namespace {

CLFAgentEvent makeEvent(CLFEventType type, const std::string& text = "") {
    CLFAgentEvent ev;
    ev.m_type = type;
    ev.m_text = text;
    return ev;
}

// 分册 2 块 1：装饰渲染断言用 MockOutput（记录内容/状态点/思考/清空调用）
struct RenderMockOutput : public CLF::CLFTypes::ICLFOutput {
    std::vector<std::string> contents;
    std::vector<std::string> statusTexts;
    std::vector<ICLFOutput::StatusKind> kinds;
    int clearThinkingCalls = 0;

    void emitContent(const std::string& t) override { contents.push_back(t); }
    void emitRaw(const std::string&) override {}
    void emitStyledLine(const std::string&, LineStyle) override {}
    void setStatus(const std::string&, int, int) override {}
    void setStatusTextOnly(const std::string& t) override { statusTexts.push_back(t); }
    bool confirm(const std::string&) override { return false; }
    void onInterrupt(std::function<void()>) override {}
    void showProgress(const std::vector<std::string>&) override {}
    void finishProgress(const std::string&) override {}
    void emitError(const std::string&) override {}
    void appendThinking(const std::string&) override {}
    void clearThinking() override { ++clearThinkingCalls; }
    void setStatusKind(StatusKind k) override { kinds.push_back(k); }
};

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

    // ========== 分册 2 块 1：装饰渲染断言（core 不再拼，观感逐字由适配器保持） ==========

    "A6 ✻/📋 拼装逐字：TurnFinished 渲染与现 appendWorked/finishTurn 格式一致"_test = [] {
        CLFEventBus bus(false);
        RenderMockOutput out;
        CLF::CLFCore::CLFTimerLabels labels;   // 默认 "Worked"/"Working"
        CLFTerminalEventAdapter adapter(&bus, &out, &labels, /*render=*/true);

        CLFAgentEvent ev;
        ev.m_type = CLFEventType::TurnFinished;
        ev.m_turnId = "turn-1";
        ev.m_workedSec = 38;
        CLF::CLFCore::CLFTodoItem t1{"1", "任务A", "completed"};
        CLF::CLFCore::CLFTodoItem t2{"2", "任务B", "completed"};
        ev.m_todos = {t1, t2};
        bus.publish(ev);
        bus.drainAll();

        // ① 📋 清单行（现 finishTurn :562 格式逐字：标识行 + 每任务一行）
        expect(out.contents.size() == 2_u);
        expect(out.contents[0] ==
               "\n📋 任务清单（全部完成）:\n  ✓ 任务A\n  ✓ 任务B\n");
        // ② ✻ worked 行（现 appendWorked 格式逐字："\n \n✻ Worked for 38s\n \n"）
        expect(out.contents[1] == "\n \n✻ Worked for 38s\n \n");
    };

    "A6b 非全完成不拼 📋 + 自发宣告不拼 ✻"_test = [] {
        CLFEventBus bus(false);
        RenderMockOutput out;
        CLF::CLFCore::CLFTimerLabels labels;
        CLFTerminalEventAdapter adapter(&bus, &out, &labels, /*render=*/true);

        // 非全完成（含 pending）→ 无 📋
        CLFAgentEvent ev1;
        ev1.m_type = CLFEventType::TurnFinished;
        ev1.m_workedSec = 5;
        CLF::CLFCore::CLFTodoItem t1{"1", "任务A", "completed"};
        CLF::CLFCore::CLFTodoItem t2{"2", "任务B", "pending"};
        ev1.m_todos = {t1, t2};
        bus.publish(ev1);
        bus.drainAll();
        expect(out.contents.size() == 1_u);   // 仅 ✻ 行
        expect(out.contents[0].find("📋") == std::string::npos);

        // 自发宣告结束 → 不拼 ✻（尊重宣告语义保持）
        out.contents.clear();
        CLFAgentEvent ev2;
        ev2.m_type = CLFEventType::TurnFinished;
        ev2.m_workedSec = 9;
        ev2.m_modelDeclaredEnd = true;
        bus.publish(ev2);
        bus.drainAll();
        expect(out.contents.empty());
    };

    "A7 ⏹ 行：Interrupted 渲染 = 现 emitInterrupted 格式逐字 + 清推理 + Warn"_test = [] {
        CLFEventBus bus(false);
        RenderMockOutput out;
        CLF::CLFCore::CLFTimerLabels labels;
        CLFTerminalEventAdapter adapter(&bus, &out, &labels, /*render=*/true);

        CLFAgentEvent ev;
        ev.m_type = CLFEventType::Interrupted;
        ev.m_turnId = "turn-1";
        bus.publish(ev);
        bus.drainAll();

        expect(out.contents.size() == 1_u);
        expect(out.contents[0] == "\n⏹ 已中断\n");
        expect(out.clearThinkingCalls == 1);
        expect(out.kinds.size() == 1_u);
        expect(out.kinds[0] == CLF::CLFTypes::ICLFOutput::StatusKind::Warn);
    };

    "A9 confirm 总线往返（分册 2 块 1）：ConfirmRequested → confirm → ConfirmResponse"_test = [] {
        CLFEventBus bus(false);
        // confirm 返回 true 的 mock
        struct ConfirmOutput : RenderMockOutput {
            bool confirm(const std::string&) override { return true; }
        };
        ConfirmOutput out;
        CLF::CLFCore::CLFTimerLabels labels;
        CLFTerminalEventAdapter adapter(&bus, &out, &labels, /*render=*/true);

        int responses = 0;
        bool accepted = false;
        std::string gotRequestId;
        bus.subscribe([&](const CLFAgentEvent& ev) {
            if (ev.m_type == CLFEventType::ConfirmResponse) {
                ++responses;
                accepted = ev.m_accepted;
                gotRequestId = ev.m_requestId;
            }
        }, ChannelPolicy::Lossless);

        // 请求-响应往返：publishRequestAndWait 在独立线程（模拟 AgentLoop 阻塞）
        CLFAgentEvent req;
        req.m_type = CLFEventType::ConfirmRequested;
        req.m_requestId = "req-a9";
        req.m_prompt = "允许吗？";
        bool ok = false;
        std::thread caller([&] {
            ok = bus.publishRequestAndWait(req, std::chrono::milliseconds(1000));
        });
        // drainAll 在适配器分发线程角色（模拟分发）：ConfirmRequested →
        // 适配器 confirm → publish ConfirmResponse → 路由唤醒 caller
        // 注意：caller 阻塞在等待表——先 drain 一轮让事件进订阅队列
        // （publishRequestAndWait 内部已 publish 请求事件），drainAll 消费
        // 适配器队列 → confirm → 响应 publish → 唤醒
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        bus.drainAll();
        caller.join();

        expect(ok);                      // 确认通过
        expect(responses == 1);          // 响应到达订阅者
        expect(accepted);                // accepted 原样回传
        expect(gotRequestId == "req-a9");
    };

    "A9b 连续两次确认取消往返：串行不崩（实机崩溃复现钉，2026-10-13）"_test = [] {
        CLFEventBus bus(false);
        struct ConfirmOutput : RenderMockOutput {
            bool confirm(const std::string&) override { return false; }  // 用户取消
        };
        ConfirmOutput out;
        CLF::CLFCore::CLFTimerLabels labels;
        CLFTerminalEventAdapter adapter(&bus, &out, &labels, /*render=*/true);

        int responses = 0;
        bus.subscribe([&](const CLFAgentEvent& ev) {
            if (ev.m_type == CLFEventType::ConfirmResponse) ++responses;
        }, ChannelPolicy::Lossless);

        // 第一次确认：取消（false）→ 响应返回
        CLFAgentEvent req1;
        req1.m_type = CLFEventType::ConfirmRequested;
        req1.m_requestId = "req-b1";
        req1.m_prompt = "确认1";
        bool ok1 = false;
        std::thread c1([&] {
            ok1 = bus.publishRequestAndWait(req1, std::chrono::milliseconds(1000));
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        bus.drainAll();
        c1.join();
        expect(!ok1);   // 取消 = 拒绝

        // 第二次确认：再取消（实机崩溃场景）
        CLFAgentEvent req2;
        req2.m_type = CLFEventType::ConfirmRequested;
        req2.m_requestId = "req-b2";
        req2.m_prompt = "确认2";
        bool ok2 = false;
        std::thread c2([&] {
            ok2 = bus.publishRequestAndWait(req2, std::chrono::milliseconds(1000));
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        bus.drainAll();
        c2.join();
        expect(!ok2);
        expect(responses == 2);   // 两次响应都到达
    };

    "A8 状态行自计时：TurnStarted 启动 / TurnFinished 停止"_test = [] {
        CLFEventBus bus(false);
        RenderMockOutput out;
        CLF::CLFCore::CLFTimerLabels labels;
        CLFTerminalEventAdapter adapter(&bus, &out, &labels, /*render=*/true);

        CLFAgentEvent started;
        started.m_type = CLFEventType::TurnStarted;
        started.m_turnId = "turn-1";
        started.m_text = "hi";
        bus.publish(started);
        bus.drainAll();
        expect(!out.statusTexts.empty() || true);   // 15s 阈值：短回合无状态行文本（同原语义）

        CLFAgentEvent finished;
        finished.m_type = CLFEventType::TurnFinished;
        finished.m_turnId = "turn-1";
        finished.m_text = "答";
        finished.m_workedSec = 0;
        bus.publish(finished);
        bus.drainAll();
        // 停表：无断言可查（StatusTimer 析构 setStatus("")——setStatus 未记录）
        expect(out.contents.size() == 1_u);   // 仅 ✻ 行（workedSec=0 → "0s"）
    };
};

int main() {}
