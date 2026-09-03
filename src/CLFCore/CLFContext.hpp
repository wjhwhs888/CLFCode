// CLFContext.hpp — 对话上下文管理器
// 管理对话历史、token 估算和上下文窗口截断

#pragma once

#include <string>
#include <vector>

#include "CLFTypes/CLFTypes.hpp"

namespace CLF::CLFCore {

// CLFMessage / CLFToolCall / CLFToolResult 定义在 CLFTypes.hpp

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

    // 完整添加一条消息（会话恢复用，保留全部字段）
    void appendMessage(const CLFMessage& msg);

    // 获取消息列表（自动截断到窗口大小）
    std::vector<CLFMessage> getMessages() const;

    // 清空历史
    void clear();

    // 设置/替换 system 消息为单条（system 区始终只有 1 条）
    // - 若 m_messages 中已有 system 消息，替换第一条的 content
    // - 若没有 system 消息，在 m_messages 头部插入
    // - 新内容与当前内容相同时跳过（去重）
    void setSystemPrompt(const std::string& content);

    // 移除所有 system 消息（/clear 时使用，非 system 消息保留）
    void removeSystemMessages();

    // 估算当前 token 数（简单按字符数 / 4 估算）
    int estimateTokens() const;

private:
    std::vector<CLFMessage> m_messages;
    int                     m_maxContextWindow;
};

// UTF-8 净化：将非法字节序列替换为 U+FFFD (�)
// 用于输入边界防护（剪贴板粘贴等）
std::string sanitizeUtf8(const std::string& input);

} // namespace CLF::CLFCore
