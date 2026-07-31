// CLFContext.hpp — 对话上下文管理器
// 管理对话历史、token 估算和上下文窗口截断

#pragma once

#include <string>
#include <vector>

namespace CLF::CLFCore {

// 前置声明（CLFMessage 中引用 CLFToolCall）
struct CLFToolCall;

struct CLFMessage {
    std::string m_role;     // "system" | "user" | "assistant" | "tool"
    std::string m_content;  // 文本内容（assistant 发出 tool_calls 时可为空）
    std::vector<CLFToolCall> m_toolCalls; // assistant role: 请求的工具调用
    std::string m_toolCallId;             // tool role: 对应的 tool_call_id
    std::string m_name;                   // tool role: 函数名（部分 provider 要求）
};

struct CLFToolCall {
    std::string m_id;
    std::string m_name;
    std::string m_arguments; // JSON string
};

struct CLFToolResult {
    std::string m_toolCallId;
    std::string m_name;      // 工具名（用于 addToolResult）
    std::string m_content;
};

class CLFContext {
public:
    explicit CLFContext(int maxContextWindow = 65536);

    // 添加消息
    void addMessage(const std::string& role, const std::string& content);

    // 添加含 tool_calls 的 assistant 消息（content 可为空）
    void addAssistantToolCalls(const std::vector<CLFToolCall>& toolCalls,
                               const std::string& content = "");

    // 添加 tool 角色结果消息
    void addToolResult(const std::string& toolCallId,
                       const std::string& name,
                       const std::string& content);

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
