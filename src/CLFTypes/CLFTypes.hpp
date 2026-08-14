// CLFTypes.hpp — 项目共享纯数据类型定义
// 零项目头依赖（仅标准库），所有模块安全包含
//
// 从 CLFAgentLoop.hpp / CLFContext.hpp / CLFSecurityPolicy.hpp 提取，
// 消除 CLFConfigLoader→CLFAgentLoop、CLFProtocolAdapter→CLFAgentLoop 的重型头依赖

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace CLF::CLFCore {

// ============================================================================
// 安全策略相关枚举
// ============================================================================

enum class CLFSecurityMode {
    Auto    = 0,  // L1 自动：全放行
    Analyze = 1,  // L2 分析：读放行，写/命令阻断
    Edit    = 2,  // L3 编辑：读放行，写/命令需确认
    Manual  = 3   // L4 手动：读放行，写/命令需确认
};

enum class CLFToolRisk {
    Read    = 0,  // 读操作：永不限制
    Write   = 1,  // 写操作（文件覆盖等）
    Command = 2   // 命令执行（不可控副作用）
};

// ============================================================================
// 消息与工具调用数据结构
// ============================================================================

struct CLFToolCall {
    std::string m_id;
    std::string m_name;
    std::string m_arguments; // JSON string
};

struct CLFToolResult {
    std::string m_toolCallId;
    std::string m_name;      // 工具名
    std::string m_content;
};

struct CLFMessage {
    std::string m_role;     // "system" | "user" | "assistant" | "tool"
    std::string m_content;  // 文本内容（assistant 发出 tool_calls 时可为空）
    std::vector<CLFToolCall> m_toolCalls; // assistant role: 请求的工具调用
    std::string m_toolCallId;             // tool role: 对应的 tool_call_id
    std::string m_name;                   // tool role: 函数名
};

// ============================================================================
// 工具调用统计
// ============================================================================

struct ToolStats {
    int searchCount = 0;
    int readCount   = 0;
    int totalCalls  = 0;
};

// ============================================================================
// Agent 配置
// ============================================================================

struct CLFAgentConfig {
    // —— connection（连接认证）——
    std::string m_apiBaseUrl = "https://api.deepseek.com";
    std::string m_apiKey;

    // —— chat_completions（对齐 DeepSeek API 参数）——
    std::string m_modelName    = "deepseek-v4-flash";
    std::string m_subModel     = "deepseek-v4-pro";
    int         m_maxTokens    = 8192;
    float       m_temperature  = 0.0f;
    float       m_topP         = 1.0f;
    float       m_frequencyPenalty = 0.0f;
    float       m_presencePenalty  = 0.0f;
    std::string m_responseFormat   = "text";
    std::vector<std::string> m_stop;
    bool        m_stream       = false;
    std::string m_thinkingLevel = "max";

    // —— agent（Agent 行为参数）——
    int         m_maxContextWindow      = 1048576;
    int         m_maxToolCallIterations = 16;
    bool        m_contextCompression    = false;
    int         m_maxResponseDelaySec   = 300;
    std::string m_interactionLanguage   = "zh-CN";
    std::string m_securityMode          = "edit";

    // —— logging（日志配置）——
    std::string m_logLevel   = "info";
    std::string m_logFile    = "clf_agent.log";
    bool        m_logConsole = false;
};

// ============================================================================
// 工具定义
// ============================================================================

struct CLFTool {
    std::string m_name;
    std::string m_description;
    std::string m_parametersSchema; // JSON Schema 字符串
    CLFToolRisk m_risk = CLFToolRisk::Read;
    std::function<std::string(const std::string&)> m_handler; // 参数为 JSON string
};

// ============================================================================
// 会话摘要（/exit 时生成，/resume 时注入为 system 消息）
// ============================================================================

struct CLFSessionSummary {
    std::string              m_summary;        // 总体摘要文本
    std::string              m_currentPlan;    // 当前计划/任务描述
    std::vector<std::string> m_keyDecisions;   // 关键决策列表
    std::vector<std::string> m_filesModified;  // 涉及的文件路径
    std::vector<std::string> m_pendingTasks;   // 待完成任务
    std::string              m_method;         // "api" | "rule_based"
    bool                     m_valid = false;

    bool isEmpty() const { return !m_valid || m_summary.empty(); }

    // 转换为注入上下文的 system 消息体
    std::string toSystemMessage() const {
        std::string msg = "[会话摘要";
        if (m_method == "api") msg += " — API 生成";
        msg += "]\n\n";
        msg += "## 摘要\n" + m_summary + "\n";
        if (!m_keyDecisions.empty()) {
            msg += "\n## 关键决策\n";
            for (const auto& d : m_keyDecisions) msg += "- " + d + "\n";
        }
        if (!m_currentPlan.empty()) {
            msg += "\n## 当前进度\n" + m_currentPlan + "\n";
        }
        if (!m_filesModified.empty()) {
            msg += "\n## 涉及文件\n";
            for (const auto& f : m_filesModified) msg += "- `" + f + "`\n";
        }
        if (!m_pendingTasks.empty()) {
            msg += "\n## 待完成\n";
            for (const auto& t : m_pendingTasks) msg += "- [ ] " + t + "\n";
        }
        return msg;
    }
};

// ============================================================================
// 计时器标签配置
// ============================================================================

struct CLFTimerLabels {
    std::string thinking = "thinking";
    std::string thought   = "thought";
    std::string working   = "Working";
    std::string worked    = "Worked";
};

// 格式化耗时（秒 → "1m55s" / "38s"）
inline std::string formatDurationSeconds(long long sec) {
    if (sec >= 60)
        return std::to_string(sec / 60) + "m" + std::to_string(sec % 60) + "s";
    return std::to_string(sec) + "s";
}

// UI 侧 head/tail 截断（dsh 模式）：前 n 条 + marker + 后 n 条
// 条目数 <= 2n 时原样返回；按"条"操作，天然不劈半多字节字符
template <typename T>
std::vector<T> headTailCapWithMarker(const std::vector<T>& items,
                                     const T& marker, size_t n = 16) {
    if (items.size() <= n * 2) return items;
    std::vector<T> out;
    out.reserve(n * 2 + 1);
    out.insert(out.end(), items.begin(), items.begin() + n);
    out.push_back(marker);
    out.insert(out.end(), items.end() - n, items.end());
    return out;
}

} // namespace CLF::CLFCore
