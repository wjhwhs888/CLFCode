// CLFContext.cpp — 对话上下文实现

#include "CLFCore/CLFContext.hpp"
#include "CLFTypes/CLFEncoding.hpp"
#include "CLFTypes/CLFTextUtil.hpp"

#include <algorithm>
#include <string>

namespace CLF::CLFCore {

namespace {

constexpr size_t kMaxMessageChars = 8000;

std::string truncateContent(const std::string& content, bool isToolResult) {
    if (content.size() <= kMaxMessageChars) return content;
    if (!isToolResult) return content;
    // A2：字节级截断 → utf8SafeHead（原 substr(0, kMaxMessageChars) 会劈半多字节）
    return CLFTextUtil::utf8SafeHead(content, kMaxMessageChars)
           + "\n\n[truncated, original: " + std::to_string(content.size()) + " chars]";
}

} // anonymous namespace

CLFContext::CLFContext(int maxContextWindow)
    : m_maxContextWindow(maxContextWindow) {
}

void CLFContext::addMessage(const std::string& role, const std::string& content) {
    m_messages.push_back({role, CLFEncoding::sanitizeUtf8(content)});
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
    msg.m_content    = truncateContent(CLFEncoding::sanitizeUtf8(content), true);
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
            tokenCount += CLFTextUtil::estimateTokensForMessage(msg);
        } else {
            nonSystem.push_back(msg);
        }
    }

    std::vector<CLFMessage> truncated;
    for (auto it = nonSystem.rbegin(); it != nonSystem.rend(); ++it) {
        int msgTokens = CLFTextUtil::estimateTokensForMessage(*it);
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
        total += CLFTextUtil::estimateTokensForMessage(msg);
    }
    return total;
}

} // namespace CLF::CLFCore
