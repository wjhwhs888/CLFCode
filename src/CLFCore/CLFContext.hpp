// CLFContext.hpp — 对话上下文管理器
// 管理对话历史、token 估算和上下文窗口截断

#pragma once

#include <string>
#include <vector>

namespace CLF::CLFCore {

struct CLFMessage {
    std::string m_role;     // "system" | "user" | "assistant" | "tool"
    std::string m_content;
};

struct CLFToolCall {
    std::string m_id;
    std::string m_name;
    std::string m_arguments; // JSON string
};

struct CLFToolResult {
    std::string m_toolCallId;
    std::string m_content;
};

class CLFContext {
public:
    explicit CLFContext(int maxContextWindow = 65536);

    // 添加消息
    void addMessage(const std::string& role, const std::string& content);

    // 获取消息列表（自动截断到窗口大小）
    std::vector<CLFMessage> getMessages() const;

    // 清空历史
    void clear();

    // 估算当前 token 数（简单按字符数 / 4 估算）
    int estimateTokens() const;

private:
    std::vector<CLFMessage> m_messages;
    int                     m_maxContextWindow;
};

} // namespace CLF::CLFCore
