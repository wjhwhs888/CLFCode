// CLFSessionSummarizer.cpp — 会话摘要生成器实现

#include "CLFCore/CLFSessionSummarizer.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFProtocolAdapter.hpp"
#include "CLFNetwork/ICLFHttpClient.hpp"
#include "CLFTypes/CLFTextUtil.hpp"   // A2：utf8SafeHead

#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

using json = nlohmann::json;

namespace CLF::CLFCore {

// ============================================================================
// 构造 / 查询
// ============================================================================

CLFSessionSummarizer::CLFSessionSummarizer(
    std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> httpClient,
    const CLFAgentConfig& config)
    : m_httpClient(std::move(httpClient))
    , m_config(config) {
}

bool CLFSessionSummarizer::isEnabled() const {
    return m_config.m_contextCompression && m_httpClient != nullptr;
}

// ============================================================================
// generate — 核心入口
// ============================================================================

CLFSessionSummary CLFSessionSummarizer::generate(const std::vector<CLFMessage>& messages) {
    if (messages.empty()) {
        CLFLogger::instance().debug("[Summarizer] skipped: no messages");
        return {};
    }

    // 尝试 API 生成
    if (m_config.m_contextCompression && m_httpClient) {
        CLFLogger::instance().debug("[Summarizer] generate: "
                                    + std::to_string(messages.size()) + " msgs, trying API");
        try {
            auto summary = generateViaApi(messages);
            if (summary.m_valid) return summary;
        } catch (const std::exception& e) {
            CLFLogger::instance().warn("[Summarizer] API exception: "
                                       + std::string(e.what()) + ", falling back");
        } catch (...) {
            CLFLogger::instance().warn("[Summarizer] API unknown exception, falling back");
        }
    }

    // 降级：规则提取
    CLFLogger::instance().info("[Summarizer] falling back to rule-based");
    return buildFallback(messages);
}

// ============================================================================
// generateViaApi — API 生成摘要
// ============================================================================

CLFSessionSummary CLFSessionSummarizer::generateViaApi(
    const std::vector<CLFMessage>& messages) {

    // ① 截取对话文本（最近 ~48000 token，单条 >3000 字符截断）
    std::string conversationText;
    int estimatedTokens = 0;
    constexpr int kMaxInputTokens = 48000;
    constexpr int kMaxSingleMsgChars = 3000;

    std::vector<std::string> lines;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->m_role == "system") continue;

        std::string body = it->m_content;
        if (body.size() > kMaxSingleMsgChars) {
            body = body.substr(0, kMaxSingleMsgChars)
                 + "...[truncated " + std::to_string(body.size()) + " chars]";
        }

        std::string line = "[" + it->m_role + "]: " + body;
        int lineTokens = static_cast<int>(line.size()) / 4;  // ASCII 粗略估算
        if (estimatedTokens + lineTokens > kMaxInputTokens) break;
        estimatedTokens += lineTokens;
        lines.push_back(std::move(line));
    }
    std::reverse(lines.begin(), lines.end());

    for (const auto& l : lines) conversationText += l + "\n";
    if (conversationText.empty()) return {};

    // ② 构造 API 请求
    json body;
    body["model"]       = m_config.m_modelName;
    body["max_tokens"]  = 1024;
    body["temperature"] = 0.0;
    body["stream"]      = false;

    json msgs = json::array();
    msgs.push_back({
        {"role", "system"},
        {"content",
         "You are a session archiver for an AI coding assistant called CLFCode. "
         "Analyze the following conversation and output a JSON object. "
         "Return ONLY valid JSON, no markdown fences or other text. "
         "The JSON must have these fields:\n"
         "- \"summary\": (string) A concise summary of what the user asked and what was accomplished.\n"
         "- \"key_decisions\": (array of strings) Important technical decisions made.\n"
         "- \"current_plan\": (string) The current plan or task being worked on.\n"
         "- \"files_modified\": (array of strings) File paths discussed or modified.\n"
         "- \"pending_tasks\": (array of strings) Tasks explicitly mentioned as still needing work.\n"
         "Use the conversation language for the summary text. "
         "Limit summary to 500 characters and current_plan to 300 characters."}
    });
    msgs.push_back({
        {"role", "user"},
        {"content", "Summarize this CLFCode session:\n\n" + conversationText}
    });
    body["messages"] = msgs;

    CLFLogger::instance().debug("[Summarizer] API request: model=" + m_config.m_modelName
                                + ", max_tokens=1024");

    // ③ 发起 API 调用
    auto response = m_httpClient->postJson("/v1/chat/completions", body.dump());
    if (!response.m_error.empty()) {
        CLFLogger::instance().warn("[Summarizer] API error: " + response.m_error);
        return {};
    }
    if (response.m_body.empty()) {
        CLFLogger::instance().warn("[Summarizer] API returned empty body");
        return {};
    }

    CLFLogger::instance().info("[Summarizer] API response: "
                               + std::to_string(response.m_body.size()) + " chars");

    // ④ 解析响应
    return parseSummaryResponse(response.m_body);
}

// ============================================================================
// parseSummaryResponse — 解析 API 返回的 JSON 摘要
// ============================================================================

CLFSessionSummary CLFSessionSummarizer::parseSummaryResponse(
    const std::string& responseText) const {

    // Step A: 提取外层 JSON → choices[0].message.content
    json respJson;
    try {
        respJson = json::parse(responseText);
    } catch (const json::parse_error&) {
        return {};
    }

    if (!respJson.contains("choices") || !respJson["choices"].is_array()
        || respJson["choices"].empty()) {
        return {};
    }

    const auto& choice = respJson["choices"][0];
    if (!choice.contains("message") || !choice["message"].is_object()) return {};

    std::string content = choice["message"].value("content", "");
    if (content.empty()) return {};

    // Step B: 从 content 中提取 JSON（strip markdown 围栏）
    auto jsonStart = content.find('{');
    auto jsonEnd   = content.rfind('}');
    if (jsonStart == std::string::npos || jsonEnd == std::string::npos) return {};
    std::string jsonStr = content.substr(jsonStart, jsonEnd - jsonStart + 1);

    // Step C: 解析为 CLFSessionSummary
    CLFSessionSummary summary;
    try {
        json sum = json::parse(jsonStr);
        summary.m_summary     = sum.value("summary", "");
        summary.m_currentPlan = sum.value("current_plan", "");
        summary.m_method      = "api";
        summary.m_valid       = !summary.m_summary.empty();

        if (sum.contains("key_decisions") && sum["key_decisions"].is_array()) {
            for (const auto& kd : sum["key_decisions"]) {
                if (kd.is_string()) summary.m_keyDecisions.push_back(kd.get<std::string>());
            }
        }
        if (sum.contains("files_modified") && sum["files_modified"].is_array()) {
            for (const auto& fm : sum["files_modified"]) {
                if (fm.is_string()) summary.m_filesModified.push_back(fm.get<std::string>());
            }
        }
        if (sum.contains("pending_tasks") && sum["pending_tasks"].is_array()) {
            for (const auto& pt : sum["pending_tasks"]) {
                if (pt.is_string()) summary.m_pendingTasks.push_back(pt.get<std::string>());
            }
        }
    } catch (const json::parse_error&) {
        // JSON 解析失败 → 将原始文本作为降级摘要
        summary.m_summary = content;
        summary.m_method  = "api_fallback_text";
        summary.m_valid   = !content.empty();
    }

    CLFLogger::instance().debug("[Summarizer] parsed: summary="
                                + std::to_string(summary.m_summary.size()) + " chars, decisions="
                                + std::to_string(summary.m_keyDecisions.size()) + ", files="
                                + std::to_string(summary.m_filesModified.size()));

    return summary;
}

// ============================================================================
// buildFallback — 规则降级摘要
// ============================================================================

CLFSessionSummary CLFSessionSummarizer::buildFallback(
    const std::vector<CLFMessage>& messages) const {

    CLFSessionSummary summary;
    summary.m_method = "rule_based";

    if (messages.empty()) return summary;

    std::string firstUserMsg;
    std::string lastUserMsg;
    std::set<std::string> filesSeen;

    for (const auto& msg : messages) {
        // 首尾 user 消息
        if (msg.m_role == "user" && !msg.m_content.empty()) {
            if (firstUserMsg.empty()) firstUserMsg = msg.m_content;
            lastUserMsg = msg.m_content;
        }

        // 从 tool call arguments 中提取文件路径
        for (const auto& tc : msg.m_toolCalls) {
            try {
                auto args = json::parse(tc.m_arguments);
                auto tryExtract = [&](const char* key) {
                    if (args.contains(key) && args[key].is_string()) {
                        std::string val = args[key].get<std::string>();
                        // 过滤明显不是文件路径的值
                        if (!val.empty() && val.size() < 500
                            && val.find(' ') == std::string::npos) {
                            filesSeen.insert(val);
                        }
                    }
                };
                tryExtract("file_path");
                tryExtract("path");
                tryExtract("filePath");
            } catch (...) {}
        }
    }

    // 摘要 = 第一条 user 消息截断（A2：字节级 substr → utf8SafeHead，
    // 摘要文本注入 system 提示，劈半会产生非法 UTF-8；阈值 200 语义保留）
    if (!firstUserMsg.empty()) {
        summary.m_summary = firstUserMsg.size() > 200
                          ? CLFTextUtil::utf8SafeHead(firstUserMsg, 197, "...")
                          : firstUserMsg;
    } else {
        summary.m_summary = "(无用户输入)";
    }

    // 当前计划 = 最后一条 user 消息（如果与首条不同）
    if (!lastUserMsg.empty() && lastUserMsg != firstUserMsg) {
        summary.m_currentPlan = lastUserMsg.size() > 200
                              ? CLFTextUtil::utf8SafeHead(lastUserMsg, 197, "...")
                              : lastUserMsg;
    }

    // 涉及文件
    for (const auto& f : filesSeen) {
        summary.m_filesModified.push_back(f);
    }

    summary.m_valid = true;

    CLFLogger::instance().info("[Summarizer] fallback: summary="
                               + std::to_string(summary.m_summary.size()) + " chars, files="
                               + std::to_string(summary.m_filesModified.size()));

    return summary;
}

} // namespace CLF::CLFCore
