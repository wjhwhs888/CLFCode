// CLFAgentLoop.cpp — Agent 主循环实现（含 tool-calling 循环 + 流式响应）

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "CLFCore/CLFStreamAccumulator.hpp"

namespace CLF::CLFCore {

CLFAgentLoop::CLFAgentLoop(const CLFAgentConfig& config)
    : m_config(config)
    , m_context(config.m_maxContextWindow)
    , m_httpClient(config.m_apiBaseUrl, config.m_apiKey)
    , m_securityPolicy(CLFSecurityPolicy::modeFromString(config.m_securityMode)) {
    // 应用回复超时配置（默认 300 秒）
    m_httpClient.setTimeout(config.m_maxResponseDelaySec);
    injectSystemPrompt();
}

std::string CLFAgentLoop::runTurn(const std::string& userInput) {
    m_context.addMessage("user", userInput);

    std::string finalContent;

    for (int iteration = 0; iteration < m_config.m_maxToolCallIterations; ++iteration) {
        try {
            std::string body = m_protocolAdapter.buildChatRequest(
                m_context.getMessages(), m_tools, m_config);

            CLFAssistantResponse parsed;

            if (m_config.m_stream) {
                // ====== 流式路径 ======
                CLFStreamAccumulator acc;
                bool hadError = false;
                std::string errorMsg;

                CLF::CLFNetwork::CLFHttpResponse response =
                    m_httpClient.postJsonStream(
                        "/v1/chat/completions", body,
                        [&](const std::string& line) {
                            if (hadError) return;

                            // SSE data 行: "data: <json>"
                            if (line.rfind("data: ", 0) != 0) return;
                            std::string payload = line.substr(6);

                            if (payload == "[DONE]") {
                                acc.markDone();
                                return;
                            }

                            try {
                                auto delta = nlohmann::json::parse(payload);
                                // 检查是否为错误响应（非流式错误可能包裹在 delta 中）
                                if (delta.contains("error")) {
                                    hadError = true;
                                    errorMsg = delta["error"].dump();
                                    return;
                                }
                                // 提取 choices[0].delta
                                if (delta.contains("choices") && !delta["choices"].empty()) {
                                    const auto& choice = delta["choices"][0];
                                    if (choice.contains("delta")) {
                                        std::string chunk = acc.feedDelta(choice["delta"]);
                                        if (!chunk.empty()) {
                                            std::cout << chunk << std::flush;
                                        }
                                    }
                                    // finish_reason 也可能在 choices[0] 中
                                    if (choice.contains("finish_reason")) {
                                        acc.feedDelta(choice);
                                    }
                                }
                            } catch (const nlohmann::json::exception&) {
                                // 忽略无法解析的 SSE 行
                            }
                        });

                if (hadError) {
                    return std::string("[Error] Stream error: ") + errorMsg;
                }
                if (!response.m_error.empty()) {
                    return std::string("[Error] ") + response.m_error;
                }

                // 构造与非流式相同的 CLFAssistantResponse
                parsed.m_content      = acc.getContent();
                parsed.m_toolCalls    = acc.getToolCalls();
                parsed.m_finishReason = acc.getFinishReason();

            } else {
                // ====== 同步路径 ======
                CLF::CLFNetwork::CLFHttpResponse response =
                    m_httpClient.postJson("/v1/chat/completions", body);

                if (!response.m_error.empty()) {
                    return std::string("[Error] ") + response.m_error;
                }

                parsed = m_protocolAdapter.parseAssistantResponse(response.m_body);
            }

            // 检查 finish_reason
            if (!CLFProtocolAdapter::isValidFinish(parsed)) {
                return std::string("[Error] Unexpected finish_reason: ")
                       + parsed.m_finishReason;
            }

            // 累积文本内容
            if (!parsed.m_content.empty()) {
                if (!finalContent.empty()) {
                    finalContent += "\n";
                }
                finalContent += parsed.m_content;
            }

            // tool_calls → 执行并继续循环
            if (CLFProtocolAdapter::hasToolCalls(parsed)) {
                m_context.addAssistantToolCalls(parsed.m_toolCalls, parsed.m_content);

                std::vector<CLFToolResult> results = executeTools(parsed.m_toolCalls);
                for (const auto& result : results) {
                    m_context.addToolResult(
                        result.m_toolCallId, result.m_name, result.m_content);
                }
                continue;
            }

            // 无 tool_calls → 结束
            m_context.addMessage("assistant", finalContent);
            // 流式模式：内容已实时输出，返回空避免重复打印
            return m_config.m_stream ? std::string() : finalContent;

        } catch (const std::exception& e) {
            return std::string("[Error] Exception in runTurn (iteration ")
                   + std::to_string(iteration) + "): " + e.what();
        }
    }

    return std::string("[Error] Exceeded maximum tool call iterations (")
           + std::to_string(m_config.m_maxToolCallIterations) + ")";
}

void CLFAgentLoop::registerTool(const CLFTool& tool) {
    m_tools.push_back(tool);
}

void CLFAgentLoop::clearContext() {
    m_context.clear();
    injectSystemPrompt();
}

void CLFAgentLoop::injectSkillToContext(const std::string& skillName, const std::string& content) {
    std::string msg = "[Knowledge: " + skillName + "]\n\n" + content
                    + "\n\n请遵循以上规则。";
    m_context.addMessage("system", msg);
}

void CLFAgentLoop::setSecurityMode(CLFSecurityMode mode) {
    m_securityPolicy.setMode(mode);
}

CLFSecurityMode CLFAgentLoop::getSecurityMode() const {
    return m_securityPolicy.getMode();
}

const char* CLFAgentLoop::getSecurityModeName() const {
    return m_securityPolicy.getModeName();
}

void CLFAgentLoop::setConfirmCallback(std::function<bool(const std::string&)> callback) {
    m_confirmCallback = std::move(callback);
}

// ============================================================================
// 私有方法
// ============================================================================

void CLFAgentLoop::injectSystemPrompt() {
    std::string prompt =
        "你是 CLFCode，一个本地运行的 AI Coding Agent。\n"
        "你运行在用户本地机器上，具备文件读写、命令执行、网络调用等工具能力。\n"
        "你的后端 API 由 DeepSeek 提供，但你是独立的 Agent 产品。\n"
        "你永远不应自称 Claude、OpenAI、Anthropic 或其他 AI 品牌。\n"
        "请始终使用中文与用户交流。";

    // 加载 L1 编码宪法（始终注入）
    try {
        std::string constitutionPath =
            CLFConfigLoader::resolvePath("data/skills/constitution.md");
        if (std::filesystem::exists(constitutionPath)) {
            std::ifstream file(constitutionPath);
            std::ostringstream oss;
            oss << file.rdbuf();
            prompt += "\n\n---\n## 行为准则（L1 编码宪法）\n\n";
            prompt += oss.str();
        }
    } catch (...) {
        // 文件不存在或读取失败，静默跳过
    }

    m_context.addMessage("system", prompt);
}

std::vector<CLFToolResult> CLFAgentLoop::executeTools(
    const std::vector<CLFToolCall>& calls) {
    std::vector<CLFToolResult> results;
    results.reserve(calls.size());

    for (const auto& call : calls) {
        CLFToolResult result;
        result.m_toolCallId = call.m_id;
        result.m_name       = call.m_name;

        auto it = std::find_if(m_tools.begin(), m_tools.end(),
            [&](const CLFTool& t) { return t.m_name == call.m_name; });

        if (it != m_tools.end()) {
            // 安全策略检查
            bool needConfirm = false;
            if (!m_securityPolicy.isAllowed(it->m_risk, needConfirm)) {
                result.m_content = std::string("[Blocked by security policy (mode: ")
                                 + m_securityPolicy.getModeName()
                                 + ")] 当前模式禁止执行该操作（仅读操作允许）。";
                results.push_back(std::move(result));
                continue;
            }

            // 需用户确认
            if (needConfirm && m_confirmCallback) {
                std::string prompt = "工具 [" + call.m_name + "] 需要执行高风险操作。\n"
                                   + "参数: " + call.m_arguments;
                if (!m_confirmCallback(prompt)) {
                    result.m_content = "[Denied by user] 用户拒绝了该操作。";
                    results.push_back(std::move(result));
                    continue;
                }
            }

            try {
                result.m_content = it->m_handler(call.m_arguments);
            } catch (const std::exception& e) {
                result.m_content = std::string("Tool execution error: ") + e.what();
            }
        } else {
            result.m_content = std::string("Tool not found: ") + call.m_name;
        }

        results.push_back(std::move(result));
    }

    return results;
}

} // namespace CLF::CLFCore
