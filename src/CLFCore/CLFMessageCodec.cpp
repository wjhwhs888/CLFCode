// CLFMessageCodec.cpp — 消息 JSON 编解码器实现

#include "CLFCore/CLFMessageCodec.hpp"

#include <nlohmann/json.hpp>

namespace CLF::CLFCore {

namespace {

// ============================================================================
// 字段级序列化/反序列化 helpers（覆盖式 serialize/parseFull 与 jsonl 行函数共用）
// ============================================================================

nlohmann::json messagesToJson(const std::vector<CLFMessage>& messages) {
    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& msg : messages) {
        nlohmann::json m;
        m["role"]    = msg.m_role;
        m["content"] = msg.m_content;

        if (!msg.m_toolCalls.empty()) {
            nlohmann::json tcs = nlohmann::json::array();
            for (const auto& tc : msg.m_toolCalls) {
                nlohmann::json tcJson;
                tcJson["id"]        = tc.m_id;
                tcJson["name"]      = tc.m_name;
                tcJson["arguments"] = tc.m_arguments;
                tcs.push_back(std::move(tcJson));
            }
            m["tool_calls"] = std::move(tcs);
        }
        if (!msg.m_toolCallId.empty()) m["tool_call_id"] = msg.m_toolCallId;
        if (!msg.m_name.empty())       m["name"] = msg.m_name;
        msgs.push_back(std::move(m));
    }
    return msgs;
}

std::vector<CLFMessage> messagesFromJson(const nlohmann::json& arr) {
    std::vector<CLFMessage> result;
    if (!arr.is_array()) return result;
    for (const auto& m : arr) {
        if (!m.is_object()) continue;
        if (!m.contains("role") || !m.contains("content")) continue;

        CLFMessage msg;
        msg.m_role    = m["role"].get<std::string>();
        msg.m_content = m["content"].get<std::string>();
        if (m.contains("tool_call_id") && m["tool_call_id"].is_string()) {
            msg.m_toolCallId = m["tool_call_id"].get<std::string>();
        }
        if (m.contains("name") && m["name"].is_string()) {
            msg.m_name = m["name"].get<std::string>();
        }
        if (m.contains("tool_calls") && m["tool_calls"].is_array()) {
            for (const auto& tc : m["tool_calls"]) {
                CLFToolCall call;
                call.m_id        = tc.value("id", "");
                call.m_name      = tc.value("name", "");
                call.m_arguments = tc.value("arguments", "");
                msg.m_toolCalls.push_back(std::move(call));
            }
        }
        result.push_back(std::move(msg));
    }
    return result;
}

nlohmann::json todosToJson(const std::vector<CLFTodoItem>& todos) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : todos) {
        arr.push_back({{"id", t.m_id}, {"content", t.m_content}, {"status", t.m_status}});
    }
    return arr;
}

// 缺 content 的待办项丢弃（与 parseFull 既有语义一致）
std::vector<CLFTodoItem> todosFromJson(const nlohmann::json& arr) {
    std::vector<CLFTodoItem> out;
    if (!arr.is_array()) return out;
    for (const auto& t : arr) {
        if (!t.is_object()) continue;
        CLFTodoItem item;
        item.m_id      = t.value("id", "");
        item.m_content = t.value("content", "");
        item.m_status  = t.value("status", "pending");
        if (!item.m_content.empty()) out.push_back(std::move(item));
    }
    return out;
}

// summary 无效（m_valid=false）时返回 null，调用方据此决定是否写字段
nlohmann::json summaryToJson(const CLFSessionSummary& summary) {
    if (!summary.m_valid) return nlohmann::json();
    nlohmann::json sum;
    sum["text"]         = summary.m_summary;
    sum["method"]       = summary.m_method;
    sum["current_plan"] = summary.m_currentPlan;
    if (!summary.m_keyDecisions.empty()) {
        nlohmann::json kd = nlohmann::json::array();
        for (const auto& d : summary.m_keyDecisions) kd.push_back(d);
        sum["key_decisions"] = std::move(kd);
    }
    if (!summary.m_filesModified.empty()) {
        nlohmann::json fm = nlohmann::json::array();
        for (const auto& f : summary.m_filesModified) fm.push_back(f);
        sum["files_modified"] = std::move(fm);
    }
    if (!summary.m_pendingTasks.empty()) {
        nlohmann::json pt = nlohmann::json::array();
        for (const auto& t : summary.m_pendingTasks) pt.push_back(t);
        sum["pending_tasks"] = std::move(pt);
    }
    return sum;
}

void summaryFromJson(const nlohmann::json& obj, CLFSessionSummary& out) {
    out = {};
    if (!obj.is_object()) return;
    out.m_summary     = obj.value("text", "");
    out.m_currentPlan = obj.value("current_plan", "");
    out.m_method      = obj.value("method", "rule_based");
    out.m_valid       = !out.m_summary.empty();
    if (obj.contains("key_decisions") && obj["key_decisions"].is_array()) {
        for (const auto& kd : obj["key_decisions"]) {
            if (kd.is_string()) out.m_keyDecisions.push_back(kd.get<std::string>());
        }
    }
    if (obj.contains("files_modified") && obj["files_modified"].is_array()) {
        for (const auto& fm : obj["files_modified"]) {
            if (fm.is_string()) out.m_filesModified.push_back(fm.get<std::string>());
        }
    }
    if (obj.contains("pending_tasks") && obj["pending_tasks"].is_array()) {
        for (const auto& pt : obj["pending_tasks"]) {
            if (pt.is_string()) out.m_pendingTasks.push_back(pt.get<std::string>());
        }
    }
}

// 覆盖式整文件：dump 失败降级为 replace（保证永不返回空串）
std::string dumpOrReplace(const nlohmann::json& data) {
    try {
        return data.dump();
    } catch (const nlohmann::json::exception&) {
        return data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
}

// jsonl 行：dump 失败返回空串（调用方 warn + 跳过该行，绝不写入坏行）
std::string dumpLine(const nlohmann::json& line) {
    try {
        return line.dump();
    } catch (const nlohmann::json::exception&) {
        return std::string();
    }
}

} // anonymous namespace

std::string CLFMessageCodec::serialize(const std::vector<CLFMessage>& messages,
                                        const std::string& savedAt,
                                        const std::string& title,
                                        const std::vector<std::string>& skills,
                                        const CLFSessionSummary* summary,
                                        const std::vector<CLFTodoItem>& todos) {
    nlohmann::json data;
    data["version"]      = 1;
    data["messageCount"] = static_cast<int>(messages.size());
    if (!savedAt.empty()) data["saved_at"] = savedAt;
    if (!title.empty())   data["title"]    = title;

    // skills 数组
    if (!skills.empty()) {
        nlohmann::json sk = nlohmann::json::array();
        for (const auto& s : skills) sk.push_back(s);
        data["skills"] = std::move(sk);
    }

    // todos 数组（S2-6，照 skills 模式：空则不写字段。version 维持 1——
    // 旧版读新文件会忽略该字段，新版读旧文件视为空清单，双向兼容）
    if (!todos.empty()) {
        data["todos"] = todosToJson(todos);
    }

    // summary 对象
    if (summary && summary->m_valid) {
        data["summary"] = summaryToJson(*summary);
    }

    data["messages"] = messagesToJson(messages);
    return dumpOrReplace(data);
}

std::vector<CLFMessage> CLFMessageCodec::parse(const std::string& jsonData) {
    return parseFull(jsonData);
}

std::vector<CLFMessage> CLFMessageCodec::parseFull(const std::string& jsonData,
                                                     int* outVersion,
                                                     std::string* outSavedAt,
                                                     std::string* outTitle,
                                                     std::vector<std::string>* outSkills,
                                                     CLFSessionSummary* outSummary,
                                                     std::vector<CLFTodoItem>* outTodos) {
    std::vector<CLFMessage> result;
    try {
        nlohmann::json data = nlohmann::json::parse(jsonData);
        if (!data.contains("messages") || !data["messages"].is_array()) {
            return result;
        }

        if (outVersion)  *outVersion  = data.value("version", 0);
        if (outSavedAt)  *outSavedAt  = data.value("saved_at", "");
        if (outTitle)    *outTitle    = data.value("title", "");

        // skills 数组
        if (outSkills) {
            outSkills->clear();
            if (data.contains("skills") && data["skills"].is_array()) {
                for (const auto& s : data["skills"]) {
                    if (s.is_string()) outSkills->push_back(s.get<std::string>());
                }
            }
        }

        // todos 数组（S2-6）——字段缺失即视为空清单，保证旧会话文件照常加载
        if (outTodos) {
            *outTodos = todosFromJson(data.value("todos", nlohmann::json::array()));
        }

        // summary 对象
        if (outSummary) {
            summaryFromJson(data.value("summary", nlohmann::json()), *outSummary);
        }

        result = messagesFromJson(data["messages"]);
    } catch (const nlohmann::json::exception&) {}
    return result;
}

// ============================================================================
// jsonl 行编解码（设计-会话追加式保存.jsonl.md §3.2，2026-09-02）
// ============================================================================

std::string CLFMessageCodec::serializeHeaderLine(const std::string& title,
                                                 const std::string& startedAt,
                                                 const std::string& sessionId,
                                                 const std::string& model,
                                                 const std::vector<std::string>& skills) {
    nlohmann::json line;
    line["type"]       = "header";
    line["version"]    = 1;
    line["title"]      = title;
    line["started_at"] = startedAt;
    line["session_id"] = sessionId;
    line["model"]      = model;
    // skills 为会话级状态（S2-6 起随会话持久化）——header 是唯一的会话级元数据行，语义归位
    if (!skills.empty()) {
        nlohmann::json sk = nlohmann::json::array();
        for (const auto& s : skills) sk.push_back(s);
        line["skills"] = std::move(sk);
    }
    return dumpLine(line);
}

std::string CLFMessageCodec::serializeTurnLine(const std::vector<CLFMessage>& messages,
                                               const std::string& ts,
                                               const std::vector<CLFTodoItem>* todos) {
    nlohmann::json line;
    line["type"]     = "turn";
    line["ts"]       = ts;
    line["messages"] = messagesToJson(messages);
    // 仅当本轮操作过 todo_write（m_todoDirty）才带快照；否则省略字段
    if (todos && !todos->empty()) {
        line["todos"] = todosToJson(*todos);
    }
    return dumpLine(line);
}

std::string CLFMessageCodec::serializeTodoSnapshot(const std::vector<CLFTodoItem>& todos,
                                                   const std::string& ts) {
    nlohmann::json line;
    line["type"]  = "todo_snapshot";
    line["ts"]    = ts;
    line["todos"] = todosToJson(todos);
    return dumpLine(line);
}

std::string CLFMessageCodec::serializeCompleteLine(const std::vector<CLFTodoItem>& todos,
                                                   const std::string& ts) {
    nlohmann::json line;
    line["type"]  = "complete";
    line["ts"]    = ts;
    line["todos"] = todosToJson(todos);
    return dumpLine(line);
}

std::string CLFMessageCodec::serializeSummaryLine(const CLFSessionSummary& summary,
                                                  const std::string& ts) {
    nlohmann::json line;
    line["type"]    = "summary";
    line["ts"]      = ts;
    line["summary"] = summaryToJson(summary);
    return dumpLine(line);
}

bool CLFMessageCodec::parseTurnLine(const nlohmann::json& obj,
                                    std::vector<CLFMessage>& outMessages,
                                    std::vector<CLFTodoItem>* outTodos,
                                    std::string* outTs) {
    try {
        if (!obj.is_object() || obj.value("type", "") != "turn") return false;
        if (!obj.contains("messages") || !obj["messages"].is_array()) return false;
        outMessages = messagesFromJson(obj["messages"]);
        if (outTodos) *outTodos = todosFromJson(obj.value("todos", nlohmann::json::array()));
        if (outTs)    *outTs    = obj.value("ts", "");
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool CLFMessageCodec::parseTodoSnapshotLine(const nlohmann::json& obj,
                                            std::vector<CLFTodoItem>& outTodos) {
    try {
        if (!obj.is_object() || obj.value("type", "") != "todo_snapshot") return false;
        outTodos = todosFromJson(obj.value("todos", nlohmann::json::array()));
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool CLFMessageCodec::parseCompleteLine(const nlohmann::json& obj,
                                        std::vector<CLFTodoItem>& outTodos) {
    try {
        if (!obj.is_object() || obj.value("type", "") != "complete") return false;
        outTodos = todosFromJson(obj.value("todos", nlohmann::json::array()));
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool CLFMessageCodec::parseSummaryLine(const nlohmann::json& obj,
                                       CLFSessionSummary& outSummary) {
    try {
        if (!obj.is_object() || obj.value("type", "") != "summary") return false;
        summaryFromJson(obj.value("summary", nlohmann::json()), outSummary);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool CLFMessageCodec::parseHeaderLine(const nlohmann::json& obj,
                                      std::string* outTitle,
                                      std::string* outStartedAt,
                                      std::string* outSessionId,
                                      std::string* outModel,
                                      std::vector<std::string>* outSkills) {
    try {
        if (!obj.is_object() || obj.value("type", "") != "header") return false;
        if (outTitle)     *outTitle     = obj.value("title", "");
        if (outStartedAt) *outStartedAt = obj.value("started_at", "");
        if (outSessionId) *outSessionId = obj.value("session_id", "");
        if (outModel)     *outModel     = obj.value("model", "");
        if (outSkills) {
            outSkills->clear();
            if (obj.contains("skills") && obj["skills"].is_array()) {
                for (const auto& s : obj["skills"]) {
                    if (s.is_string()) outSkills->push_back(s.get<std::string>());
                }
            }
        }
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

} // namespace CLF::CLFCore
