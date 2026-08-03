// CLFAgentLoop.cpp — Agent 主循环实现（含 tool-calling 循环 + 流式响应）

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFConsole.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFTerminal.hpp"
#include "CLFNetwork/CLFHttpClient.hpp" // CLFHttpClient 具体类（构造注入默认实现）

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "CLFCore/CLFStreamAccumulator.hpp"

namespace CLF::CLFCore {

// ============================================================================
// ThinkingIndicator — 等待 API 响应时显示 "· Thinking… (Ns)" + ESC 中断检测
// ============================================================================
class ThinkingIndicator {
public:
    ThinkingIndicator(CLF::CLFNetwork::ICLFHttpClient* http = nullptr)
        : m_done(false), m_http(http) {
        m_start = std::chrono::steady_clock::now();
        m_thread = std::thread([this]() {
            while (!m_done.load(std::memory_order_relaxed)) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - m_start).count();
                std::string msg = "Thinking… (" + std::to_string(elapsed) + "s)";
                std::cout << "\r· " << CLFTerminal::gray(msg) << "\033[K" << std::flush;

                // 每秒检测一次 ESC，按下则中止 HTTP 请求
                if (m_http && CLFConsole::checkEscape()) {
                    m_escPressed = true;
                    m_http->abort();
                    break;
                }

                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            std::cout << "\r\033[K" << std::flush;
        });
    }

    ~ThinkingIndicator() { stop(); }

    void stop() {
        if (!m_done.exchange(true, std::memory_order_relaxed)) {
            if (m_thread.joinable()) m_thread.join();
        }
    }

    bool escPressed() const { return m_escPressed; }

    ThinkingIndicator(const ThinkingIndicator&) = delete;
    ThinkingIndicator& operator=(const ThinkingIndicator&) = delete;

private:
    std::atomic<bool> m_done;
    std::atomic<bool> m_escPressed{false};
    CLF::CLFNetwork::ICLFHttpClient* m_http;
    std::chrono::steady_clock::time_point m_start;
    std::thread m_thread;
};

CLFAgentLoop::CLFAgentLoop(const CLFAgentConfig& config,
                           std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> httpClient)
    : m_config(config)
    , m_context(config.m_maxContextWindow)
    , m_httpClient(httpClient ? httpClient
                              : std::make_shared<CLF::CLFNetwork::CLFHttpClient>(
                                    config.m_apiBaseUrl, config.m_apiKey))
    , m_securityPolicy(CLFSecurityPolicy::modeFromString(config.m_securityMode)) {
    // 应用回复超时配置（默认 300 秒）
    m_httpClient->setTimeout(config.m_maxResponseDelaySec);
    injectSystemPrompt();
}

// 判断错误是否致命（不应重试）
static bool isFatalHttpError(const std::string& err) {
    // 400 = 请求参数错误   401 = 认证失败   402/403 = 权限不足
    return err.find("HTTP 400") != std::string::npos
        || err.find("HTTP 401") != std::string::npos
        || err.find("HTTP 402") != std::string::npos
        || err.find("HTTP 403") != std::string::npos;
}

// 判断错误是否可重试（限流 / 服务端错误 / 超时）
static bool isRetryableError(const std::string& err) {
    return err.find("HTTP 429") != std::string::npos
        || err.find("HTTP 5")   != std::string::npos  // 500-599
        || err.find("Connection failed") != std::string::npos
        || err.find("timeout")  != std::string::npos
        || err.find("Timeout")  != std::string::npos;
}

std::string CLFAgentLoop::runTurn(const std::string& userInput) {
    m_context.addMessage("user", userInput);

    // 重置本轮工具统计
    m_lastToolStats.searchCount = 0;
    m_lastToolStats.readCount   = 0;

    std::string finalContent;
    int consecutiveErrors = 0;
    constexpr int kMaxErrors = 3;  // 连续错误上限

    for (int iteration = 0; iteration < m_config.m_maxToolCallIterations; ++iteration) {
        try {
            std::string body = m_protocolAdapter.buildChatRequest(
                m_context.getMessages(), m_tools, m_config);

            CLFAssistantResponse parsed;

            if (m_config.m_stream) {
                // ====== 流式路径 ======
                CLFStreamAccumulator acc;
                bool hadError = false;
                bool interrupted = false;
                std::string errorMsg;
                ThinkingIndicator thinking(m_httpClient.get());
                bool firstContent = true;

                CLF::CLFNetwork::CLFHttpResponse response =
                    m_httpClient->postJsonStream(
                        "/v1/chat/completions", body,
                        [&](const std::string& line) {
                            if (hadError || interrupted) return;

                            if (CLFConsole::checkEscape()) {
                                interrupted = true;
                                acc.markDone();
                                return;
                            }

                            if (line.rfind("data: ", 0) != 0) return;
                            std::string payload = line.substr(6);

                            if (payload == "[DONE]") {
                                acc.markDone();
                                return;
                            }

                            try {
                                auto delta = nlohmann::json::parse(payload);
                                if (delta.contains("error")) {
                                    hadError = true;
                                    errorMsg = delta["error"].dump();
                                    return;
                                }
                                if (delta.contains("choices") && !delta["choices"].empty()) {
                                    const auto& choice = delta["choices"][0];
                                    if (choice.contains("delta")) {
                                        std::string chunk = acc.feedDelta(choice["delta"]);
                                        if (!chunk.empty()) {
                                            if (firstContent) {
                                                thinking.stop();
                                                firstContent = false;
                                            }
                                            CLFTerminal::scrollPrint(chunk);
                                        }
                                    }
                                    if (choice.contains("finish_reason")) {
                                        acc.feedDelta(choice);
                                    }
                                }
                            } catch (const nlohmann::json::exception&) {}
                        });

                // 流式错误处理
                if (hadError) {
                    CLFTerminal::scrollPrint(
                        CLFTerminal::red("\n✗ Stream error: ") + errorMsg + "\n");
                    return std::string("[Error] ") + errorMsg;
                }
                if (interrupted || thinking.escPressed()) {
                    CLFTerminal::scrollPrint(CLFTerminal::yellow("\n⏹ 已中断") + "\n");
                    return std::string("[Interrupted]");
                }
                if (!response.m_error.empty()) {
                    if (isFatalHttpError(response.m_error)) {
                        CLFTerminal::scrollPrint(
                            CLFTerminal::red("\n✗ ") + response.m_error + "\n");
                        return std::string("[Error] ") + response.m_error;
                    }
                    // 可重试错误
                    ++consecutiveErrors;
                    CLFTerminal::scrollPrint(
                        CLFTerminal::yellow("\n⚠ ") + response.m_error
                        + CLFTerminal::gray(" — retry " + std::to_string(consecutiveErrors)
                                            + "/" + std::to_string(kMaxErrors)) + "\n");
                    if (consecutiveErrors >= kMaxErrors) {
                        return std::string("[Error] Too many errors: ") + response.m_error;
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(2 * consecutiveErrors));
                    continue;
                }

                consecutiveErrors = 0;  // 成功，重置计数
                acc.markDone();
                parsed.m_content      = acc.getContent();
                parsed.m_toolCalls    = acc.getToolCalls();
                parsed.m_finishReason = acc.getFinishReason();

            } else {
                // ====== 同步路径 ======
                ThinkingIndicator thinking(m_httpClient.get());
                CLF::CLFNetwork::CLFHttpResponse response =
                    m_httpClient->postJson("/v1/chat/completions", body);
                thinking.stop();

                if (thinking.escPressed()) {
                    CLFTerminal::scrollPrint(CLFTerminal::yellow("\n⏹ 已中断") + "\n");
                    return std::string("[Interrupted]");
                }
                if (!response.m_error.empty()) {
                    if (isFatalHttpError(response.m_error)) {
                        CLFTerminal::scrollPrint(
                            CLFTerminal::red("\n✗ ") + response.m_error + "\n");
                        return std::string("[Error] ") + response.m_error;
                    }
                    ++consecutiveErrors;
                    CLFTerminal::scrollPrint(
                        CLFTerminal::yellow("\n⚠ ") + response.m_error
                        + CLFTerminal::gray(" — retry " + std::to_string(consecutiveErrors)
                                            + "/" + std::to_string(kMaxErrors)) + "\n");
                    if (consecutiveErrors >= kMaxErrors) {
                        return std::string("[Error] Too many errors: ") + response.m_error;
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(2 * consecutiveErrors));
                    continue;
                }

                consecutiveErrors = 0;
                parsed = m_protocolAdapter.parseAssistantResponse(response.m_body);
            }

            // 检查 finish_reason
            if (!CLFProtocolAdapter::isValidFinish(parsed)) {
                return std::string("[Error] Unexpected finish_reason: '")
                       + parsed.m_finishReason + "'";
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
            return m_config.m_stream ? std::string() : finalContent;

        } catch (const std::exception& e) {
            ++consecutiveErrors;
            CLFTerminal::scrollPrint(
                CLFTerminal::red("\n✗ Exception: ") + e.what()
                + CLFTerminal::gray(" — retry " + std::to_string(consecutiveErrors)
                                    + "/" + std::to_string(kMaxErrors)) + "\n");
            if (consecutiveErrors >= kMaxErrors) {
                return std::string("[Error] Exception: ") + e.what();
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
            // continue loop (retry)
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
    if (!CLFSessionManager::load(filePath, messages)) {
        return false;
    }

    m_context.clear();
    for (const auto& msg : messages) {
        if (msg.m_role == "system") continue; // 身份由 injectSystemPrompt 重新注入
        m_context.appendMessage(msg); // 保留全部字段（tool_calls 等）
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
    } catch (...) {
        // 文件不存在或读取失败，静默跳过
    }

    m_context.addMessage("system", prompt);
}

namespace {

// 从工具参数 JSON 中提取关键参数用于显示
// 优先级: path > command > pattern > 截断的完整 JSON
std::string extractKeyParam(const std::string& argsJson) {
    try {
        auto args = nlohmann::json::parse(argsJson);
        if (args.contains("path") && args["path"].is_string()) {
            std::string p = args["path"].get<std::string>();
            // 取文件名部分（太长时截头留尾）
            if (p.size() > 55) p = "..." + p.substr(p.size() - 52);
            return p;
        }
        if (args.contains("command") && args["command"].is_string()) {
            std::string cmd = args["command"].get<std::string>();
            // 去掉前导空白，截断
            while (!cmd.empty() && cmd.front() == ' ') cmd.erase(0, 1);
            if (cmd.size() > 55) cmd = cmd.substr(0, 52) + "...";
            return cmd;
        }
        if (args.contains("pattern") && args["pattern"].is_string()) {
            std::string p = args["pattern"].get<std::string>();
            if (p.size() > 55) p = p.substr(0, 52) + "...";
            return p;
        }
        if (args.contains("message") && args["message"].is_string()) {
            std::string m = args["message"].get<std::string>();
            if (m.size() > 55) m = m.substr(0, 52) + "...";
            return m;
        }
    } catch (...) {}
    // fallback：空对象 / 纯空格 / 太短的 JSON 不显示
    if (argsJson.empty() || argsJson == "{}" || argsJson == "[]") return "";
    if (argsJson.size() > 60) return argsJson.substr(0, 57) + "...";
    return argsJson;
}

// 格式化工具结果显示（解析 result JSON，比"N 字符"更有意义）
// 返回 {success, message}，由调用方决定 ✓/✗ 前缀
struct ToolResultDisplay {
    bool ok;
    std::string text;
};
ToolResultDisplay formatToolResult(const std::string& resultJson) {
    try {
        auto r = nlohmann::json::parse(resultJson);
        // exitCode 优先于 success，execute_command 失败时能显示 stderr
        if (r.contains("exitCode")) {
            int code = r["exitCode"].get<int>();
            if (code == 0) return {true, "ok"};
            std::string detail;
            if (r.contains("stderr") && !r["stderr"].get<std::string>().empty())
                detail = r["stderr"].get<std::string>();
            else if (r.contains("stdout") && !r["stdout"].get<std::string>().empty())
                detail = r["stdout"].get<std::string>();
            if (detail.size() > 80) detail = detail.substr(0, 77) + "...";
            return {false, detail.empty() ? ("exit " + std::to_string(code)) : detail};
        }
        bool success = r.value("success", true);
        if (!success) {
            std::string err = r.value("error", std::string("failed"));
            return {false, err};
        }
        if (r.contains("content") && r["content"].is_string()) {
            const auto& c = r["content"].get<std::string>();
            int lines = 1;
            for (char ch : c) if (ch == '\n') ++lines;
            return {true, std::to_string(lines) + " lines, " + std::to_string(c.size()) + " chars"};
        }
        if (r.contains("stdout") && r["stdout"].is_string()) {
            const auto& out = r["stdout"].get<std::string>();
            int lines = 1;
            for (char ch : out) if (ch == '\n') ++lines;
            return {true, std::to_string(lines) + " lines"};
        }
        return {true, std::to_string(resultJson.size()) + " chars"};
    } catch (...) {
        return {true, std::to_string(resultJson.size()) + " chars"};
    }
}

} // anonymous namespace

std::vector<CLFToolResult> CLFAgentLoop::executeTools(
    const std::vector<CLFToolCall>& calls) {
    std::vector<CLFToolResult> results;
    results.reserve(calls.size());

    // 统计（供 thoughtMark 使用，跨轮累积）
    int searchCount = m_lastToolStats.searchCount;
    int readCount   = m_lastToolStats.readCount;

    for (const auto& call : calls) {
        CLFToolResult result;
        result.m_toolCallId = call.m_id;
        result.m_name       = call.m_name;

        // 工具调用显示（Claude Code 风格）：一行标题，参数融入标题
        std::string keyParam = extractKeyParam(call.m_arguments);
        std::string header = "● " + CLFTerminal::cyan(call.m_name);
        if (!keyParam.empty()) {
            header += "(" + CLFTerminal::gray(keyParam) + ")";
        }
        CLFTerminal::scrollPrint("\n" + header + "\n");

        // 统计
        if (call.m_name == "search_content" || call.m_name == "search_file"
            || call.m_name.find("search") != std::string::npos) {
            ++searchCount;
        }
        if (call.m_name == "read_file"
            || call.m_name.find("read") != std::string::npos) {
            ++readCount;
        }

        auto it = std::find_if(m_tools.begin(), m_tools.end(),
            [&](const CLFTool& t) { return t.m_name == call.m_name; });

        if (it != m_tools.end()) {
            // 安全策略检查
            bool needConfirm = false;
            if (!m_securityPolicy.isAllowed(it->m_risk, needConfirm)) {
                result.m_content = std::string("[Blocked by security policy (mode: ")
                                 + m_securityPolicy.getModeName()
                                 + ")] 当前模式禁止执行该操作（仅读操作允许）。";
                CLFTerminal::scrollPrint("  ⎿ " + CLFTerminal::red("✗ blocked") + "\n");
                results.push_back(std::move(result));
                continue;
            }

            // 需用户确认
            if (needConfirm && m_confirmCallback) {
                std::string prompt = "工具 [" + call.m_name + "] 需要执行高风险操作。\n"
                                   + "参数: " + call.m_arguments;
                if (!m_confirmCallback(prompt)) {
                    result.m_content = "[Denied by user] 用户拒绝了该操作。";
                    CLFTerminal::scrollPrint("  ⎿ " + CLFTerminal::yellow("✗ denied") + "\n");
                    results.push_back(std::move(result));
                    continue;
                }
            }

            try {
                result.m_content = it->m_handler(call.m_arguments);
                auto rd = formatToolResult(result.m_content);
                CLFTerminal::scrollPrint("  ⎿ "
                    + (rd.ok ? CLFTerminal::green("✓ ") : CLFTerminal::red("✗ "))
                    + rd.text + "\n");
            } catch (const std::exception& e) {
                result.m_content = std::string("Tool execution error: ") + e.what();
                CLFTerminal::scrollPrint("  ⎿ " + CLFTerminal::red("✗ ") + e.what() + "\n");
            }
        } else {
            result.m_content = std::string("Tool not found: ") + call.m_name;
            CLFTerminal::scrollPrint("  ⎿ " + CLFTerminal::red("✗ unknown") + "\n");
        }

        results.push_back(std::move(result));
    }

    // 存储统计供后续 thoughtMark / saveSession
    m_lastToolStats.searchCount = searchCount;
    m_lastToolStats.readCount   = readCount;
    m_lastToolStats.totalCalls  = static_cast<int>(calls.size());

    return results;
}

} // namespace CLF::CLFCore
