// CLFContext.cpp — 对话上下文实现

#include "CLFCore/CLFContext.hpp"
#include <algorithm>

namespace CLF::CLFCore {

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
    msg.m_content    = content;
    msg.m_toolCallId = toolCallId;
    msg.m_name       = name;
    m_messages.push_back(std::move(msg));
}

std::vector<CLFMessage> CLFContext::getMessages() const {
    // 从最新消息往前累计 token，截断超出窗口的部分
    std::vector<CLFMessage> result;
    int tokenCount = 0;

    for (auto it = m_messages.rbegin(); it != m_messages.rend(); ++it) {
        int msgTokens = static_cast<int>(it->m_content.size()) / 4;
        if (tokenCount + msgTokens > m_maxContextWindow && !result.empty()) {
            break;
        }
        tokenCount += msgTokens;
        result.push_back(*it);
    }

    std::reverse(result.begin(), result.end());
    return result;
}

void CLFContext::clear() {
    m_messages.clear();
}

int CLFContext::estimateTokens() const {
    int total = 0;
    for (const auto& msg : m_messages) {
        total += static_cast<int>(msg.m_content.size()) / 4;
        // 计入 tool_calls 的参数大小
        for (const auto& tc : msg.m_toolCalls) {
            total += static_cast<int>(tc.m_arguments.size()) / 4;
        }
    }
    return total;
}

} // namespace CLF::CLFCore
