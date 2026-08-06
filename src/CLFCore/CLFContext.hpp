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

    // 估算当前 token 数（简单按字符数 / 4 估算）
    int estimateTokens() const;

    // 序列化全部消息为 JSON 字符串（供会话持久化）
    std::string serialize() const;

    // 从 JSON 字符串恢复消息（跳过 system 消息——身份由 Agent 重新注入）
    // 返回 false 表示数据无效
    bool restore(const std::string& jsonData);

private:
    std::vector<CLFMessage> m_messages;
    int                     m_maxContextWindow;
};

// UTF-8 净化：将非法字节序列替换为 U+FFFD (�)
// 用于输入边界防护（剪贴板粘贴等）
std::string sanitizeUtf8(const std::string& input);

} // namespace CLF::CLFCore
