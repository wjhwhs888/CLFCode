// CLFProtocolAdapter.hpp — Chat Completion API 协议适配器
// 负责 JSON 请求体构建与响应解析，与具体 provider（DeepSeek/OpenAI）解耦
//
// 支持的 OpenAI 兼容 tool_calling 协议：
//   - 请求：messages[] + tools[] + tool_choice
//   - 响应：message.content + message.tool_calls[] + finish_reason
//   - 四种消息角色：system / user / assistant（text|tool_calls） / tool

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "CLFTypes/CLFTypes.hpp"

namespace CLF::CLFCore {

// CLFTool / CLFAgentConfig / CLFMessage / CLFToolCall 定义在 CLFTypes.hpp

// 解析后的 assistant 响应
struct CLFAssistantResponse {
    std::string              m_content;      // 文本内容（tool_calls 存在时可能为空）
    std::vector<CLFToolCall> m_toolCalls;    // 模型请求的工具调用列表
    std::string              m_finishReason; // "stop" | "tool_calls" | "length" | "content_filter"
    // P2-4: usage（默认 0 = 未返回——R3 不估猜，缺失时上层跳过统计）
    int                      m_usagePrompt     = 0;
    int                      m_usageCompletion = 0;
    int                      m_usageTotal      = 0;
};

class CLFProtocolAdapter {
public:
    CLFProtocolAdapter() = default;

    // 构建 /v1/chat/completions 的完整 POST 请求体
    // 直接从 CLFAgentConfig 读取所有 API 参数，新加字段无需改接口
    std::string buildChatRequest(
        const std::vector<CLFMessage>& messages,
        const std::vector<CLFTool>&    tools,
        const CLFAgentConfig&          config
    ) const;

    // 解析非流式 API 响应中的 assistant message
    CLFAssistantResponse parseAssistantResponse(const std::string& responseBody) const;

    // 判断响应是否包含 tool_calls
    static bool hasToolCalls(const CLFAssistantResponse& resp);

    // 判断 finish_reason 是否为正常结束（"stop" 或 "tool_calls"）
    static bool isValidFinish(const CLFAssistantResponse& resp);

private:
    // 将一条 CLFMessage 序列化为 OpenAI 兼容 JSON
    nlohmann::json serializeMessage(const CLFMessage& msg) const;

    // 将 CLFTool 定义序列化为 tools 数组中的一个元素
    nlohmann::json serializeToolDefinition(const CLFTool& tool) const;
};

} // namespace CLF::CLFCore
