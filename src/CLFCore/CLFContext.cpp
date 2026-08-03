// CLFContext.cpp — 对话上下文实现

#include "CLFCore/CLFContext.hpp"
#include <algorithm>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace CLF::CLFCore {

namespace {

// 消息内容最大字符数（超出截断），保护上下文不被单条长消息打爆
constexpr size_t kMaxMessageChars = 8000;

// 估算单条消息的 token 数
// ASCII 字符 ≈ 0.25 token/字，CJK 等多字节字符 ≈ 1.5 token/字
int estimateTokensForMessage(const CLFMessage& msg) {
    int ascii = 0;
    int nonAscii = 0;

    auto countText = [&](const std::string& text) {
        for (size_t i = 0; i < text.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c < 0x80) {
                ++ascii;
            } else if ((c & 0xC0) == 0xC0) {
                ++nonAscii; // UTF-8 lead byte = 1 Unicode character
            }
        }
    };

    countText(msg.m_content);
    for (const auto& tc : msg.m_toolCalls) {
        countText(tc.m_arguments);
        // id + name 也计入
        ascii += static_cast<int>(tc.m_id.size());
        ascii += static_cast<int>(tc.m_name.size());
    }

    return (ascii / 4) + (nonAscii * 3 / 2);
}

// 截断过长内容
std::string truncateContent(const std::string& content, bool isToolResult) {
    if (content.size() <= kMaxMessageChars) return content;
    if (!isToolResult) return content; // 非工具结果不截断

    return content.substr(0, kMaxMessageChars)
           + "\n\n[truncated, original: " + std::to_string(content.size()) + " chars]";
}

} // anonymous namespace

CLFContext::CLFContext(int maxContextWindow)
    : m_maxContextWindow(maxContextWindow) {
}

void CLFContext::addMessage(const std::string& role, const std::string& content) {
    m_messages.push_back({role, content});
}

void CLFContext::addAssistantToolCalls(const std::vector<CLFToolCall>& toolCalls,
                                       const std::string& content) {
    CLFMessage msg;
    msg.m_role      = "assistant";
    msg.m_content   = content;
    msg.m_toolCalls = toolCalls;
    m_messages.push_back(std::move(msg));
}

void CLFContext::addToolResult(const std::string& toolCallId,
                               const std::string& name,
                               const std::string& content) {
    CLFMessage msg;
    msg.m_role       = "tool";
    msg.m_content    = truncateContent(content, true);
    msg.m_toolCallId = toolCallId;
    msg.m_name       = name;
    m_messages.push_back(std::move(msg));
}

void CLFContext::appendMessage(const CLFMessage& msg) {
    m_messages.push_back(msg);
}

std::vector<CLFMessage> CLFContext::getMessages() const {
    std::vector<CLFMessage> result;
    std::vector<CLFMessage> nonSystem;
    int tokenCount = 0;

    // 第一遍：system 消息单独收集（永不截断）
    for (const auto& msg : m_messages) {
        if (msg.m_role == "system") {
            result.push_back(msg);
            tokenCount += estimateTokensForMessage(msg);
        } else {
            nonSystem.push_back(msg);
        }
    }

    // 第二遍：非 system 消息从新到旧截断
    std::vector<CLFMessage> truncated;
    for (auto it = nonSystem.rbegin(); it != nonSystem.rend(); ++it) {
        int msgTokens = estimateTokensForMessage(*it);
        if (tokenCount + msgTokens > m_maxContextWindow && !truncated.empty()) {
            break;
        }
        tokenCount += msgTokens;
        truncated.push_back(*it);
    }

    std::reverse(truncated.begin(), truncated.end());
    result.insert(result.end(), truncated.begin(), truncated.end());
    return result;
}

void CLFContext::clear() {
    m_messages.clear();
}

int CLFContext::estimateTokens() const {
    int total = 0;
    for (const auto& msg : m_messages) {
        total += estimateTokensForMessage(msg);
    }
    return total;
}

std::string CLFContext::serialize() const {
    json data;
    data["version"]   = 1;
    data["messageCount"] = static_cast<int>(m_messages.size());

    json msgs = json::array();
    for (const auto& msg : m_messages) {
        json m;
        m["role"]    = msg.m_role;
        m["content"] = msg.m_content;

        if (!msg.m_toolCalls.empty()) {
            json tcs = json::array();
            for (const auto& tc : msg.m_toolCalls) {
                json tcJson;
                tcJson["id"]        = tc.m_id;
                tcJson["name"]      = tc.m_name;
                tcJson["arguments"] = tc.m_arguments;
                tcs.push_back(std::move(tcJson));
            }
            m["tool_calls"] = std::move(tcs);
        }
        if (!msg.m_toolCallId.empty()) {
            m["tool_call_id"] = msg.m_toolCallId;
        }
        if (!msg.m_name.empty()) {
            m["name"] = msg.m_name;
        }
        msgs.push_back(std::move(m));
    }
    data["messages"] = std::move(msgs);

    try {
        return data.dump();
    } catch (const json::exception&) {
        // 无效 UTF-8 降级：用 replace 字符替代非法字节
        return data.dump(-1, ' ', false, json::error_handler_t::replace);
    }
}

bool CLFContext::restore(const std::string& jsonData) {
    try {
        json data = json::parse(jsonData);
        if (!data.contains("messages") || !data["messages"].is_array()) {
            return false;
        }

        m_messages.clear();
        for (const auto& m : data["messages"]) {
            if (!m.contains("role") || !m.contains("content")) {
                continue;
            }
            std::string role = m["role"].get<std::string>();
            // 跳过 system（身份由 Agent 重新注入）
            if (role == "system") continue;

            CLFMessage msg;
            msg.m_role    = role;
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
            m_messages.push_back(std::move(msg));
        }
        return !m_messages.empty();
    } catch (const json::exception&) {
        return false;
    }
}

} // namespace CLF::CLFCore
