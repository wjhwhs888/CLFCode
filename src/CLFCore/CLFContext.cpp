// CLFContext.cpp — 对话上下文实现

#include "CLFCore/CLFContext.hpp"
#include <algorithm>
#include <string>

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

} // namespace CLF::CLFCore
