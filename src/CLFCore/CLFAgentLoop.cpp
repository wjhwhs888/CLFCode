// CLFAgentLoop.cpp — Agent 主循环实现（tool-calling 编排 + 流式/同步双路径）

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFRetryPolicy.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"
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

// 按 \n 拆分为多行追加（折叠块渲染按行处理）
void appendSplitLines(std::vector<std::string>& out, const std::string& text) {
    if (text.empty()) { out.emplace_back(); return; }
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            out.push_back(text.substr(pos));
            break;
        }
        out.push_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
}

} // anonymous namespace

CLFAgentLoop::CLFAgentLoop(const CLFAgentConfig& config,
                           std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> httpClient,
                           const CLFTimerLabels& labels)
    : m_config(config)
    , m_labels(labels)
    , m_context(config.m_maxContextWindow)
    , m_httpClient(httpClient ? httpClient
                              : std::make_shared<CLF::CLFNetwork::CLFHttpClient>(
                                    config.m_apiBaseUrl, config.m_apiKey))
    , m_securityPolicy(CLFSecurityPolicy::modeFromString(config.m_securityMode))
    , m_summarizer(std::make_unique<CLFSessionSummarizer>(m_httpClient, m_config)) {
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
    CLFLogger::instance().info("[Turn] start, input="
        + (userInput.size() > 80 ? userInput.substr(0, 80) + "..." : userInput));
    m_interrupted = false;  // 新 turn 开始时重置中断标志
    if (m_output) m_output->clearThinking();  // 清空上一轮推理内容
    m_lastReasoningSize = 0;  // 重置推理增量追踪
    m_context.addMessage("user", userInput);
    m_lastToolStats = {};
    // P1-1: 状态点接线——Running 于 turn 开始
    if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Running);

    // Timer #2：StatusLine 持续计时
    auto turnStart = std::chrono::steady_clock::now();
    std::atomic<bool> turnTimerOn{true};
    std::thread turnTimer([&]() {
        while (turnTimerOn.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!turnTimerOn.load(std::memory_order_relaxed)) break;
            if (!m_output) continue;
            auto s = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - turnStart).count();
            // P1-1: 计时文本 ≥15s 才显示（dsh 规则，降低短任务噪声）
            if (s >= 15)
                m_output->setStatusTextOnly(m_labels.working + " for " + std::to_string(static_cast<int>(s)) + "s…");
            // F13: 1Hz 驱动——工具执行期无流式事件，靠此修复界面冻结 + 动画最低帧率
            m_output->requestRefresh();
        }
    });
    struct TurnGuard {
        std::atomic<bool>& on;
        std::thread& t;
        CLF::CLFTypes::ICLFOutput* out;
        ~TurnGuard() {
            on.store(false, std::memory_order_relaxed);
            if (t.joinable()) t.join(); // 必须 join，detach 会悬空引用
            if (out) out->setStatus("");
        }
    } turnGuard{turnTimerOn, turnTimer, m_output};

    std::string finalContent;
    int consecutiveErrors = 0;

    for (int iteration = 0; iteration < m_config.m_maxToolCallIterations; ++iteration) {
        // Timer #1：只计数（不调用 output，安全）
        std::atomic<bool> thinkingActive{true};
        std::atomic<int> thinkingSec{0};
        std::thread thinkingTimer([&]() {
            while (thinkingActive.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (!thinkingActive.load(std::memory_order_relaxed)) break;
                thinkingSec.fetch_add(1);
            }
        });
        struct ThinkGuard {
            std::atomic<bool>& active;
            std::thread& t;
            ~ThinkGuard() { active.store(false); if (t.joinable()) t.join(); }
        } thinkGuard{thinkingActive, thinkingTimer};
        // ① 每次循环迭代前检查中断
        if (m_interrupted) {
            emitInterrupted();
            return std::string("[Interrupted]");
        }
        try {
            std::string body = m_protocolAdapter.buildChatRequest(
                m_context.getMessages(), m_tools, m_config);

            CLFAssistantResponse parsed;

            if (m_config.m_stream) {
                // ====== 流式路径 ======
                CLFLogger::instance().info("[API] streaming request, iter="
                    + std::to_string(iteration) + ", ctx_msgs="
                    + std::to_string(m_context.getMessages().size()));
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
                                            if (m_output) m_output->emitContent(chunk);
                                        }
                                        // 推理过程 → 独立通道（UI 层 Ctrl+O 折叠/展开）
                                        // 只追加上次检查后新增的推理内容（避免重复累积）
                                        if (acc.hasReasoning() && m_output) {
                                            auto& reasoning = acc.getReasoning();
                                            if (reasoning.size() > m_lastReasoningSize) {
                                                m_output->appendThinking(
                                                    reasoning.substr(m_lastReasoningSize));
                                                m_lastReasoningSize = reasoning.size();
                                            }
                                        }
                                    }
                                    if (choice.contains("finish_reason")) acc.feedDelta(choice);
                                }
                            } catch (const nlohmann::json::exception&) {}
                        });

                // 错误处理
                if (hadError) {
                    if (m_output) m_output->emitError("Stream error: " + errorMsg);
                    if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Error);
                    return std::string("[Error] ") + errorMsg;
                }
                if (interrupted || m_interrupted) {
                    emitInterrupted();
                    return std::string("[Interrupted]");
                }
                if (response.m_wasAborted) {
                    // libcurl 层检测到中断 → 直接返回，不重试
                    emitInterrupted();
                    return std::string("[Interrupted]");
                }
                if (!response.m_error.empty()) {
                    if (CLFRetryPolicy::isFatalHttpError(response.m_error)) {
                        if (m_output) m_output->emitError(response.m_error);
                        if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Error);
                        return std::string("[Error] ") + response.m_error;
                    }
                    if (++consecutiveErrors >= CLFRetryPolicy::kMaxRetries) {
                        if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Error);
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
                        emitInterrupted();
                        return std::string("[Interrupted]");
                    }
                    continue;
                }

                consecutiveErrors = 0;
                acc.markDone();
                CLFLogger::instance().info("[API] streaming done, content="
                    + std::to_string(acc.getContent().size()) + "chars, reasoning="
                    + std::to_string(acc.getReasoning().size()) + "chars, finish="
                    + acc.getFinishReason());
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
                    emitInterrupted();
                    return std::string("[Interrupted]");
                }
                if (response.m_wasAborted) {
                    emitInterrupted();
                    return std::string("[Interrupted]");
                }
                if (!response.m_error.empty()) {
                    if (CLFRetryPolicy::isFatalHttpError(response.m_error)) {
                        if (m_output) m_output->emitError(response.m_error);
                        if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Error);
                        return std::string("[Error] ") + response.m_error;
                    }
                    if (++consecutiveErrors >= CLFRetryPolicy::kMaxRetries) {
                        if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Error);
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
                        emitInterrupted();
                        return std::string("[Interrupted]");
                    }
                    continue;
                }

                consecutiveErrors = 0;
                parsed = m_protocolAdapter.parseAssistantResponse(response.m_body);
            }

            // finish_reason 检查
            if (!CLFProtocolAdapter::isValidFinish(parsed)) {
                if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Error);
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
                // 中断检查：流式被 abort 后可能累积了 tool call，执行前再检查
                if (m_interrupted) {
                    emitInterrupted();
                    return std::string("[Interrupted]");
                }
                m_context.addAssistantToolCalls(parsed.m_toolCalls, parsed.m_content);
                CLFToolExecutor executor(m_tools, m_securityPolicy,
                                         m_confirmCallback, m_lastToolStats, m_output,
                                         &m_interrupted, &m_labels, &thinkingSec);
                auto results = executor.execute(parsed.m_toolCalls);
                for (const auto& result : results) {
                    m_context.addToolResult(
                        result.m_toolCallId, result.m_name, result.m_content);
                }
                if (m_interrupted) {
                    emitInterrupted();
                    return std::string("[Interrupted]");
                }
                continue;
            }

            // 完成
            {
                auto s = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - turnStart).count();
                std::string worked = "\n \n✻ " + m_labels.worked + " for " + formatDurationSeconds(s) + "\n \n";
                finalContent += worked;
                if (m_output && m_config.m_stream)
                    m_output->emitContent(worked);  // stream 路径需显式 emit
            }
            m_context.addMessage("assistant", finalContent);
            // P1-1: 正常完成点——Done 显式接线（TurnGuard 不设 kind，F20）
            if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Done);
            CLFLogger::instance().info("[Turn] done, content="
                + std::to_string(finalContent.size()) + "chars, tools="
                + std::to_string(m_lastToolStats.totalCalls));
            return m_config.m_stream ? std::string() : finalContent;

        } catch (const std::exception& e) {
            if (++consecutiveErrors >= CLFRetryPolicy::kMaxRetries) {
                if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Error);
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

    {
        auto s = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - turnStart).count();
        finalContent += "\n \n✻ " + m_labels.worked + " for " + formatDurationSeconds(s) + "\n \n";
    }
    // P1-1: 迭代上限——任务未完成语义，Warn（对齐 dsh max-tokens=warning）
    if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Warn);
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
    (void)content;  // content 已由 CLFSkillLoader 缓存，buildSystemPromptContext() 从中读取
    // 去重：已注入则跳过
    if (std::find(m_loadedSkills.begin(), m_loadedSkills.end(), skillName) != m_loadedSkills.end()) {
        return;
    }
    m_loadedSkills.push_back(skillName);
    // 重建合并后的 system 消息
    rebuildSystemMessage();
}

std::vector<std::string> CLFAgentLoop::getLoadedSkills() const {
    return m_loadedSkills;
}

void CLFAgentLoop::setSecurityMode(CLFSecurityMode mode) {
    m_securityPolicy.setMode(mode);
}

const char* CLFAgentLoop::getSecurityModeName() const {
    return m_securityPolicy.getModeName();
}

void CLFAgentLoop::setConfirmCallback(std::function<bool(const std::string&)> callback) {
    m_confirmCallback = std::move(callback);
}

std::string CLFAgentLoop::saveSession(const std::string& dirPath, bool finalize) const {
    const auto& msgs = m_context.getMessages();
    if (msgs.empty()) {
        CLFLogger::instance().debug("[Save] skipped: empty context");
        return "";
    }

    CLFLogger::instance().debug(
        "[Save] finalize=" + std::string(finalize ? "true" : "false") +
        ", msgs=" + std::to_string(msgs.size()) +
        ", skills=" + std::to_string(m_loadedSkills.size()) +
        ", summaryCached=" + std::string(m_cachedSummary.m_valid ? "yes" : "no"));

    std::string path = CLFSessionManager::save(
        msgs, dirPath, finalize, m_loadedSkills,
        m_cachedSummary.m_valid ? &m_cachedSummary : nullptr);
    if (path.empty()) {
        CLFLogger::instance().warn("[Save] saveSession failed, finalize="
                                   + std::string(finalize ? "true" : "false"));
    }
    return path;
}

void CLFAgentLoop::generateAndCacheSummary() {
    if (!m_config.m_contextCompression) return;
    m_cachedSummary = m_summarizer->generate(m_context.getMessages());
}

bool CLFAgentLoop::restoreSession(const std::string& filePath) {
    CLFLogger::instance().info("[Restore] loading: " + filePath);

    std::vector<CLFMessage> messages;
    std::vector<std::string> skills;
    CLFSessionSummary summary;
    if (!CLFSessionManager::load(filePath, messages, &skills, &summary)) return false;

    CLFLogger::instance().info("[Restore] loaded: "
                               + std::to_string(messages.size()) + " msgs, "
                               + std::to_string(skills.size()) + " skills, "
                               + "summary=" + std::string(summary.m_valid
                                   ? "yes(" + summary.m_method + ")" : "no"));

    m_context.clear();

    // ① 回显历史到终端（P2-1：折叠块，不再直灌滚动区；内容全量传入，
    //    messages 已驻留 m_context，回显只是显示投影——R1 裁决不做懒加载）
    if (m_output) {
        std::vector<std::string> echoLines;
        int userCount = 0, assistantCount = 0;
        for (const auto& msg : messages) {
            if (msg.m_role == "user") {
                appendSplitLines(echoLines, "> " + msg.m_content);
                ++userCount;
            } else if (msg.m_role == "assistant" && !msg.m_content.empty()) {
                appendSplitLines(echoLines, msg.m_content);
                ++assistantCount;
            }
            // tool / system 消息跳过（终端不需要显示）
        }
        m_output->showFoldedBlock(
            "● 会话已恢复 · " + std::to_string(userCount + assistantCount)
                + " 条消息（ctrl+r 展开）", echoLines);
        CLFLogger::instance().debug("[Restore] folded echo "
                                    + std::to_string(userCount) + " user + "
                                    + std::to_string(assistantCount) + " assistant messages");
    }

    // ② 恢复非 system 消息（全部跳过 system，由后续步骤重建）
    for (const auto& msg : messages) {
        if (msg.m_role == "system") continue;
        m_context.appendMessage(msg);
    }
    injectSystemPrompt();

    // ③ 注入会话摘要（作为 system 消息，利用 system 永不截断特性）
    if (!summary.isEmpty()) {
        m_context.addMessage("system", summary.toSystemMessage());
    }

    // ④ 重新注入知识库（从文件系统加载最新版本）
    m_loadedSkills.clear();
    for (const auto& name : skills) {
        std::string content = CLFSkillLoader::getContent(name);
        if (!content.empty()) {
            injectSkillToContext(name, content);
            CLFLogger::instance().info("[Restore] skill '" + name + "': re-injected");
        } else {
            CLFLogger::instance().info("[Restore] skill '" + name + "': SKIPPED (file missing)");
        }
    }

    return true;
}

// ============================================================================
// 私有方法
// ============================================================================

void CLFAgentLoop::emitInterrupted() {
    // P0-5: 统一文案（原 9 处两版文案）+ clearThinking + Warn 状态点
    // 所有调用点后立即 return——一轮内不可重复发射（F17 裁决）
    if (!m_output) return;
    m_output->emitContent("\n⏹ 已中断\n");
    m_output->clearThinking();
    m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Warn);
}

void CLFAgentLoop::injectSystemPrompt() {
    auto ctx = buildSystemPromptContext();
    m_context.setSystemPrompt(CLFSystemPromptBuilder::build(ctx));
}

CLFSystemPromptBuilder::Context CLFAgentLoop::buildSystemPromptContext() const {
    CLFSystemPromptBuilder::Context ctx;
    ctx.workspaceRoot       = std::filesystem::current_path().string();
    ctx.interactionLanguage = m_config.m_interactionLanguage;
    ctx.modelName           = m_config.m_modelName;
    ctx.maxContextWindow    = m_config.m_maxContextWindow;
    for (const auto& name : m_loadedSkills) {
        std::string content = CLFSkillLoader::getContent(name);
        if (!content.empty()) ctx.skills.emplace_back(name, content);
    }
    return ctx;
}

void CLFAgentLoop::rebuildSystemMessage() {
    auto ctx = buildSystemPromptContext();
    m_context.setSystemPrompt(CLFSystemPromptBuilder::build(ctx));
}

} // namespace CLF::CLFCore
