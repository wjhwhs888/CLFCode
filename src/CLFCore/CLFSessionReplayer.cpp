// CLFSessionReplayer.cpp — 会话恢复事件重放器实现（分册 2 §3.2）

#include "CLFCore/CLFSessionReplayer.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFMessageCodec.hpp"
#include "CLFTypes/CLFEventBus.hpp"
#include "CLFTypes/CLFEvents.hpp"

namespace CLF::CLFCore {

using CLF::CLFTypes::CLFAgentEvent;
using CLF::CLFTypes::CLFEventBus;
using CLF::CLFTypes::CLFEventType;

namespace {

// 装饰剥离（设计 2.4 硬约束）：只剥程序已知精确格式，模型内容合法 ✻/📋 不动。
// ① worked 行：格式 "✻ <label> for <dur>s"（现 appendWorked 拼法）——
//    行 trim 后以 "✻ " 开头 && 含 " for " && 以 's' 结尾（时长格式）
// ② 📋 收尾段：标识行 "📋 任务清单（全部完成）:" + 后续 "  ✓ " 前缀行
// 剥离后相邻空行合并保留单空行（文本结构不塌）
std::string stripDecorations(const std::string& content) {
    std::vector<std::string> lines;
    {
        std::istringstream iss(content);
        std::string line;
        while (std::getline(iss, line)) lines.push_back(line);
    }
    std::vector<bool> drop(lines.size(), false);
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        // worked 行（✻ 为 3 字节 UTF-8——按完整前缀串长度比较，防劈半误判）
        const auto ws = l.find_first_not_of(' ');
        if (ws != std::string::npos
            && l.compare(ws, std::string("✻ ").size(), "✻ ") == 0
            && l.find(" for ") != std::string::npos
            && !l.empty() && l.back() == 's') {
            drop[i] = true;
            continue;
        }
        // 📋 段：标识行 + 后续 ✓ 行
        if (l.find("📋 任务清单（全部完成）:") != std::string::npos) {
            drop[i] = true;
            for (size_t j = i + 1; j < lines.size(); ++j) {
                if (lines[j].find("  ✓ ") == 0) {
                    drop[j] = true;
                } else {
                    break;
                }
            }
        }
    }
    std::string out;
    bool prevEmpty = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (drop[i]) continue;
        const bool empty = lines[i].empty();
        if (empty && prevEmpty) continue;   // 连续空行合并
        if (empty && out.empty()) continue; // 首行空行跳过
        out += (out.empty() ? "" : "\n") + lines[i];
        prevEmpty = empty;
    }
    // 尾部空行去净
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

// 临时 turnId（老文件无此字段——仅显示用；设计 2.4）
std::string tempTurnId(size_t idx) {
    return "resume-turn-" + std::to_string(idx);
}

} // anonymous namespace

size_t CLFSessionReplayer::replay(const std::string& jsonlPath,
                                  CLF::CLFTypes::CLFEventBus* bus) {
    if (!bus) return 0;
    // 全量读入（jsonl 会话文件量级 < 10MB；两遍扫描——event_log 集合先行）
    std::vector<nlohmann::json> lines;
    {
        std::ifstream f(std::filesystem::u8path(jsonlPath));
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            try {
                lines.push_back(nlohmann::json::parse(line));
            } catch (...) {
                CLFLogger::instance().warn("[Replay] skip bad line: " + jsonlPath);
            }
        }
    }
    if (lines.empty()) return 0;

    // 第一遍：event_log 的 (turnId, callId) 集合——新文件工具痕迹权威
    std::set<std::pair<std::string, std::string>> eventLogKeys;
    for (const auto& l : lines) {
        if (l.value("type", "") == JsonlType::kEventLog) {
            eventLogKeys.emplace(l.value("turnId", ""), l.value("callId", ""));
        }
    }

    // 第二遍：按行序重放（文件行序 = seq 序）
    size_t count = 0;
    size_t turnIdx = 0;
    auto publish = [&](CLFAgentEvent ev) {
        bus->publish(std::move(ev));
        ++count;
    };

    for (const auto& l : lines) {
        const std::string type = l.value("type", "");
        if (type == JsonlType::kHeader) {
            std::string title, startedAt, sessionId, model;
            std::vector<std::string> skills;
            CLFMessageCodec::parseHeaderLine(l, &title, &startedAt, &sessionId,
                                             &model, &skills);
            CLFAgentEvent ev;
            ev.m_type = CLFEventType::HistoryHeader;
            ev.m_model = model;
            ev.m_startedAt = startedAt;
            ev.m_skills = std::move(skills);
            ev.m_text = title;
            publish(std::move(ev));
        } else if (type == JsonlType::kTurn) {
            std::vector<CLFMessage> msgs;
            CLFMessageCodec::parseTurnLine(l, msgs);
            const std::string turnId = l.contains("turnId")
                ? l["turnId"].get<std::string>() : tempTurnId(++turnIdx);
            for (const auto& m : msgs) {
                CLFAgentEvent ev;
                ev.m_turnId = turnId;
                if (m.m_role == "user") {
                    ev.m_type = CLFEventType::HistoryUser;
                    ev.m_text = m.m_content;
                    publish(std::move(ev));
                } else if (m.m_role == "assistant") {
                    ev.m_type = CLFEventType::HistoryAssistant;
                    ev.m_text = stripDecorations(m.m_content);
                    publish(std::move(ev));
                } else if (m.m_role == "tool") {
                    // 新文件：tool 消息已由 event_log 行权威重放（跳过防重复）；
                    // 老文件无 event_log：此处兜底（差异收敛在重放器——设计 3.2）
                    if (eventLogKeys.count({turnId, m.m_toolCallId}) == 0) {
                        ev.m_type = CLFEventType::HistoryToolTrace;
                        ev.m_callId = m.m_toolCallId;
                        ev.m_name = m.m_name;
                        ev.m_ok = true;   // 兜底显示不误标失败（信息在 summary 文本）
                        ev.m_summary = m.m_content;
                        publish(std::move(ev));
                    }
                }
                // system 等其余角色跳过
            }
        } else if (type == JsonlType::kEventLog) {
            CLFAgentEvent ev;
            ev.m_type = CLFEventType::HistoryToolTrace;
            ev.m_turnId = l.value("turnId", "");
            ev.m_callId = l.value("callId", "");
            ev.m_name = l.value("name", "");
            ev.m_ok = l.value("ok", true);
            ev.m_summary = l.value("summary", "");
            publish(std::move(ev));
        } else if (type == JsonlType::kSummary) {
            CLFSessionSummary summary;
            CLFMessageCodec::parseSummaryLine(l, summary);
            if (summary.m_valid) {
                CLFAgentEvent ev;
                ev.m_type = CLFEventType::HistorySummary;
                ev.m_text = summary.m_summary;
                publish(std::move(ev));
            }
        }
        // todo_snapshot / complete / 未知类型：跳过（设计 3.3——清单不重建，
        // 恢复后面板经 setTodos 显示最后快照；未知行静默跳过安全网）
    }

    // 回显结束
    {
        CLFAgentEvent ev;
        ev.m_type = CLFEventType::ResumeDone;
        publish(std::move(ev));
    }
    return count;
}

} // namespace CLF::CLFCore
