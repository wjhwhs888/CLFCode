// CLFMessageCodec.cpp — 消息 JSON 编解码器实现

#include "CLFCore/CLFMessageCodec.hpp"

#include <nlohmann/json.hpp>

namespace CLF::CLFCore {

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
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : todos) {
            arr.push_back({{"id", t.m_id}, {"content", t.m_content}, {"status", t.m_status}});
        }
        data["todos"] = std::move(arr);
    }

    // summary 对象
    if (summary && summary->m_valid) {
        nlohmann::json sum;
        sum["text"]         = summary->m_summary;
        sum["method"]       = summary->m_method;
        sum["current_plan"] = summary->m_currentPlan;
        if (!summary->m_keyDecisions.empty()) {
            nlohmann::json kd = nlohmann::json::array();
            for (const auto& d : summary->m_keyDecisions) kd.push_back(d);
            sum["key_decisions"] = std::move(kd);
        }
        if (!summary->m_filesModified.empty()) {
            nlohmann::json fm = nlohmann::json::array();
            for (const auto& f : summary->m_filesModified) fm.push_back(f);
            sum["files_modified"] = std::move(fm);
        }
        if (!summary->m_pendingTasks.empty()) {
            nlohmann::json pt = nlohmann::json::array();
            for (const auto& t : summary->m_pendingTasks) pt.push_back(t);
            sum["pending_tasks"] = std::move(pt);
        }
        data["summary"] = std::move(sum);
    }

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
    data["messages"] = std::move(msgs);

    try {
        return data.dump();
    } catch (const nlohmann::json::exception&) {
        return data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
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
            outTodos->clear();
            if (data.contains("todos") && data["todos"].is_array()) {
                for (const auto& t : data["todos"]) {
                    if (!t.is_object()) continue;
                    CLFTodoItem item;
                    item.m_id      = t.value("id", "");
                    item.m_content = t.value("content", "");
                    item.m_status  = t.value("status", "pending");
                    if (!item.m_content.empty()) outTodos->push_back(std::move(item));
                }
            }
        }

        // summary 对象
        if (outSummary) {
            *outSummary = {};
            if (data.contains("summary") && data["summary"].is_object()) {
                const auto& sum = data["summary"];
                outSummary->m_summary     = sum.value("text", "");
                outSummary->m_currentPlan = sum.value("current_plan", "");
                outSummary->m_method      = sum.value("method", "rule_based");
                outSummary->m_valid       = !outSummary->m_summary.empty();
                if (sum.contains("key_decisions") && sum["key_decisions"].is_array()) {
                    for (const auto& kd : sum["key_decisions"]) {
                        if (kd.is_string()) outSummary->m_keyDecisions.push_back(kd.get<std::string>());
                    }
                }
                if (sum.contains("files_modified") && sum["files_modified"].is_array()) {
                    for (const auto& fm : sum["files_modified"]) {
                        if (fm.is_string()) outSummary->m_filesModified.push_back(fm.get<std::string>());
                    }
                }
                if (sum.contains("pending_tasks") && sum["pending_tasks"].is_array()) {
                    for (const auto& pt : sum["pending_tasks"]) {
                        if (pt.is_string()) outSummary->m_pendingTasks.push_back(pt.get<std::string>());
                    }
                }
            }
        }

        for (const auto& m : data["messages"]) {
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
    } catch (const nlohmann::json::exception&) {}
    return result;
}

} // namespace CLF::CLFCore
