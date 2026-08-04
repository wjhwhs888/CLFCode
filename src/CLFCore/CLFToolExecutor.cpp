// CLFToolExecutor.cpp — 工具调用执行器实现

#include "CLFCore/CLFToolExecutor.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace CLF::CLFCore {

namespace {

// 从工具参数 JSON 中提取关键参数用于显示
std::string extractKeyParam(const std::string& argsJson) {
    try {
        auto args = nlohmann::json::parse(argsJson);
        if (args.contains("path") && args["path"].is_string()) {
            std::string p = args["path"].get<std::string>();
            if (p.size() > 55) p = "..." + p.substr(p.size() - 52);
            return p;
        }
        if (args.contains("command") && args["command"].is_string()) {
            std::string cmd = args["command"].get<std::string>();
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
    if (argsJson.empty() || argsJson == "{}" || argsJson == "[]") return "";
    if (argsJson.size() > 60) return argsJson.substr(0, 57) + "...";
    return argsJson;
}

// 格式化工具结果显示
struct ToolResultDisplay {
    bool ok;
    std::string text;
};
ToolResultDisplay formatToolResult(const std::string& resultJson) {
    try {
        auto r = nlohmann::json::parse(resultJson);
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

CLFToolExecutor::CLFToolExecutor(std::vector<CLFTool>& tools,
                                 CLFSecurityPolicy& policy,
                                 std::function<bool(const std::string&)> confirmCallback,
                                 ToolStats& stats,
                                 CLF::CLFTypes::ICLFOutput* output)
    : m_tools(tools)
    , m_securityPolicy(policy)
    , m_confirmCallback(std::move(confirmCallback))
    , m_stats(stats)
    , m_output(output) {
}

std::vector<CLFToolResult> CLFToolExecutor::execute(
    const std::vector<CLFToolCall>& calls) {
    std::vector<CLFToolResult> results;
    results.reserve(calls.size());

    int searchCount = m_stats.searchCount;
    int readCount   = m_stats.readCount;

    for (const auto& call : calls) {
        CLFToolResult result;
        result.m_toolCallId = call.m_id;
        result.m_name       = call.m_name;

        std::string keyParam = extractKeyParam(call.m_arguments);
        std::string header = "● " + call.m_name;
        if (!keyParam.empty()) {
            header += "(" + keyParam + ")";
        }
        if(m_output) m_output->emitContent("\n" + header + "\n");

        // 统计
        if (call.m_name.find("search") != std::string::npos) {
            ++searchCount;
        }
        if (call.m_name.find("read") != std::string::npos) {
            ++readCount;
        }

        auto it = std::find_if(m_tools.begin(), m_tools.end(),
            [&](const CLFTool& t) { return t.m_name == call.m_name; });

        if (it == m_tools.end()) {
            result.m_content = std::string("Tool not found: ") + call.m_name;
            if(m_output) m_output->emitContent("  ⎿ ✗ unknown\n");
            results.push_back(std::move(result));
            continue;
        }

        // 安全策略检查
        bool needConfirm = false;
        if (!m_securityPolicy.isAllowed(it->m_risk, needConfirm)) {
            result.m_content = std::string("[Blocked by security policy (mode: ")
                             + m_securityPolicy.getModeName()
                             + ")] 当前模式禁止执行该操作。";
            if(m_output) m_output->emitContent("  ⎿ ✗ blocked\n");
            results.push_back(std::move(result));
            continue;
        }

        // 需用户确认
        if (needConfirm && m_confirmCallback) {
            std::string prompt = "工具 [" + call.m_name + "] 需要执行高风险操作。\n"
                               + "参数: " + call.m_arguments;
            if (!m_confirmCallback(prompt)) {
                result.m_content = "[Denied by user] 用户拒绝了该操作。";
                if(m_output) m_output->emitContent("  ⎿ ✗ denied\n");
                results.push_back(std::move(result));
                continue;
            }
        }

        // 执行工具
        try {
            result.m_content = it->m_handler(call.m_arguments);
            auto rd = formatToolResult(result.m_content);
            if (m_output) m_output->emitContent("  ⎿ "
                + std::string(rd.ok ? "✓ " : "✗ ")
                + rd.text + "\n");
        } catch (const std::exception& e) {
            result.m_content = std::string("Tool execution error: ") + e.what();
            if(m_output) m_output->emitContent(std::string("  ⎿ ✗ ") + e.what() + "\n");
        }

        results.push_back(std::move(result));
    }

    m_stats.searchCount = searchCount;
    m_stats.readCount   = readCount;
    m_stats.totalCalls  = static_cast<int>(calls.size());

    return results;
}

} // namespace CLF::CLFCore
