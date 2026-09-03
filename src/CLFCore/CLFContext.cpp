// CLFContext.cpp — 对话上下文实现

#include "CLFCore/CLFContext.hpp"

#include <algorithm>
#include <string>

namespace CLF::CLFCore {

// UTF-8 净化：将非法字节序列替换为 U+FFFD (�)
// 从字节流中识别合法的 1~4 字节 UTF-8 序列, 非法部分逐字节替换
std::string sanitizeUtf8(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        // ASCII
        if (c < 0x80) { out += static_cast<char>(c); ++i; continue; }
        // 2-byte sequence (C2..DF 80..BF)
        if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 < input.size() &&
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80)
            { out += input[i]; out += input[i+1]; i += 2; continue; }
        }
        // 3-byte sequence (E0..EF 80..BF 80..BF)
        else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 < input.size() &&
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80)
            {
                // 排除 overlong encodings
                if (c == 0xE0 && static_cast<unsigned char>(input[i+1]) < 0xA0) goto invalid;
                if (c == 0xED && static_cast<unsigned char>(input[i+1]) > 0x9F) goto invalid;
                out += input[i]; out += input[i+1]; out += input[i+2]; i += 3; continue;
            }
        }
        // 4-byte sequence (F0..F4 80..BF 80..BF 80..BF)
        else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 < input.size() &&
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+3]) & 0xC0) == 0x80)
            {
                if (c == 0xF0 && static_cast<unsigned char>(input[i+1]) < 0x90) goto invalid;
                if (c == 0xF4 && static_cast<unsigned char>(input[i+1]) > 0x8F) goto invalid;
                out += input[i]; out += input[i+1]; out += input[i+2]; out += input[i+3]; i += 4; continue;
            }
        }
    invalid:
        // 非法字节 → U+FFFD (3 bytes in UTF-8: EF BF BD)
        out += '\xEF'; out += '\xBF'; out += '\xBD';
        ++i;
    }
    return out;
}

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
    m_messages.push_back({role, sanitizeUtf8(content)});
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
    msg.m_content    = truncateContent(sanitizeUtf8(content), true);
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

void CLFContext::setSystemPrompt(const std::string& content) {
    auto it = std::find_if(m_messages.begin(), m_messages.end(),
        [](const CLFMessage& m) { return m.m_role == "system"; });
    if (it != m_messages.end()) {
        if (it->m_content == content) return;  // 内容未变，跳过
        it->m_content = content;
    } else {
        CLFMessage msg;
        msg.m_role    = "system";
        msg.m_content = content;
        m_messages.insert(m_messages.begin(), std::move(msg));
    }
}

void CLFContext::removeSystemMessages() {
    m_messages.erase(
        std::remove_if(m_messages.begin(), m_messages.end(),
            [](const CLFMessage& m) { return m.m_role == "system"; }),
        m_messages.end());
}

int CLFContext::estimateTokens() const {
    int total = 0;
    for (const auto& msg : m_messages) {
        total += estimateTokensForMessage(msg);
    }
    return total;
}

} // namespace CLF::CLFCore
