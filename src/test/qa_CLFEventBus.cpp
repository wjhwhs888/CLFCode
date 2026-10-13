// qa_CLFEventBus.cpp — 事件总线机制全集测试（事件化重构步骤 2，分册 1 §4.1）
// 覆盖：B1-B10（订阅接收/保序/背压/慢消费者/异常隔离/历史环/请求-响应）
//
// ⚠ qa 线程纪律（审查修正 R2，2026-10-12）：boost::ut 静态初始化期执行——
// 总线测试默认 startDispatcher=false（手动 publish + drainAll，无后台线程）；
// 必须真实线程的用例（B3 阻塞生产者 / B8-B10 请求-响应）用 std::thread + join
// 短等待——join 返回即线程终止，静态期零残留。

#include <boost/ut.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "CLFTypes/CLFEventBus.hpp"

using namespace boost::ut;
using CLF::CLFTypes::CLFAgentEvent;
using CLF::CLFTypes::CLFEventBus;
using CLF::CLFTypes::CLFEventType;
using CLF::CLFTypes::CLFSubscription;
using CLF::CLFTypes::ChannelPolicy;

namespace {

CLFAgentEvent makeEvent(CLFEventType type, const std::string& text = "") {
    CLFAgentEvent ev;
    ev.m_type = type;
    ev.m_text = text;
    return ev;
}

} // anonymous namespace

const boost::ut::suite<"CLFEventBus"> tests = [] {
    "B1 订阅接收：单/多订阅者各自收全"_test = [] {
        {
            CLFEventBus bus(false);   // startDispatcher=false——qa 纪律
            int count = 0;
            bus.subscribe([&](const CLFAgentEvent&) { ++count; }, ChannelPolicy::Lossless);
            bus.publish(makeEvent(CLFEventType::TurnStarted, "a"));
            bus.publish(makeEvent(CLFEventType::TurnStarted, "b"));
            expect(bus.drainAll() == 2_u);
            expect(count == 2);
        }
        {
            CLFEventBus bus(false);
            int a = 0, b = 0;
            bus.subscribe([&](const CLFAgentEvent&) { ++a; }, ChannelPolicy::Lossless);
            bus.subscribe([&](const CLFAgentEvent&) { ++b; }, ChannelPolicy::Lossless);
            bus.publish(makeEvent(CLFEventType::TurnStarted, "x"));
            expect(bus.drainAll() == 2_u);   // 两订阅者各一条
            expect(a == 1 && b == 1);
        }
    };

    "B2 保序：1000 事件 seq 严格递增"_test = [] {
        CLFEventBus bus(false);
        std::vector<uint64_t> seqs;
        bus.subscribe([&](const CLFAgentEvent& ev) { seqs.push_back(ev.m_seq); },
                      ChannelPolicy::Lossless);
        for (int i = 0; i < 1000; ++i)
            bus.publish(makeEvent(CLFEventType::TurnStarted));
        expect(bus.drainAll() == 1000_u);
        expect(seqs.size() == 1000_u);
        bool strictly = true;
        for (size_t i = 1; i < seqs.size(); ++i)
            if (seqs[i] != seqs[i - 1] + 1) { strictly = false; break; }
        expect(strictly);   // 单生产者全局严格递增（含多条订阅交错）
    };

    "B3 Lossless 满阻塞生产者：消费后恢复"_test = [] {
        CLFEventBus bus(false);
        bus.subscribe([](const CLFAgentEvent&) {}, ChannelPolicy::Lossless,
                      /*capacity=*/1);
        bus.publish(makeEvent(CLFEventType::TurnStarted, "1"));  // 填满（1/1）

        std::atomic<bool> published{false};
        std::thread producer([&] {
            bus.publish(makeEvent(CLFEventType::TurnStarted, "2"));  // 阻塞在此
            published = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        expect(!published.load());          // 队列满 → 生产者阻塞中
        expect(bus.drainAll() == 1_u);      // 消费 1 条 → 唤醒生产者
        producer.join();                    // join 返回即线程终止（纪律 ✓）
        expect(published.load());
        expect(bus.drainAll() == 1_u);      // 阻塞恢复后的事件已入队
    };

    "B4 Coalescing 满丢旧留新"_test = [] {
        CLFEventBus bus(false);
        std::vector<std::string> got;
        bus.subscribe([&](const CLFAgentEvent& ev) { got.push_back(ev.m_text); },
                      ChannelPolicy::Coalescing, /*capacity=*/2);
        bus.publish(makeEvent(CLFEventType::ContentDelta, "a"));
        bus.publish(makeEvent(CLFEventType::ContentDelta, "b"));
        bus.publish(makeEvent(CLFEventType::ContentDelta, "c"));  // 丢最旧 "a"
        expect(bus.drainAll() == 2_u);
        expect(got.size() == 2_u);
        expect(got[0] == "b" && got[1] == "c");  // 保留最新
    };

    "B5 慢消费者不阻塞其他订阅者"_test = [] {
        CLFEventBus bus(false);
        int slow = 0, fast = 0;
        // 慢订阅者：容量 1 的 Lossless；快订阅者：容量 8
        bus.subscribe([&](const CLFAgentEvent&) { ++slow; }, ChannelPolicy::Lossless, 1);
        bus.subscribe([&](const CLFAgentEvent&) { ++fast; }, ChannelPolicy::Lossless, 8);
        // 3 条发布：慢订阅者在第 2 条起阻塞（容量 1）——发布仍推进（fast 收全）
        std::atomic<bool> done{false};
        std::thread producer([&] {
            for (int i = 0; i < 3; ++i)
                bus.publish(makeEvent(CLFEventType::TurnStarted));
            done = true;
        });
        // 交替消费让生产者持续推进（收敛式轮询：drain 至生产者完成——
        // 固定轮数会因线程启动调度竞态提前耗尽而挂死）
        while (!done.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            bus.drainAll();
        }
        producer.join();
        bus.drainAll();
        expect(fast == 3);   // 快订阅者全收
        expect(slow == 3);   // 慢订阅者经阻塞-唤醒也全收（不丢语义）
    };

    "B6 订阅者异常隔离：handler 抛异常不影响总线与其他订阅者"_test = [] {
        CLFEventBus bus(false);
        int healthy = 0;
        bus.subscribe([](const CLFAgentEvent&) { throw std::runtime_error("boom"); },
                      ChannelPolicy::Lossless);
        bus.subscribe([&](const CLFAgentEvent&) { ++healthy; }, ChannelPolicy::Lossless);
        bus.publish(makeEvent(CLFEventType::TurnStarted, "x"));
        expect(bus.drainAll() == 2_u);  // 两条都分发，异常被吞
        expect(healthy == 1);
    };

    "B7 事件历史环：有界、满丢最旧"_test = [] {
        CLFEventBus bus(false);
        for (int i = 0; i < 4098; ++i)   // 容量 4096 + 2
            bus.publish(makeEvent(CLFEventType::TurnStarted));
        auto hist = bus.historySnapshot();
        expect(hist.size() == 4096_u);
        expect(hist.front().m_seq > 1);         // 最旧已被丢
        expect(hist.back().m_seq == 4098_u);    // 最新保留
        // seq 连续（历史环内无空洞）
        bool cont = true;
        for (size_t i = 1; i < hist.size(); ++i)
            if (hist[i].m_seq != hist[i - 1].m_seq + 1) { cont = false; break; }
        expect(cont);
    };

    "B8 publishRequestAndWait 配对成功：accepted 原样回传"_test = [] {
        CLFEventBus bus(false);
        // 应答线程：drain 请求事件 → 回 ConfirmResponse（模拟 UI 侧）
        std::thread responder([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            CLFAgentEvent reply;
            reply.m_type = CLFEventType::ConfirmResponse;
            reply.m_requestId = "req-1";
            reply.m_accepted = true;
            bus.publish(reply);
        });
        CLFAgentEvent req;
        req.m_type = CLFEventType::ConfirmRequested;
        req.m_requestId = "req-1";
        req.m_prompt = "允许吗？";
        bool ok = bus.publishRequestAndWait(req, std::chrono::milliseconds(1000));
        responder.join();
        expect(ok);   // 收到 accepted=true
    };

    "B9 超时默认拒绝：无应答 → false（60s 语义的测试缩时版）"_test = [] {
        CLFEventBus bus(false);
        CLFAgentEvent req;
        req.m_type = CLFEventType::ConfirmRequested;
        req.m_requestId = "req-timeout";
        bool ok = bus.publishRequestAndWait(req, std::chrono::milliseconds(100));
        expect(!ok);  // 超时 = 拒绝
    };

    "B10 错误 requestId 不影响等待表：应答不匹配 → 照常超时；匹配不受干扰"_test = [] {
        CLFEventBus bus(false);
        int stray = 0;
        bus.subscribe([&](const CLFAgentEvent&) { ++stray; }, ChannelPolicy::Lossless);
        // 应答线程发错误 id → 等待者不应被唤醒；随后正确 id → 正常唤醒
        std::thread responder([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            CLFAgentEvent wrong;
            wrong.m_type = CLFEventType::ConfirmResponse;
            wrong.m_requestId = "req-wrong";
            wrong.m_accepted = true;
            bus.publish(wrong);                       // 错误 id：无匹配
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            CLFAgentEvent right;
            right.m_type = CLFEventType::ConfirmResponse;
            right.m_requestId = "req-right";
            right.m_accepted = true;
            bus.publish(right);                       // 正确 id：唤醒
        });
        CLFAgentEvent req;
        req.m_type = CLFEventType::ConfirmRequested;
        req.m_requestId = "req-right";
        bool ok = bus.publishRequestAndWait(req, std::chrono::milliseconds(1000));
        responder.join();
        expect(ok);
        // 三条全部照常入分发队列（未匹配等待表不吞事件）：
        // ConfirmRequested（请求本身）+ 错误 id 应答 + 正确 id 应答
        expect(bus.drainAll() == 3_u);
        expect(stray == 3);
    };

    "B11 退订：幂等、后续 publish 不再入队"_test = [] {
        CLFEventBus bus(false);
        int got = 0;
        CLFSubscription sub = bus.subscribe(
            [&](const CLFAgentEvent&) { ++got; }, ChannelPolicy::Lossless);
        bus.publish(makeEvent(CLFEventType::TurnStarted, "1"));
        sub.unsubscribe();
        sub.unsubscribe();   // 幂等
        bus.publish(makeEvent(CLFEventType::TurnStarted, "2"));
        expect(bus.drainAll() == 1_u);   // 仅退订前那条
        expect(got == 1);
    };
};

int main() {}
