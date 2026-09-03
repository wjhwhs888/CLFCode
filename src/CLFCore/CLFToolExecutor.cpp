// CLFToolExecutor.cpp — 工具调用执行器实现
// 包含 Write 工具的 diff 预览、TOCTOU 校验、确认流程

#include "CLFCore/CLFToolExecutor.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFTypes/CLFTextUtil.hpp"

#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>

#include "CLFTools/CLFDiff.hpp"
#include "CLFTools/CLFFileOps.hpp"

namespace CLF::CLFCore {

// ============================================================================
namespace {

// 从工具参数 JSON 中提取关键参数用于显示
// A2：字节级 substr 截断 → utf8SafeHead/Tail（显示场景，不劈半多字节；阈值语义保留）
std::string extractKeyParam(const std::string& argsJson) {
    try {
        auto args = nlohmann::json::parse(argsJson);
        if (args.contains("path") && args["path"].is_string()) {
            std::string p = args["path"].get<std::string>();
            if (p.size() > 55) p = CLFTextUtil::utf8SafeTail(p, 52, "...");
            return p;
        }
        if (args.contains("command") && args["command"].is_string()) {
            std::string cmd = args["command"].get<std::string>();
            while (!cmd.empty() && cmd.front() == ' ') cmd.erase(0, 1);
            if (cmd.size() > 55) cmd = CLFTextUtil::utf8SafeHead(cmd, 52, "...");
            return cmd;
        }
        if (args.contains("pattern") && args["pattern"].is_string()) {
            std::string p = args["pattern"].get<std::string>();
            if (p.size() > 55) p = CLFTextUtil::utf8SafeHead(p, 52, "...");
            return p;
        }
        if (args.contains("message") && args["message"].is_string()) {
            std::string m = args["message"].get<std::string>();
            if (m.size() > 55) m = CLFTextUtil::utf8SafeHead(m, 52, "...");
            return m;
        }
    } catch (...) {}
    if (argsJson.empty() || argsJson == "{}" || argsJson == "[]") return "";
    if (argsJson.size() > 60) return CLFTextUtil::utf8SafeHead(argsJson, 57, "...");
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
            if (detail.size() > 80) detail = CLFTextUtil::utf8SafeHead(detail, 77, "...");  // A2
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

        // B1-7 复查：此处为 write_file/edit_file 的**行为分支**（覆盖 vs 替换，
        // 非分类语义，m_risk 同为 Write 无法区分）——保留名字匹配
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
// renderDiff — 将结构化 diff 逐行 emit 到 ICLFOutput（带样式）
// ============================================================================

void renderDiff(CLF::CLFTypes::ICLFOutput* output, const WritePreview& preview) {
    const auto& diff  = preview.diffLines;
    const auto& stats = preview.diffStats;
    using LS = CLF::CLFTypes::ICLFOutput::LineStyle;

    if (diff.empty() && !stats.truncated) return;

    // 摘要行
    if (stats.truncated) {
        output->emitStyledLine("  ⎿  " + stats.truncReason, LS::Context);
    } else if (stats.added + stats.removed > 0) {
        std::string info = "  ⎿  +" + std::to_string(stats.added)
                         + " -" + std::to_string(stats.removed);
        if (stats.hunks > 0)
            info += " in " + std::to_string(stats.hunks) + " hunk" + (stats.hunks > 1 ? "s" : "");
        output->emitStyledLine(info, LS::Context);
    }

    if (stats.truncated && diff.empty()) return;
    if (diff.empty()) return;

    // 第一遍：标记 hunk 边界
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
            int lo = i;
            while (lo > 0 && (i - lo) < 5 && diff[lo - 1].op == CLF::CLFTools::CLFDiffOp::Keep && diff[lo - 1].text != "...") --lo;
            int hi = i;
            while (hi < total - 1 && diff[hi + 1].op == CLF::CLFTools::CLFDiffOp::Keep && diff[hi + 1].text != "...") ++hi;
            int end = hi;
            for (; end < total; ++end) {
                if (diff[end].op == CLF::CLFTools::CLFDiffOp::Keep && diff[end].text != "...") {
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
            for (int k = lo; k <= end; ++k)
                if (hunkId[k] == -1) hunkId[k] = curHunk;
            ++curHunk;
        }
    }

    // 第二遍：收集渲染条目（@@ 头、... 分隔、行号+内容），再统一截断与发射
    struct RenderEntry {
        std::string text;
        LS style;
    };
    std::vector<RenderEntry> entries;
    int lastHunk = -1;
    for (int i = 0; i < total; ++i) {
        const auto& line = diff[i];

        if (hunkId[i] != -1 && hunkId[i] != lastHunk) {
            lastHunk = hunkId[i];
            int oStart = 0, nStart = 0;
            for (int k = i; k < total && hunkId[k] == lastHunk; ++k) {
                if (diff[k].oldLineNo > 0 && oStart == 0) oStart = diff[k].oldLineNo;
                if (diff[k].newLineNo > 0 && nStart == 0) nStart = diff[k].newLineNo;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "  @@ -%d +%d @@",
                     oStart > 0 ? oStart : (nStart > 0 ? nStart : 1),
                     nStart > 0 ? nStart : (oStart > 0 ? oStart : 1));
            entries.push_back({buf, LS::Context});
        }

        if (hunkId[i] == -1) continue;

        if (line.text == "...") {
            entries.push_back({"  ...", LS::Context});
            lastHunk = -1;
            continue;
        }
        if (line.text.find("... (") == 0) {
            entries.push_back({"  " + line.text, LS::Context});
            lastHunk = -1;
            continue;
        }

        char numBuf[16];
        LS style = LS::Context;
        switch (line.op) {
        case CLF::CLFTools::CLFDiffOp::Add:
            snprintf(numBuf, sizeof(numBuf), " %4d + ", line.newLineNo);
            style = LS::Add;
            break;
        case CLF::CLFTools::CLFDiffOp::Remove:
            snprintf(numBuf, sizeof(numBuf), " %4d - ", line.oldLineNo);
            style = LS::Remove;
            break;
        case CLF::CLFTools::CLFDiffOp::Keep:
        default:
            snprintf(numBuf, sizeof(numBuf), " %4d   ", line.newLineNo);
            style = LS::Context;
            break;
        }
        entries.push_back({std::string(numBuf) + line.text, style});
    }

    // P0-2B: UI 侧 head/tail 截断（dsh 模式：前 16 + 标记 + 后 16，公共 headTailCapWithMarker）
    if (entries.size() > 32) {
        size_t omitted = entries.size() - 32;
        entries = headTailCapWithMarker(
            entries, RenderEntry{"  … 其余 " + std::to_string(omitted) + " 行", LS::Context});
    }
    for (const auto& e : entries)
        output->emitStyledLine(e.text, e.style);
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
                                 std::atomic<bool>* interruptFlag,
                                 const CLFTimerLabels* labels,
                                 std::atomic<int>* thinkingSec)
    : m_tools(tools)
    , m_securityPolicy(policy)
    , m_confirmCallback(std::move(confirmCallback))
    , m_stats(stats)
    , m_output(output)
    , m_interruptFlag(interruptFlag)
    , m_labels(labels)
    , m_thinkingSec(thinkingSec) {
}

// ============================================================================
// execute — 主执行循环（含 Write 工具的 diff 预览 + 确认流程）
// ============================================================================

std::vector<CLFToolResult> CLFToolExecutor::execute(
    const std::vector<CLFToolCall>& calls) {
    std::vector<CLFToolResult> results;
    results.reserve(calls.size());

    struct ProgressGuard {
        CLF::CLFTypes::ICLFOutput* out;
        bool committed = false;
        ~ProgressGuard() { if (!committed && out) out->finishProgress(""); }
        void commit(const std::string& s) { committed = true; if (out) out->finishProgress(s); }
    } guard{m_output};

    int searchCount   = m_stats.searchCount;
    int readCount     = m_stats.readCount;
    int progressReads = 0;  // 读类工具计数（用于 summary）
    int progressEdits = 0;  // 写类工具计数

    for (const auto& call : calls) {
        // T3: 每次迭代末刷新（设计-任务清单UI显示 §3.4）——todo_write 等状态类
        // 工具返回即重绘（原仅靠 turnTimer 1Hz 兜底，最长延迟 1 秒）。
        // RAII 保证 :376/:387 等 continue 提前退出分支也被覆盖
        struct RefreshGuard {
            CLF::CLFTypes::ICLFOutput* out;
            ~RefreshGuard() { if (out) out->requestRefresh(); }
        } refreshGuard{m_output};

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
        // B1：工具查找前置（能力标签判定需 it；语义微调——不存在的工具不再计入统计，
        // 原名字匹配在查找前会误计幻觉工具名，B1 定案记录）
        auto it = std::find_if(m_tools.begin(), m_tools.end(),
            [&](const CLFTool& t) { return t.m_name == call.m_name; });

        if (it == m_tools.end()) {
            result.m_content = std::string("Tool not found: ") + call.m_name;
            if (m_output) m_output->emitContent("  ⎿ ✗ unknown\n");
            results.push_back(std::move(result));
            continue;
        }

        bool useProgressive = (m_labels && m_thinkingSec);
        // 渐进模式下非写类工具不发射永久内容（由 showProgress 替代）
        // B1：名字匹配 → m_risk == Write（风险级即能力声明）
        if (m_output && (!useProgressive || it->m_risk == CLFToolRisk::Write))
            m_output->emitContent("\n" + header + "\n");

        // 统计（B1：名字匹配 → 能力标签；口径 B1-4：read 桶含 list_directory）
        if (it->m_isSearch) ++searchCount;
        if (it->m_isRead)   ++readCount;

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

        // S2-2: 危险命令强制确认——**不受安全模式影响**，Auto 模式同样拦截。
        // 定位为提示层（模型可绕过），仅降低误操作概率，不替代上面的模式管控。
        // B1：名字匹配 → m_risk == Command（确认触发仍由 policy 按内容判定，B1-1 定案）
        if (it->m_risk == CLFToolRisk::Command) {
            std::string cmdText;
            try {
                auto argJson = nlohmann::json::parse(call.m_arguments);
                cmdText = argJson.value("command", "");
            } catch (...) {
                cmdText = call.m_arguments;  // 解析失败则按原文匹配，宁可多确认
            }
            if (m_securityPolicy.isDangerousCommand(cmdText)) {
                needConfirm = true;
                if (m_output) m_output->emitContent("  ⎿ ⚠ 命中危险命令模式，需确认\n");
            }
        }

        // S2-5: web_fetch 工具本身按 Read 级注册（GET/HEAD 本质是读取），
        // 但 POST 有远端副作用——此处动态升级为强制确认，即使 Auto 模式。
        // B1-2 定案：入口识别保留名字匹配（识别"这个特定工具"的运行时参数
        // 升级逻辑，非分类语义；升级判定本身是参数驱动，不静态打标）
        if (it->m_name == "web_fetch") {
            try {
                auto argJson = nlohmann::json::parse(call.m_arguments);
                std::string method = argJson.value("method", "GET");
                std::transform(method.begin(), method.end(), method.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                if (method == "POST") {
                    needConfirm = true;
                    if (m_output) m_output->emitContent("  ⎿ ⚠ POST 请求有远端副作用，需确认\n");
                }
            } catch (...) {
                // 参数解析失败：按默认 GET 处理，不额外升级
            }
        }

        // ================================================================
        // Write 工具的 diff 预览 + 确认流程（设计 §2.1 Step 1–7）
        // ================================================================
        // B1：名字匹配 → m_risk == Write（diff 预览触发）
        bool isWriteTool = (it->m_risk == CLFToolRisk::Write);
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
                                   "Use Auto mode.";
                if (m_output) {
                    m_output->emitContent("  ⎿ ✗ File too large to preview diff. "
                                          "Use Auto mode.\n");
                }
                results.push_back(std::move(result));
                continue;
            }

            // --- Step 3: 渲染 diff 预览 ---
            if (m_output) {
                renderDiff(m_output, preview);
                // 保证刷新
                m_output->emitContent("");
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

        // P0-4: 执行中单行进度（动画帧由 Renderer 附加）——读类工具执行期可见
        if (m_output && (m_labels && m_thinkingSec) && !isWriteTool) {
            std::string toolLine = "  ⎿ " + call.m_name
                                 + (keyParam.empty() ? "" : "(" + keyParam + ")");
            m_output->showProgress({toolLine});
        }

        // --- Step 6 & 7: 执行 handler + 显示结果 ---
        bool toolOk = false;
        std::string toolResultText;
        try {
            CLFLogger::instance().info(
                "[ToolExec] executing: " + call.m_name
                + (keyParam.empty() ? "" : "(" + keyParam + ")"));
            result.m_content = it->m_handler(call.m_arguments);
            // A5-concludesTurn：仅 handler 成功路径复制静态声明（失败分支
            // 保持 false——工具失败不应提前结束回合，设计 §四 T2）
            result.m_concludesTurn = it->m_concludesTurn;
            auto rd = formatToolResult(result.m_content);
            toolOk = rd.ok;
            toolResultText = rd.text;
            CLFLogger::instance().info(
                "[ToolExec] " + call.m_name + " done, ok=" + (toolOk ? "true" : "false")
                + ", result=" + std::to_string(result.m_content.size()) + " chars");

            // F10: 失败（!toolOk）也必须进永久内容——读工具失败的可见性
            // （useProgressive 沿用上方 :345 声明，同作用域不重复声明）
            if (m_output && (!useProgressive || isWriteTool || !toolOk)) {
                // 渐进模式下仅写类工具/失败走永久内容；读类工具成功仅 showProgress
                if (isWriteTool && !preview.diffLines.empty()) {
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
            toolOk = false;
            toolResultText = e.what();
            if (toolResultText.size() > 100) toolResultText = toolResultText.substr(0, 100) + "…";
            CLFLogger::instance().error(
                "[ToolExec] " + call.m_name + " failed: " + e.what());
            if (m_output) m_output->emitContent("  ✗ " + call.m_name
                + (keyParam.empty() ? "" : "(" + keyParam + ")")
                + " — " + toolResultText + " (scroll for full detail)\n");
        }

        // ---- 渐进式统计（summary 数据源；执行中单行进度已在执行前发射） ----
        // P1-2: search 独立成桶（searchCount 在 :351 计数），不再重复计入 read
        if (m_labels && m_thinkingSec) {
            if (isWriteTool) {
                ++progressEdits;
            } else if (it->m_isRead) {   // B1：名字匹配 → 能力标签
                ++progressReads;
            }
        }

        results.push_back(std::move(result));
    }

    // ---- 提交进度总结（P1-2: 增强——总工具数 + search 计数，数据为局部计数器） ----
    if (m_output && m_labels && m_thinkingSec) {
        int elapsed = m_thinkingSec->load();
        std::string summary;
        summary += "● " + m_labels->thought + " for "
                + std::to_string(elapsed) + "s";
        int total = static_cast<int>(calls.size());
        if (total > 0) {
            summary += "，" + std::to_string(total) + " 工具";
            std::string detail;
            if (progressReads > 0)
                detail += "read " + std::to_string(progressReads);
            if (searchCount > 0) {
                if (!detail.empty()) detail += " · ";
                detail += "search " + std::to_string(searchCount);
            }
            if (progressEdits > 0) {
                if (!detail.empty()) detail += " · ";
                detail += "edited " + std::to_string(progressEdits);
            }
            if (!detail.empty())
                summary += " (" + detail + ")";
        }
        // P2-4: usage 缺失（totalTokens==0）时字段省略——不估猜
        if (m_stats.totalTokens > 0)
            summary += " · " + formatTokenCount(m_stats.totalTokens) + " tok";
        summary += " (ctrl+t to expand)";
        guard.commit("\n \n" + summary + "\n \n");
    }

    m_stats.searchCount = searchCount;
    m_stats.readCount   = readCount;
    m_stats.totalCalls  = static_cast<int>(calls.size());

    return results;
}

} // namespace CLF::CLFCore
