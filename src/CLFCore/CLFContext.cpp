// CLFContext.cpp — 对话上下文实现
// 序列化 → 委托 CLFMessageCodec

#include "CLFCore/CLFContext.hpp"
#include "CLFCore/CLFMessageCodec.hpp"

#include <algorithm>
#include <string>

namespace CLF::CLFCore {

namespace {

constexpr size_t kMaxMessageChars = 8000;

int estimateTokensForMessage(const CLFMessage& msg) {
    int ascii = 0;
    int nonAscii = 0;

    auto countText = [&](const std::string& text) {
        for (size_t i = 0; i < text.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c < 0x80) ++ascii;
            else if ((c & 0xC0) == 0xC0) ++nonAscii;
        }
    };

    countText(msg.m_content);
    for (const auto& tc : msg.m_toolCalls) {
        countText(tc.m_arguments);
        ascii += static_cast<int>(tc.m_id.size());
        ascii += static_cast<int>(tc.m_name.size());
    }

    return (ascii / 4) + (nonAscii * 3 / 2);
}

std::string truncateContent(const std::string& content, bool isToolResult) {
    if (content.size() <= kMaxMessageChars) return content;
    if (!isToolResult) return content;
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

    for (const auto& msg : m_messages) {
        if (msg.m_role == "system") {
            result.push_back(msg);
            tokenCount += estimateTokensForMessage(msg);
        } else {
            nonSystem.push_back(msg);
        }
    }

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
    return CLFMessageCodec::serialize(m_messages);
}

bool CLFContext::restore(const std::string& jsonData) {
    auto messages = CLFMessageCodec::parse(jsonData);
    if (messages.empty()) return false;

    m_messages.clear();
    for (auto& msg : messages) {
        if (msg.m_role == "system") continue;
        m_messages.push_back(std::move(msg));
    }
    return !m_messages.empty();
}

} // namespace CLF::CLFCore
