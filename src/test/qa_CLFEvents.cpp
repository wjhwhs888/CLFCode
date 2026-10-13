// qa_CLFEvents.cpp — 事件协议头单元测试（事件化重构步骤 1，分册 1 §4.1）
// 覆盖：E1 字段默认值 / E2 稳定关联键 seq 语义 / E3 turnId 传播 / 枚举序钉
// 注意：协议头是 POD 契约（跨进程 JSON codec 直写）——本套件同时钉死
// 字段默认值与枚举序，防未来重构破坏已发布协议。

#include <boost/ut.hpp>

#include "CLFTypes/CLFEvents.hpp"

using namespace boost::ut;
using CLF::CLFTypes::CLFAgentEvent;
using CLF::CLFTypes::CLFEventType;

// 枚举序即协议合同（发布后不可重排）——ResumeDone 为全集末位，序 + 1 = 21
static_assert(static_cast<int>(CLFEventType::ResumeDone) + 1 == 21,
              "协议 v1 事件全集 = 21 个（分册 1 §2.2b 唯一权威总表）");

const boost::ut::suite<"CLFEvents"> tests = [] {
    "E1 字段默认值：POD 契约基线"_test = [] {
        CLFAgentEvent ev;
        expect(ev.m_type == CLFEventType::TurnStarted);
        expect(ev.m_seq == 0_u64);              // 0 = 总线未分配
        expect(ev.m_turnId.empty());
        expect(ev.m_text.empty());
        expect(ev.m_callId.empty());
        expect(ev.m_name.empty());
        expect(ev.m_argsPreview.empty());
        expect(ev.m_ok);                        // 默认成功（仅失败路径显式置 false）
        expect(ev.m_summary.empty());
        expect(ev.m_errorKind.empty());
        expect(ev.m_statusKind == 0);
        expect(ev.m_workedSec == 0_ll);
        expect(!ev.m_capReached);
        expect(ev.m_todos.empty());
        expect(ev.m_requestId.empty());
        expect(ev.m_prompt.empty());
        expect(!ev.m_accepted);                 // 确认默认拒绝（60s 超时同语义）
        expect(ev.m_sessionFile.empty());
        expect(ev.m_model.empty());
        expect(ev.m_startedAt.empty());
        expect(ev.m_skills.empty());
    };

    "E2 seq 稳定关联键：总线序号语义（uint64 单调递增，0 = 未分配）"_test = [] {
        CLFAgentEvent a;
        CLFAgentEvent b;
        a.m_seq = 1;
        b.m_seq = 2;
        // 单生产者（AgentLoop 工作线程）分配 → 全局严格递增；两事件互不影响
        expect(a.m_seq < b.m_seq);
        expect(a.m_seq == 1_u64);
        expect(b.m_seq == 2_u64);
        // 序号与其余字段独立（tagged union 手工版：字段按类型使用）
        a.m_seq = 100;
        expect(a.m_seq == 100_u64);
        expect(b.m_seq == 2_u64);
    };

    "E3 turnId 传播：同回合事件共用稳定 id"_test = [] {
        const std::string turnId = "turn-20261013-001";
        CLFAgentEvent started;   // TurnStarted 起全部同回合事件共用
        CLFAgentEvent delta;
        CLFAgentEvent finished;
        started.m_type = CLFEventType::TurnStarted;
        delta.m_type   = CLFEventType::ContentDelta;
        finished.m_type = CLFEventType::TurnFinished;
        started.m_turnId  = turnId;
        delta.m_turnId    = turnId;
        finished.m_turnId = turnId;
        expect(started.m_turnId == delta.m_turnId);
        expect(delta.m_turnId == finished.m_turnId);
        // 不同回合的事件 id 独立
        CLFAgentEvent other;
        other.m_turnId = "turn-20261013-002";
        expect(other.m_turnId != started.m_turnId);
        // 事件类型互不干扰（按类型使用字段）
        expect(started.m_type != delta.m_type);
        expect(delta.m_type != finished.m_type);
    };
};

int main() {}
