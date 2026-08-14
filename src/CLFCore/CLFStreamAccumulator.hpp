// CLFStreamAccumulator.hpp — SSE 流式响应 delta 累积器
// 接收 SSE 逐块 delta JSON，累积文本内容和 tool_calls
//
// 用法:
//   CLFStreamAccumulator acc;
//   for each SSE "data: {...}" line:
//       std::string delta = acc.feedDelta(nlohmann::json::parse(jsonStr));
//       if (!delta.empty()) std::cout << delta << std::flush;  // 实时输出
//   CLFAssistantResponse parsed{acc.getContent(), acc.getToolCalls(), acc.getFinishReason()};

#pragma once

#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "CLFTypes/CLFTypes.hpp"

namespace CLF::CLFCore {

class CLFStreamAccumulator {
public:
    CLFStreamAccumulator() = default;

    // 输入一个 delta JSON 对象（已从 "data: " 行解析）
    // 返回本块的 content 增量文本（用于实时显示），无内容返回空字符串
    std::string feedDelta(const nlohmann::json& delta);

    // P2-4: 输入 usage 对象——流式 usage chunk 的 choices 为空数组，
    // 调用方（AgentLoop 流式 lambda）须在 choices 过滤之前单独投喂
    void feedUsage(const nlohmann::json& usage);

    // 标记流结束（收到 [DONE] 时调用）
    void markDone();

    bool isFinished() const { return m_finished; }
    const std::string& getContent() const { return m_content; }
    const std::vector<CLFToolCall>& getToolCalls() const { return m_toolCalls; }
    const std::string& getFinishReason() const { return m_finishReason; }

    // 思考过程（reasoning_content，与 content 分通道，供 UI 折叠）
    bool hasReasoning() const { return !m_reasoning.empty(); }
    const std::string& getReasoning() const { return m_reasoning; }

    // P2-4: usage（流式 usage chunk 的 choices 为空数组，delta 层独立提取；
    // 未到达时保持 0——不估猜）
    int getUsagePrompt() const { return m_usagePrompt; }
    int getUsageCompletion() const { return m_usageCompletion; }
    int getUsageTotal() const { return m_usageTotal; }

    void reset();

private:
    // 将 map 中累积的部分 tool_calls 转为最终 CLFToolCall 向量
    void finalizeToolCalls();

    std::string m_content;
    std::string m_reasoning;  // 独立累积推理内容
    std::string m_finishReason;
    std::vector<CLFToolCall> m_toolCalls;
    bool m_finished = false;
    bool m_toolCallsFinalized = false;
    int m_usagePrompt = 0;      // P2-4
    int m_usageCompletion = 0;  // P2-4
    int m_usageTotal = 0;       // P2-4

    struct Part {
        std::string id;
        std::string name;
        std::string args;
    };
    std::map<int, Part> m_parts; // key = tool_calls[index].index
};

// ============================================================================
// inline 实现
// ============================================================================

inline std::string CLFStreamAccumulator::feedDelta(const nlohmann::json& delta) {
    std::string contentDelta;

    // 提取推理过程 → 独立累积到 m_reasoning（UI 层 Ctrl+O 折叠/展开）
    if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string()) {
        auto rc = delta["reasoning_content"].get<std::string>();
        if (!rc.empty()) {
            m_reasoning += rc;
        }
    }
    // 提取文本增量 → m_content（正式回复，正常流式显示）
    if (delta.contains("content") && delta["content"].is_string()) {
        auto c = delta["content"].get<std::string>();
        if (!c.empty()) {
            contentDelta = c;
            m_content += c;
        }
    }

    // 提取 tool_calls 增量（可能为空数组）
    if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
        for (const auto& tc : delta["tool_calls"]) {
            int idx = tc.value("index", 0);
            auto& part = m_parts[idx];

            if (tc.contains("id") && tc["id"].is_string()) {
                part.id = tc["id"].get<std::string>();
            }
            if (tc.contains("function")) {
                const auto& func = tc["function"];
                if (func.contains("name") && func["name"].is_string()) {
                    part.name = func["name"].get<std::string>();
                }
                if (func.contains("arguments") && func["arguments"].is_string()) {
                    part.args += func["arguments"].get<std::string>();
                }
            }
        }
        m_toolCallsFinalized = false; // 有新的 tool delta，需重新 finalize
    }

    // 提取 finish_reason
    if (delta.contains("finish_reason") && delta["finish_reason"].is_string()) {
        m_finishReason = delta["finish_reason"].get<std::string>();
        // 收到 finish_reason 即视为流结束：立即 finalize tool_calls
        // 不依赖 [DONE]，防止网络中断时工具调用丢失
        if (!m_parts.empty() && !m_toolCallsFinalized) {
            finalizeToolCalls();
        }
    }

    return contentDelta;
}

inline void CLFStreamAccumulator::feedUsage(const nlohmann::json& usage) {
    // P2-4: usage 提取（缺失字段保持原值——不估猜）
    if (!usage.is_object()) return;
    if (usage.contains("prompt_tokens") && usage["prompt_tokens"].is_number())
        m_usagePrompt = usage["prompt_tokens"].get<int>();
    if (usage.contains("completion_tokens") && usage["completion_tokens"].is_number())
        m_usageCompletion = usage["completion_tokens"].get<int>();
    if (usage.contains("total_tokens") && usage["total_tokens"].is_number())
        m_usageTotal = usage["total_tokens"].get<int>();
}

inline void CLFStreamAccumulator::markDone() {
    m_finished = true;
    if (m_finishReason.empty()) {
        m_finishReason = "stop";
    }
    if (!m_parts.empty() && !m_toolCallsFinalized) {
        finalizeToolCalls();
    }
}

inline void CLFStreamAccumulator::reset() {
    m_content.clear();
    m_reasoning.clear();
    m_finishReason.clear();
    m_toolCalls.clear();
    m_parts.clear();
    m_finished = false;
    m_toolCallsFinalized = false;
    m_usagePrompt = 0;
    m_usageCompletion = 0;
    m_usageTotal = 0;
}

inline void CLFStreamAccumulator::finalizeToolCalls() {
    m_toolCalls.clear();
    for (auto& [idx, part] : m_parts) {
        CLFToolCall call;
        call.m_id        = std::move(part.id);
        call.m_name      = std::move(part.name);
        call.m_arguments = std::move(part.args);
        m_toolCalls.push_back(std::move(call));
    }
    m_toolCallsFinalized = true;
}

} // namespace CLF::CLFCore
