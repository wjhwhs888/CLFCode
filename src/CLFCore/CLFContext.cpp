// CLFContext.cpp — 对话上下文容器实现（C2b：纯容器化）
// 窗口截断 → CLFContextWindow；tool result 截断 → CLFTextUtil::truncateToolResult

#include "CLFCore/CLFContext.hpp"
#include "CLFTypes/CLFEncoding.hpp"
#include "CLFTypes/CLFTextUtil.hpp"

#include <algorithm>
#include <string>

namespace CLF::CLFCore {

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
    // C2b：内容截断移出（CLFTextUtil::truncateToolResult，调用方截断后入库）；
    // 容器仅保证存储不变量（sanitize 合法 UTF-8）
    CLFMessage msg;
    msg.m_role       = "tool";
    msg.m_content    = CLFEncoding::sanitizeUtf8(content);
    msg.m_toolCallId = toolCallId;
    msg.m_name       = name;
    m_messages.push_back(std::move(msg));
}

void CLFContext::appendMessage(const CLFMessage& msg) {
    m_messages.push_back(msg);
}

std::vector<CLFMessage> CLFContext::getMessages() const {
    // C2b：全量返回（窗口截断策略在 CLFContextWindow::apply）
    return m_messages;
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
