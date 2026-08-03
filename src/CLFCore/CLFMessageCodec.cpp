// CLFMessageCodec.cpp — 消息 JSON 编解码器实现

#include "CLFCore/CLFMessageCodec.hpp"

#include <nlohmann/json.hpp>

namespace CLF::CLFCore {

std::string CLFMessageCodec::serialize(const std::vector<CLFMessage>& messages,
                                        const std::string& savedAt,
                                        const std::string& title) {
    nlohmann::json data;
    data["version"]      = 1;
    data["messageCount"] = static_cast<int>(messages.size());
    if (!savedAt.empty()) data["saved_at"] = savedAt;
    if (!title.empty())   data["title"]    = title;

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
                                                     std::string* outTitle) {
    std::vector<CLFMessage> result;
    try {
        nlohmann::json data = nlohmann::json::parse(jsonData);
        if (!data.contains("messages") || !data["messages"].is_array()) {
            return result;
        }

        if (outVersion)  *outVersion  = data.value("version", 0);
        if (outSavedAt)  *outSavedAt  = data.value("saved_at", "");
        if (outTitle)    *outTitle    = data.value("title", "");

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
