// CLFToolExecutor.cpp — 工具调用执行器实现
// 包含 Write 工具的 diff 预览、TOCTOU 校验、确认流程

#include "CLFCore/CLFToolExecutor.hpp"

#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>

#include "CLFTools/CLFDiff.hpp"
#include "CLFTools/CLFFileOps.hpp"

namespace CLF::CLFCore {

// ============================================================================
// ANSI 颜色常量
// ============================================================================
namespace {

constexpr const char* ANSI_GREEN  = "\033[32m";
constexpr const char* ANSI_RED    = "\033[31m";
constexpr const char* ANSI_GRAY   = "\033[90m";
constexpr const char* ANSI_RESET  = "\033[0m";

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

// ============================================================================
// 格式化工具结果显示
// ============================================================================

struct ToolResultDisplay {
    bool ok;
    std::string text;
    int lines = 0;
    int chars = 0;
};
ToolResultDisplay formatToolResult(const std::string& resultJson) {
    try {
        auto r = nlohmann::json::parse(resultJson);
        if (r.contains("exitCode")) {
            int code = r["exitCode"].get<int>();
            if (code == 0) return {true, "ok", 0, 0};
            std::string detail;
            if (r.contains("stderr") && !r["stderr"].get<std::string>().empty())
                detail = r["stderr"].get<std::string>();
            else if (r.contains("stdout") && !r["stdout"].get<std::string>().empty())
                detail = r["stdout"].get<std::string>();
            if (detail.size() > 80) detail = detail.substr(0, 77) + "...";
            return {false, detail.empty() ? ("exit " + std::to_string(code)) : detail, 0, 0};
        }
        bool success = r.value("success", true);
        if (!success) {
            std::string err = r.value("error", std::string("failed"));
            return {false, err, 0, 0};
        }
        if (r.contains("content") && r["content"].is_string()) {
            const auto& c = r["content"].get<std::string>();
            int lines = 1;
            for (char ch : c) if (ch == '\n') ++lines;
            return {true, std::to_string(lines) + " lines, " + std::to_string(c.size()) + " chars", lines, static_cast<int>(c.size())};
        }
        if (r.contains("stdout") && r["stdout"].is_string()) {
            const auto& out = r["stdout"].get<std::string>();
            int lines = 1;
            for (char ch : out) if (ch == '\n') ++lines;
            return {true, std::to_string(lines) + " lines", lines, static_cast<int>(out.size())};
        }
        return {true, std::to_string(resultJson.size()) + " chars", 0, static_cast<int>(resultJson.size())};
    } catch (...) {
        return {true, std::to_string(resultJson.size()) + " chars", 0, static_cast<int>(resultJson.size())};
    }
}

// ============================================================================
// Diff 预览相关结构
// ============================================================================

struct FileSnapshot {
    std::string content;
    uint64_t mtime = 0;
    uint64_t size  = 0;
};

struct WritePreview {
    bool valid = false;
    std::string filePath;
    std::string errorMsg;
    FileSnapshot oldSnapshot;
    std::string newContent;
    std::vector<CLF::CLFTools::CLFDiffLine> diffLines;
    CLF::CLFTools::CLFDiffStats diffStats;
};

// ============================================================================
// prepareWritePreview — 设计 §2.1 Step 1
// ============================================================================

WritePreview prepareWritePreview(const CLFToolCall& call) {
    WritePreview preview;
    try {
        auto args = nlohmann::json::parse(call.m_arguments);
        preview.filePath = args.value("path", "");

        // 读取旧文件快照
        CLF::CLFTools::CLFFileSnapshot snap;
        CLF::CLFTools::readFileWithSnapshot(preview.filePath, snap);
        preview.oldSnapshot.content = snap.content;
        preview.oldSnapshot.mtime   = snap.mtime;
        preview.oldSnapshot.size    = snap.size;

        // 准备新旧内容
        std::string oldContent = snap.content;
        std::string newContent;

        if (call.m_name == "write_file") {
            newContent = args.value("content", "");
            preview.newContent = newContent;
        } else if (call.m_name == "edit_file") {
            std::string oldStr = args.value("old_string", "");
            std::string newStr = args.value("new_string", "");
            auto editPreview = CLF::CLFTools::previewEdit(oldContent, oldStr, newStr);
            if (!editPreview.m_success) {
                preview.valid = false;
                preview.errorMsg = editPreview.m_error;
                return preview;
            }
            newContent = editPreview.m_content;
            preview.newContent = std::move(editPreview.m_content);
        } else {
            preview.valid = false;
            preview.errorMsg = "not a write tool";
            return preview;
        }

        // 计算 diff
        CLF::CLFTools::CLFDiffStats stats;
        preview.diffLines = CLF::CLFTools::computeDiff(oldContent, newContent, stats);
        preview.diffStats = stats;
        preview.valid = true;
        return preview;
    } catch (const std::exception& e) {
        preview.valid = false;
        preview.errorMsg = std::string("prepareWritePreview error: ") + e.what();
        return preview;
    }
}

// ============================================================================
// renderDiffAnsi — 设计 §2.1 Step 3（结构化 diff → ANSI 彩色文本）
// 两遍渲染：第一遍识别 hunk 边界，第二遍带 header 输出
// ============================================================================

std::string renderDiffAnsi(const WritePreview& preview) {
    std::string out;
    const auto& diff  = preview.diffLines;
    const auto& stats = preview.diffStats;

    if (diff.empty() && !stats.truncated) return "";

    // 摘要行
    if (stats.truncated) {
        out += "  ";
        out += ANSI_GRAY;
        out += "⎿  " + stats.truncReason;
        out += ANSI_RESET;
        out += "\n";
    } else if (stats.added + stats.removed > 0) {
        out += "  ";
        out += ANSI_GRAY;
        out += "⎿  +" + std::to_string(stats.added)
            + " -" + std::to_string(stats.removed);
        if (stats.hunks > 0) {
            out += " in " + std::to_string(stats.hunks) + " hunk";
            if (stats.hunks > 1) out += "s";
        }
        out += ANSI_RESET;
        out += "\n";
    }

    if (stats.truncated && diff.empty()) return out;
    if (diff.empty()) return out;

    // 第一遍：标记每个 diff 行所属的 hunk 编号（-1 = 不属于任何 hunk）
    int total = static_cast<int>(diff.size());
    std::vector<int> hunkId(total, -1);
    int curHunk = 0;

    for (int i = 0; i < total; ++i) {
        bool isSkip = (diff[i].text == "..." ||
                       diff[i].text.find("... (") == 0);
        if (isSkip) continue;

        bool isChange = (diff[i].op == CLF::CLFTools::CLFDiffOp::Add ||
                         diff[i].op == CLF::CLFTools::CLFDiffOp::Remove);
        if (isChange && hunkId[i] == -1) {
            // 新 hunk：向前扩展 context 行，向后扩展 context 行
            int lo = i;
            while (lo > 0 && (i - lo) < 5 && diff[lo - 1].op == CLF::CLFTools::CLFDiffOp::Keep && diff[lo - 1].text != "...") --lo;
            int hi = i;
            while (hi < total - 1 && diff[hi + 1].op == CLF::CLFTools::CLFDiffOp::Keep && diff[hi + 1].text != "...") ++hi;
            // 也标记后续变更行
            int end = hi;
            for (; end < total; ++end) {
                if (diff[end].op == CLF::CLFTools::CLFDiffOp::Keep && diff[end].text != "...") {
                    // 检查之后是否还有变更（常规窗口）
                    bool hasNearChange = false;
                    for (int c = end + 1; c <= std::min(total - 1, end + 5); ++c) {
                        if (diff[c].op != CLF::CLFTools::CLFDiffOp::Keep) { hasNearChange = true; break; }
                    }
                    if (!hasNearChange) { --end; break; }
                } else if (diff[end].text == "..." || diff[end].text.find("... (") == 0) {
                    --end; break;
                }
            }
            if (end >= total) end = total - 1;
            for (int k = lo; k <= end; ++k) {
                if (hunkId[k] == -1) hunkId[k] = curHunk;
            }
            ++curHunk;
        }
    }

    // 第二遍：渲染（hunk 开始时输出 @@ header）
    int lastHunk = -1;
    for (int i = 0; i < total; ++i) {
        const auto& line = diff[i];

        // hunk header
        if (hunkId[i] != -1 && hunkId[i] != lastHunk) {
            lastHunk = hunkId[i];
            // 找该 hunk 的第一个旧/新行号
            int oStart = 0, nStart = 0;
            for (int k = i; k < total && hunkId[k] == lastHunk; ++k) {
                if (diff[k].oldLineNo > 0 && oStart == 0) oStart = diff[k].oldLineNo;
                if (diff[k].newLineNo > 0 && nStart == 0) nStart = diff[k].newLineNo;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "  @@ -%d +%d @@\n",
                     oStart > 0 ? oStart : (nStart > 0 ? nStart : 1),
                     nStart > 0 ? nStart : (oStart > 0 ? oStart : 1));
            out += ANSI_GRAY;
            out += buf;
            out += ANSI_RESET;
        }

        // 跳过不属于任何 hunk 的行
        if (hunkId[i] == -1) continue;

        // 折叠/省略行
        if (line.text == "...") {
            out += "  ";
            out += ANSI_GRAY;
            out += "...\n";
            out += ANSI_RESET;
            lastHunk = -1; // 折叠后重新输出 header
            continue;
        }
        if (line.text.find("... (") == 0) {
            out += "  ";
            out += ANSI_GRAY;
            out += line.text + "\n";
            out += ANSI_RESET;
            lastHunk = -1;
            continue;
        }

        // 格式化行
        char numBuf[16];
        switch (line.op) {
        case CLF::CLFTools::CLFDiffOp::Add:
            snprintf(numBuf, sizeof(numBuf), " %4d + ", line.newLineNo);
            out += ANSI_GREEN;
            out += numBuf;
            out += line.text;
            out += ANSI_RESET;
            out += "\n";
            break;
        case CLF::CLFTools::CLFDiffOp::Remove:
            snprintf(numBuf, sizeof(numBuf), " %4d - ", line.oldLineNo);
            out += ANSI_RED;
            out += numBuf;
            out += line.text;
            out += ANSI_RESET;
            out += "\n";
            break;
        case CLF::CLFTools::CLFDiffOp::Keep:
        default:
            snprintf(numBuf, sizeof(numBuf), " %4d   ", line.newLineNo);
            out += ANSI_GRAY;
            out += numBuf;
            out += line.text;
            out += ANSI_RESET;
            out += "\n";
            break;
        }
    }

    return out;
}

} // anonymous namespace

// ============================================================================
// CLFToolExecutor 构造
// ============================================================================

CLFToolExecutor::CLFToolExecutor(std::vector<CLFTool>& tools,
                                 CLFSecurityPolicy& policy,
                                 std::function<bool(const std::string&)> confirmCallback,
                                 ToolStats& stats,
                                 CLF::CLFTypes::ICLFOutput* output,
                                 std::atomic<bool>* interruptFlag)
    : m_tools(tools)
    , m_securityPolicy(policy)
    , m_confirmCallback(std::move(confirmCallback))
    , m_stats(stats)
    , m_output(output)
    , m_interruptFlag(interruptFlag) {
}

// ============================================================================
// execute — 主执行循环（含 Write 工具的 diff 预览 + 确认流程）
// ============================================================================

std::vector<CLFToolResult> CLFToolExecutor::execute(
    const std::vector<CLFToolCall>& calls) {
    std::vector<CLFToolResult> results;
    results.reserve(calls.size());

    int searchCount = m_stats.searchCount;
    int readCount   = m_stats.readCount;

    for (const auto& call : calls) {
        // 中断检查
        if (m_interruptFlag && m_interruptFlag->load()) {
            if (m_output) m_output->emitContent("  ⎿ ⏹ 已中断\n");
            break;
        }

        CLFToolResult result;
        result.m_toolCallId = call.m_id;
        result.m_name       = call.m_name;

        std::string keyParam = extractKeyParam(call.m_arguments);
        std::string header = "● " + call.m_name;
        if (!keyParam.empty()) {
            header += "(" + keyParam + ")";
        }
        if (m_output) m_output->emitContent("\n" + header + "\n");

        // 统计
        if (call.m_name.find("search") != std::string::npos) ++searchCount;
        if (call.m_name.find("read") != std::string::npos) ++readCount;

        // 工具查找
        auto it = std::find_if(m_tools.begin(), m_tools.end(),
            [&](const CLFTool& t) { return t.m_name == call.m_name; });

        if (it == m_tools.end()) {
            result.m_content = std::string("Tool not found: ") + call.m_name;
            if (m_output) m_output->emitContent("  ⎿ ✗ unknown\n");
            results.push_back(std::move(result));
            continue;
        }

        // 安全策略检查
        bool needConfirm = false;
        if (!m_securityPolicy.isAllowed(it->m_risk, needConfirm)) {
            result.m_content = std::string("[Blocked by security policy (mode: ")
                             + m_securityPolicy.getModeName()
                             + ")] 当前模式禁止执行该操作。";
            if (m_output) m_output->emitContent("  ⎿ ✗ blocked\n");
            results.push_back(std::move(result));
            continue;
        }

        // ================================================================
        // Write 工具的 diff 预览 + 确认流程（设计 §2.1 Step 1–7）
        // ================================================================
        bool isWriteTool = (call.m_name == "write_file" || call.m_name == "edit_file");
        WritePreview preview;

        if (isWriteTool) {
            // --- Step 1: prepareWritePreview ---
            preview = prepareWritePreview(call);

            // --- Step 1.5: valid 检查 ---
            if (!preview.valid) {
                result.m_content = preview.errorMsg;
                if (m_output) {
                    m_output->emitContent("  ⎿ ✗ " + preview.errorMsg + "\n");
                }
                results.push_back(std::move(result));
                continue;
            }

            // --- Step 2: 超限阻断（仅需确认模式）---
            if (preview.diffStats.truncated && needConfirm) {
                result.m_content = "File too large to preview diff. "
                                   "Use Auto mode or set force=true.";
                if (m_output) {
                    m_output->emitContent("  ⎿ ✗ File too large to preview diff. "
                                          "Use Auto mode or set force=true.\n");
                }
                results.push_back(std::move(result));
                continue;
            }

            // --- Step 3: 渲染 diff 预览 ---
            if (m_output) {
                std::string diffAnsi = renderDiffAnsi(preview);
                if (!diffAnsi.empty()) {
                    m_output->emitContent(diffAnsi);
                }
            }

            // --- Step 4: 模式分流 ---
            if (needConfirm && m_confirmCallback) {
                std::string prompt = "工具 [" + call.m_name + "] 将修改文件: "
                                   + preview.filePath;
                if (!m_confirmCallback(prompt)) {
                    result.m_content = "[Denied by user] 用户拒绝了该操作。";
                    if (m_output) m_output->emitContent("  ⎿ ✗ denied\n");
                    results.push_back(std::move(result));
                    if (m_interruptFlag && m_interruptFlag->load()) break;
                    continue;
                }
            }

            // --- Step 5: TOCTOU 校验 ---
            {
                auto currentMtime = CLF::CLFTools::getFileMtime(preview.filePath);
                auto currentSize  = CLF::CLFTools::getFileSize(preview.filePath);
                if ((preview.oldSnapshot.mtime != 0 && currentMtime != 0 &&
                     currentMtime != preview.oldSnapshot.mtime) ||
                    (preview.oldSnapshot.size != 0 && currentSize != 0 &&
                     currentSize != preview.oldSnapshot.size)) {
                    result.m_content = "File modified after preview. Please review again.";
                    if (m_output) {
                        m_output->emitContent(
                            "  ⎿ ✗ File modified after preview. Please review again.\n");
                    }
                    results.push_back(std::move(result));
                    continue;
                }
            }
        } else {
            // 非 Write 类工具（execute_command 等 Command risk）的确认检查
            if (needConfirm && m_confirmCallback) {
                std::string prompt = "工具 [" + call.m_name + "] 需要执行高风险操作。\n"
                                   + "参数: " + call.m_arguments;
                if (!m_confirmCallback(prompt)) {
                    result.m_content = "[Denied by user] 用户拒绝了该操作。";
                    if (m_output) m_output->emitContent("  ⎿ ✗ denied\n");
                    results.push_back(std::move(result));
                    if (m_interruptFlag && m_interruptFlag->load()) break;
                    continue;
                }
            }
        }

        // --- Step 6 & 7: 执行 handler + 显示结果 ---
        try {
            result.m_content = it->m_handler(call.m_arguments);
            auto rd = formatToolResult(result.m_content);

            if (m_output) {
                if (isWriteTool && !preview.diffLines.empty()) {
                    // Write 工具成功：摘要带 diff 统计
                    const auto& ds = preview.diffStats;
                    if (ds.added + ds.removed > 0 && !ds.truncated) {
                        m_output->emitContent(
                            "  ✓ " + call.m_name + "(" + preview.filePath
                            + ") — +" + std::to_string(ds.added)
                            + " -" + std::to_string(ds.removed) + " lines"
                            + ", " + std::to_string(ds.hunks) + " hunk"
                            + (ds.hunks > 1 ? "s" : "") + "\n");
                    } else if (ds.truncated) {
                        m_output->emitContent(
                            "  ✓ " + call.m_name + "(" + preview.filePath
                            + ") — written (diff truncated)\n");
                    } else {
                        m_output->emitContent(
                            "  ✓ " + call.m_name + "(" + preview.filePath
                            + ") — written\n");
                    }
                } else if (rd.ok && rd.text.size() <= 200) {
                    m_output->emitContent("  ✓ " + call.m_name
                        + (keyParam.empty() ? "" : "(" + keyParam + ")") + "\n");
                } else if (rd.ok) {
                    m_output->emitContent("  ✓ " + call.m_name
                        + (keyParam.empty() ? "" : "(" + keyParam + ")")
                        + " — " + std::to_string(rd.lines) + " lines, "
                        + std::to_string(rd.chars) + " chars\n");
                } else {
                    auto reason = rd.text.size() > 100 ? rd.text.substr(0, 100) + "…" : rd.text;
                    m_output->emitContent("  ✗ " + call.m_name
                        + (keyParam.empty() ? "" : "(" + keyParam + ")")
                        + " — " + reason + " (scroll for full detail)\n");
                }
            }
        } catch (const std::exception& e) {
            result.m_content = std::string("Tool execution error: ") + e.what();
            std::string err = e.what();
            if (err.size() > 100) err = err.substr(0, 100) + "…";
            if (m_output) m_output->emitContent("  ✗ " + call.m_name
                + (keyParam.empty() ? "" : "(" + keyParam + ")")
                + " — " + err + " (scroll for full detail)\n");
        }

        results.push_back(std::move(result));
    }

    m_stats.searchCount = searchCount;
    m_stats.readCount   = readCount;
    m_stats.totalCalls  = static_cast<int>(calls.size());

    return results;
}

} // namespace CLF::CLFCore
