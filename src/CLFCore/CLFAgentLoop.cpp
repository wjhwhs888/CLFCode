// CLFAgentLoop.cpp — Agent 主循环实现（tool-calling 编排 + 流式/同步双路径）

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFTypes/CLFEvent.hpp"
#include "CLFTypes/CLFEventQueue.hpp"
#include "CLFCore/CLFRetryPolicy.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFStreamAccumulator.hpp"
#include "CLFNetwork/CLFThinkingIndicator.hpp"
#include "CLFCore/CLFToolExecutor.hpp"
#include "CLFNetwork/CLFHttpClient.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

namespace CLF::CLFCore {

namespace {
// 推送事件到队列 + 通过输出接口渲染
void emitContent(CLFEventQueue* q, CLF::CLFTypes::ICLFOutput* out, const std::string& text) {
    if (q) { Event e; e.type = EventType::ContentAppend; e.text = text; q->push(e); }
    if (out) out->emitContent(text);
}
void emitNewline(CLFEventQueue* q, CLF::CLFTypes::ICLFOutput* out) {
    if (q) { Event e; e.type = EventType::ContentNewline; q->push(e); }
    if (out) out->emitContent("\n");
}
} // anonymous namespace

CLFAgentLoop::CLFAgentLoop(const CLFAgentConfig& config,
                           std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> httpClient)
    : m_config(config)
    , m_context(config.m_maxContextWindow)
    , m_httpClient(httpClient ? httpClient
                              : std::make_shared<CLF::CLFNetwork::CLFHttpClient>(
                                    config.m_apiBaseUrl, config.m_apiKey))
    , m_securityPolicy(CLFSecurityPolicy::modeFromString(config.m_securityMode)) {
    m_httpClient->setTimeout(config.m_maxResponseDelaySec);
    injectSystemPrompt();
}

CLFAgentLoop::~CLFAgentLoop() {
    if (m_output) m_output->onInterrupt(nullptr);  // 清空回调, 防止悬空指针
}

void CLFAgentLoop::setOutput(CLF::CLFTypes::ICLFOutput* output) {
    m_output = output;
    m_output->onInterrupt([this]() {
        m_interrupted = true;
        if (m_httpClient) m_httpClient->abort();
    });
}

std::string CLFAgentLoop::runTurn(const std::string& userInput) {
    m_context.addMessage("user", userInput);
    m_lastToolStats = {};

    std::string finalContent;
    int consecutiveErrors = 0;

    for (int iteration = 0; iteration < m_config.m_maxToolCallIterations; ++iteration) {
        // ① 每次循环迭代前检查中断
        if (m_interrupted) {
            if (m_output) m_output->emitContent("\n⏹ 已中断\n");
            return std::string("[Interrupted]");
        }
        try {
            std::string body = m_protocolAdapter.buildChatRequest(
                m_context.getMessages(), m_tools, m_config);

            CLFAssistantResponse parsed;

            if (m_config.m_stream) {
                // ====== 流式路径 ======
                CLFStreamAccumulator acc;
                CLF::CLFNetwork::CLFThinkingIndicator thinking(m_httpClient.get(), m_output);
                bool firstContent = true;
                bool interrupted = false;
                bool hadError = false;
                std::string errorMsg;

                CLF::CLFNetwork::CLFHttpResponse response =
                    m_httpClient->postJsonStream(
                        "/v1/chat/completions", body,
                        [&](const std::string& line) {
                            if (hadError || interrupted) return;
                            if (m_interrupted) {
                                interrupted = true;
                                acc.markDone();
                                return;
                            }
                            if (line.rfind("data: ", 0) != 0) return;
                            std::string payload = line.substr(6);
                            if (payload == "[DONE]") { acc.markDone(); return; }
                            try {
                                auto delta = nlohmann::json::parse(payload);
                                if (delta.contains("error")) { hadError = true; errorMsg = delta["error"].dump(); return; }
                                if (delta.contains("choices") && !delta["choices"].empty()) {
                                    const auto& choice = delta["choices"][0];
                                    if (choice.contains("delta")) {
                                        std::string chunk = acc.feedDelta(choice["delta"]);
                                        if (!chunk.empty()) {
                                            if (firstContent) { thinking.stop(); firstContent = false; }
                                            emitContent(m_eventQueue, m_output, chunk);
                                        }
                                    }
                                    if (choice.contains("finish_reason")) acc.feedDelta(choice);
                                }
                            } catch (const nlohmann::json::exception&) {}
                        });

                // 错误处理
                if (hadError) {
                    if (m_output) m_output->emitError("Stream error: " + errorMsg);
                    return std::string("[Error] ") + errorMsg;
                }
                if (interrupted) {
                    if (m_output) m_output->emitContent("\n⏹ 已中断\n");
                    return std::string("[Interrupted]");
                }
                if (!response.m_error.empty()) {
                    if (CLFRetryPolicy::isFatalHttpError(response.m_error)) {
                        if (m_output) m_output->emitError(response.m_error);
                        return std::string("[Error] ") + response.m_error;
                    }
                    if (++consecutiveErrors >= CLFRetryPolicy::kMaxRetries) {
                        return std::string("[Error] Too many errors: ") + response.m_error;
                    }
                    if (m_output) m_output->emitContent(
                        "\n⚠ " + response.m_error + " — retry "
                        + std::to_string(consecutiveErrors) + "/"
                        + std::to_string(CLFRetryPolicy::kMaxRetries) + "\n");
                    // ③ 可中断等待 (每 100ms 检查一次)
                    for (int w = 0; w < 20 * consecutiveErrors && !m_interrupted; ++w)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (m_interrupted) {
                        if (m_output) m_output->emitContent("⏹ 已中断\n");
                        return std::string("[Interrupted]");
                    }
                    continue;
                }

                consecutiveErrors = 0;
                acc.markDone();
                parsed.m_content      = acc.getContent();
                parsed.m_toolCalls    = acc.getToolCalls();
                parsed.m_finishReason = acc.getFinishReason();

            } else {
                // ====== 同步路径 ======
                CLF::CLFNetwork::CLFThinkingIndicator thinking(m_httpClient.get(), m_output);
                CLF::CLFNetwork::CLFHttpResponse response =
                    m_httpClient->postJson("/v1/chat/completions", body);
                thinking.stop();

                if (m_interrupted) {
                    if (m_output) m_output->emitContent("\n⏹ 已中断\n");
                    return std::string("[Interrupted]");
                }
                if (!response.m_error.empty()) {
                    if (CLFRetryPolicy::isFatalHttpError(response.m_error)) {
                        if (m_output) m_output->emitError(response.m_error);
                        return std::string("[Error] ") + response.m_error;
                    }
                    if (++consecutiveErrors >= CLFRetryPolicy::kMaxRetries) {
                        return std::string("[Error] Too many errors: ") + response.m_error;
                    }
                    if (m_output) m_output->emitContent(
                        "\n⚠ " + response.m_error + " — retry "
                        + std::to_string(consecutiveErrors) + "/"
                        + std::to_string(CLFRetryPolicy::kMaxRetries) + "\n");
                    // ③ 可中断等待 (每 100ms 检查一次)
                    for (int w = 0; w < 20 * consecutiveErrors && !m_interrupted; ++w)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (m_interrupted) {
                        if (m_output) m_output->emitContent("⏹ 已中断\n");
                        return std::string("[Interrupted]");
                    }
                    continue;
                }

                consecutiveErrors = 0;
                parsed = m_protocolAdapter.parseAssistantResponse(response.m_body);
            }

            // finish_reason 检查
            if (!CLFProtocolAdapter::isValidFinish(parsed)) {
                return std::string("[Error] Unexpected finish_reason: '")
                       + parsed.m_finishReason + "'";
            }

            // 累积文本（思考模型可能在 reasoning_content 中耗尽 token，
            // content 为空但 finish_reason 为 "length" 或 "stop"）
            if (!parsed.m_content.empty()) {
                if (!finalContent.empty()) finalContent += "\n";
                finalContent += parsed.m_content;
            }
            if (finalContent.empty() && parsed.m_finishReason == "length") {
                finalContent = "(响应被截断 — 思考时间过长，token 已达上限)";
            }

            // tool_calls → 执行并继续循环
            if (CLFProtocolAdapter::hasToolCalls(parsed)) {
                m_context.addAssistantToolCalls(parsed.m_toolCalls, parsed.m_content);
                CLFToolExecutor executor(m_tools, m_securityPolicy,
                                         m_confirmCallback, m_lastToolStats);
                auto results = executor.execute(parsed.m_toolCalls);
                for (const auto& result : results) {
                    m_context.addToolResult(
                        result.m_toolCallId, result.m_name, result.m_content);
                }
                // ② 工具执行后检查中断 (工具可能耗时数秒)
                if (m_interrupted) {
                    if (m_output) m_output->emitContent("\n⏹ 已中断\n");
                    return std::string("[Interrupted]");
                }
                continue;
            }

            // 完成
            m_context.addMessage("assistant", finalContent);
            return m_config.m_stream ? std::string() : finalContent;

        } catch (const std::exception& e) {
            if (++consecutiveErrors >= CLFRetryPolicy::kMaxRetries) {
                return std::string("[Error] Exception: ") + e.what();
            }
            if (m_output) m_output->emitContent(
                "\n✗ Exception: " + std::string(e.what())
                + " — retry " + std::to_string(consecutiveErrors)
                + "/" + std::to_string(CLFRetryPolicy::kMaxRetries) + "\n");
            // ④ 可中断等待
            for (int w = 0; w < 20 && !m_interrupted; ++w)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    return std::string("[Error] Exceeded maximum tool call iterations (")
           + std::to_string(m_config.m_maxToolCallIterations) + ")";
}

// ============================================================================
// 工具 / 上下文 / Skill / 安全模式 / 会话 — 门面方法
// ============================================================================

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
    m_loadedSkills.push_back(skillName);
}

std::vector<std::string> CLFAgentLoop::getLoadedSkills() const {
    return m_loadedSkills;
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

void CLFAgentLoop::setStatusCallback(
    std::function<void(const std::string&, const std::string&)> callback) {
    m_statusCallback = std::move(callback);
}

std::string CLFAgentLoop::saveSession(const std::string& dirPath, bool incomplete) const {
    return CLFSessionManager::save(m_context.getMessages(), dirPath, incomplete);
}

bool CLFAgentLoop::restoreSession(const std::string& filePath) {
    std::vector<CLFMessage> messages;
    if (!CLFSessionManager::load(filePath, messages)) return false;

    m_context.clear();
    for (const auto& msg : messages) {
        if (msg.m_role == "system") continue;
        m_context.appendMessage(msg);
    }
    injectSystemPrompt();
    return true;
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
        "请始终使用中文与用户交流。\n"
        "\n"
        "## 运行环境\n"
        "- 你运行在 **Windows** 上，必须使用 Windows 命令。\n"
        "- 目录列表用 `dir`，查看文件用 `type`，搜索文本用 `findstr`。\n"
        "- **禁止**使用 Linux 命令：ls、pwd、cat、grep、find、head、tail、iconv。\n"
        "- 中文 Windows 的命令输出可能是 GBK 编码，遇到乱码先执行 `chcp 65001`。\n"
        "\n"
        "## 文件管理规则\n"
        "- 任务中创建的临时文件（备份、中间输出等），任务结束前必须清理。\n"
        "- 优先复用已有文件，避免重复创建备份。\n"
        "- 尽量用重定向/管道而非落盘中间文件。";

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
    } catch (...) {}

    m_context.addMessage("system", prompt);
}

} // namespace CLF::CLFCore
