// CLFToolExecutor.cpp — 工具调用执行器实现
// 包含 Write 工具的 diff 预览、TOCTOU 校验、确认流程

#include "CLFCore/CLFToolExecutor.hpp"
#include "CLFCore/CLFToolScheduler.hpp"   // 分册 3：execute 兼容壳 = 串行调度
#include "CLFCore/CLFLogger.hpp"
#include "CLFTypes/CLFTextUtil.hpp"
#include "CLFTypes/CLFEventBus.hpp"   // 步骤 2：ToolStarted/ToolFinished 双轨发射
#include "CLFTypes/CLFEvents.hpp"

#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>

#include "CLFCapabilities/FileOps/CLFDiff.hpp"
#include "CLFPluginApi/CLFFileService.hpp"

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

// formatToolResult/ToolResultDisplay 已上提 CLFTextUtil（分册 4 渲染切轨——
// 适配器与 core 同函数重算 ✓/✗ 行，单点维护防口径漂移）

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
// ICLFFileService 回调接收器（C1 接口化）
// ctx 聚合结构 + 静态回调转发（无捕获 → POD 函数指针）
// ============================================================================

// previewEdit 接收：新内容 + 错误
struct EditCollector {
    std::string newContent;
    std::string error;
};

void onSnapContent(void* ctx, const char* data, size_t len) {
    static_cast<FileSnapshot*>(ctx)->content.append(data, len);
}

void onEditContent(void* ctx, const char* data, size_t len) {
    static_cast<EditCollector*>(ctx)->newContent.append(data, len);
}

void onEditError(void* ctx, const char* msg) {
    static_cast<EditCollector*>(ctx)->error = msg;
}

void onPreviewDiffLine(void* ctx, int op, int oldLineNo, int newLineNo, const char* text) {
    auto* p = static_cast<WritePreview*>(ctx);
    p->diffLines.push_back({static_cast<CLF::CLFTools::CLFDiffOp>(op),
                            oldLineNo, newLineNo, text});
}

void onPreviewDiffStats(void* ctx, int added, int removed, int hunks,
                        int truncated, const char* truncReason) {
    auto* s = &static_cast<WritePreview*>(ctx)->diffStats;
    s->added     = added;
    s->removed   = removed;
    s->hunks     = hunks;
    s->truncated = truncated != 0;
    s->truncReason = truncReason ? truncReason : "";
}

// ============================================================================
// prepareWritePreview — 设计 §2.1 Step 1
// ============================================================================

WritePreview prepareWritePreview(CLF::CLFPluginApi::ICLFFileService* fileService,
                                 const CLFToolCall& call) {
    WritePreview preview;
    try {
        auto args = nlohmann::json::parse(call.m_arguments);
        preview.filePath = args.value("path", "");

        // 读取旧文件快照（C1：经 ICLFFileService 回调接收。
        // 读失败静默 = 行为保真——现状忽略 readFileWithSnapshot 返回值，
        // 文件不存在 → 空快照 → "新文件"语义，故 readCb 不设 onError）
        CLF::CLFPluginApi::CLFFileCallbacks readCb;
        readCb.onContent = onSnapContent;
        CLF::CLFPluginApi::CLFFileInfo info;
        fileService->readFile(preview.filePath.c_str(), &info,
                              &preview.oldSnapshot, &readCb);
        preview.oldSnapshot.mtime = info.mtime;
        preview.oldSnapshot.size  = info.size;

        // 准备新旧内容
        std::string oldContent = preview.oldSnapshot.content;
        std::string newContent;

        // B1-7 复查：此处为 write_file/edit_file 的**行为分支**（覆盖 vs 替换，
        // 非分类语义，m_risk 同为 Write 无法区分）——保留名字匹配
        if (call.m_name == "write_file") {
            newContent = args.value("content", "");
            preview.newContent = newContent;
        } else if (call.m_name == "edit_file") {
            std::string oldStr = args.value("old_string", "");
            std::string newStr = args.value("new_string", "");
            EditCollector editCol;
            CLF::CLFPluginApi::CLFFileCallbacks editCb;
            editCb.onContent = onEditContent;
            editCb.onError   = onEditError;
            if (!fileService->previewEdit(oldContent.data(), oldContent.size(),
                                          oldStr.c_str(), newStr.c_str(),
                                          &editCol, &editCb)) {
                preview.valid = false;
                preview.errorMsg = std::move(editCol.error);
                return preview;
            }
            newContent = editCol.newContent;
            preview.newContent = std::move(editCol.newContent);
        } else {
            preview.valid = false;
            preview.errorMsg = "not a write tool";
            return preview;
        }

        // 计算 diff（C1：行流 + 统计经回调直写 preview）
        CLF::CLFPluginApi::CLFFileCallbacks diffCb;
        diffCb.onDiffLine = onPreviewDiffLine;
        diffCb.onStats    = onPreviewDiffStats;
        fileService->computeDiff(oldContent.c_str(), newContent.c_str(),
                                 5, &preview, &diffCb);
        preview.valid = true;
        return preview;
    } catch (const std::exception& e) {
        preview.valid = false;
        preview.errorMsg = std::string("prepareWritePreview error: ") + e.what();
        return preview;
    }
}

// ============================================================================
// buildDiffLines — 构建 diff 渲染行列表（分册 4 渲染切轨：原 renderDiff 的
// 条目构建 + 截断——直调发射移除，行列表经 DiffPreview 事件传适配器）
// ============================================================================

std::vector<std::pair<std::string, int>> buildDiffLines(const WritePreview& preview) {
    const auto& diff  = preview.diffLines;
    const auto& stats = preview.diffStats;
    using LS = CLF::CLFTypes::ICLFContentOutput::LineStyle;
    std::vector<std::pair<std::string, int>> lines;

    if (diff.empty() && !stats.truncated) return lines;

    // 摘要行
    if (stats.truncated) {
        lines.push_back({"  ⎿  " + stats.truncReason, static_cast<int>(LS::Context)});
    } else if (stats.added + stats.removed > 0) {
        std::string info = "  ⎿  +" + std::to_string(stats.added)
                         + " -" + std::to_string(stats.removed);
        if (stats.hunks > 0)
            info += " in " + std::to_string(stats.hunks) + " hunk" + (stats.hunks > 1 ? "s" : "");
        lines.push_back({info, static_cast<int>(LS::Context)});
    }

    if (stats.truncated && diff.empty()) return lines;
    if (diff.empty()) return lines;

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
    // 分册 4：行列表产出（style 数值化——协议用数值，适配器各自映射）
    for (const auto& e : entries)
        lines.push_back({e.text, static_cast<int>(e.style)});
    return lines;
}

} // anonymous namespace

// 协议闭合 appendNotExecutedResults 已随调度循环迁至 CLFToolScheduler.cpp
// （execute 兼容壳经 CLFToolScheduler 调度，中断补位在调度器内完成）

// ============================================================================
// CLFToolExecutor 构造
// ============================================================================

CLFToolExecutor::CLFToolExecutor(std::vector<CLFTool>& tools,
                                 CLFSecurityPolicy& policy,
                                 std::function<bool(const std::string&)> confirmCallback,
                                 ToolStats& stats,
                                 CLF::CLFPluginApi::ICLFFileService* fileService,
                                 CLF::CLFTypes::ICLFContentOutput* contentOutput,
                                 CLF::CLFTypes::ICLFProgressOutput* progressOutput,
                                 std::atomic<bool>* interruptFlag,
                                 const CLFTimerLabels* labels,
                                 std::atomic<int>* thinkingSec,
                                 CLF::CLFTypes::CLFEventBus* eventBus)
    : m_tools(tools)
    , m_securityPolicy(policy)
    , m_confirmCallback(std::move(confirmCallback))
    , m_stats(stats)
    , m_contentOutput(contentOutput)
    , m_progressOutput(progressOutput)
    , m_interruptFlag(interruptFlag)
    , m_labels(labels)
    , m_thinkingSec(thinkingSec)
    , m_fileService(fileService)
    , m_eventBus(eventBus) {
}

// ============================================================================
// 单工具三段（调度器驱动，分册 3 §3.1）+ 批次接口 + 串行兼容壳
// ============================================================================

CLFToolExecutor::~CLFToolExecutor() = default;

const CLFTool* CLFToolExecutor::findTool(const std::string& name) const {
    auto it = std::find_if(m_tools.begin(), m_tools.end(),
        [&](const CLFTool& t) { return t.m_name == name; });
    return it == m_tools.end() ? nullptr : &(*it);
}

void CLFToolExecutor::beginBatch() {
    m_batchGuard = std::make_unique<BatchProgressGuard>(m_progressOutput);
    m_batchSearchCount = m_stats.searchCount;   // 会话累计为基底
    m_batchReadCount   = m_stats.readCount;
    m_batchProgressReads = 0;
    m_batchProgressEdits = 0;
    m_batchDetailLines.clear();
}

void CLFToolExecutor::finishBatch(int totalCalls) {
    // ---- 提交进度总结（P1-2: 增强——总工具数 + search 计数，数据为批次累计） ----
    if (m_progressOutput && m_labels && m_thinkingSec) {
        int elapsed = m_thinkingSec->load();
        std::string summary;
        summary += "● " + m_labels->thought + " for "
                + CLF::CLFCore::formatDurationSeconds(elapsed);
        if (totalCalls > 0) {
            summary += "，" + std::to_string(totalCalls) + " 工具";
            std::string detail;
            if (m_batchProgressReads > 0)
                detail += "read " + std::to_string(m_batchProgressReads);
            if (m_batchSearchCount > 0) {
                if (!detail.empty()) detail += " · ";
                detail += "search " + std::to_string(m_batchSearchCount);
            }
            if (m_batchProgressEdits > 0) {
                if (!detail.empty()) detail += " · ";
                detail += "edited " + std::to_string(m_batchProgressEdits);
            }
            if (!detail.empty())
                summary += " (" + detail + ")";
        }
        // P2-4: usage 缺失（totalTokens==0）时字段省略——不估猜
        // 2026-09-20：本轮实际使用 + 会话累计双数展示（用户方案 A 定案——
        // "本轮"回答"这个回合花了多少"，"累计"保留会话总计口径）
        if (m_stats.totalTokens > 0) {
            summary += " · 本轮 " + formatTokenCount(m_stats.turnTokens) + " tok";
            summary += " · 累计 " + formatTokenCount(m_stats.totalTokens) + " tok";
        }
        // 2026-09-20：详情行直显（替代失效的 "(ctrl+t to expand)" 提示——
        // Ctrl+T 已改作切换思考过程）；单批上限 3 行防长回合刷屏
        static constexpr int kMaxDetailLines = 3;
        for (size_t i = 0; i < m_batchDetailLines.size()
                        && i < static_cast<size_t>(kMaxDetailLines); ++i)
            summary += "\n" + m_batchDetailLines[i];
        if (m_batchDetailLines.size() > static_cast<size_t>(kMaxDetailLines))
            summary += "\n  ↳ … 还有 "
                    + std::to_string(m_batchDetailLines.size() - kMaxDetailLines)
                    + " 个工具";
        m_batchGuard->commit("\n " + summary + "\n");
    }
    m_batchGuard.reset();

    m_stats.searchCount = m_batchSearchCount;
    m_stats.readCount   = m_batchReadCount;
    m_stats.totalCalls  = totalCalls;
}

// prepare — 串行闸门（调度线程逐个串行执行；确认/预览/守卫全在此——
// 弹确认时后续调用不启动，用户定调安全优先）
CLFToolExecutor::PrepareOutcome CLFToolExecutor::prepare(
    const CLFToolCall& call, PreparedCall& pc) {
    PrepareOutcome out;

    // 中断检查（§3.5 检查点：fillPool 启动前 + prepare 内）
    if (isInterrupted()) {
        // 分册 4：中断行移除直调（准死代码——fillPool 前置检查拦截；
        // 回合中断观感由 Interrupted 事件渲染覆盖）
        out.kind = PrepareOutcome::Kind::Aborted;
        return out;
    }

    pc.keyParam = extractKeyParam(call.m_arguments);
    std::string header = "● " + call.m_name;
    if (!pc.keyParam.empty()) {
        header += "(" + pc.keyParam + ")";
    }

    // B1：工具查找前置（能力标签判定需 it；语义微调——不存在的工具不再计入统计，
    // 原名字匹配在查找前会误计幻觉工具名，B1 定案记录）
    auto it = std::find_if(m_tools.begin(), m_tools.end(),
        [&](const CLFTool& t) { return t.m_name == call.m_name; });

    if (it == m_tools.end()) {
        // 分册 4：ToolStarted 事件化发射（not_found 也发——E1 配对纪律；
        // showHeader=false——header 行不显示，与现状一致：unknown 前无 header）
        emitToolStarted(call, pc.keyParam, false);
        out.kind = PrepareOutcome::Kind::Fallback;
        out.fallback.m_toolCallId = call.m_id;
        out.fallback.m_name = call.m_name;
        out.fallback.m_content = std::string("Tool not found: ") + call.m_name;
        out.errorKind = "not_found";
        out.displayError = "  ⎿ ✗ unknown\n";
        return out;
    }

    pc.toolIndex = static_cast<size_t>(it - m_tools.begin());
    const CLFTool& tool = *it;
    pc.useProgressive = (m_labels && m_thinkingSec);
    // 分册 4：ToolStarted 事件化发射（header 条件 core 算好——非渐进 ||
    // Write 显示 header，与现状 emitContent 条件逐字一致）
    emitToolStarted(call, pc.keyParam,
                    !pc.useProgressive || tool.m_risk == CLFToolRisk::Write);

    // 统计（B1：名字匹配 → 能力标签；口径 B1-4：read 桶含 list_directory。
    // prepare 串行闸门内执行——§3.4 天然串行安全；与现状"查找成功后计数"
    // 一致：not_found 不计、blocked/denied 计入）
    if (tool.m_isSearch) ++m_batchSearchCount;
    if (tool.m_isRead)   ++m_batchReadCount;

    // 安全策略检查
    bool needConfirm = false;
    if (!m_securityPolicy.isAllowed(tool.m_risk, needConfirm)) {
        out.kind = PrepareOutcome::Kind::Fallback;
        out.fallback.m_toolCallId = call.m_id;
        out.fallback.m_name = call.m_name;
        out.fallback.m_content = std::string("[Blocked by security policy (mode: ")
                         + m_securityPolicy.getModeName()
                         + ")] 当前模式禁止执行该操作。";
        out.errorKind = "permission";
        out.displayError = "  ⎿ ✗ blocked\n";
        return out;
    }

    // S2-2: 危险命令强制确认——**不受安全模式影响**，Auto 模式同样拦截。
    // 定位为提示层（模型可绕过），仅降低误操作概率，不替代上面的模式管控。
    // B1：名字匹配 → m_risk == Command（确认触发仍由 policy 按内容判定，B1-1 定案）
    if (tool.m_risk == CLFToolRisk::Command) {
        std::string cmdText;
        try {
            auto argJson = nlohmann::json::parse(call.m_arguments);
            cmdText = argJson.value("command", "");
        } catch (...) {
            cmdText = call.m_arguments;  // 解析失败则按原文匹配，宁可多确认
        }
        if (m_securityPolicy.isDangerousCommand(cmdText)) {
            needConfirm = true;
            // 分册 4：⚠ 行事件化（随 ToolFinished——渲染序 ⚠ → ✗/✓）
            out.warningText = "  ⎿ ⚠ 命中危险命令模式，需确认\n";
        }
    }

    // S2-5: web_fetch 工具本身按 Read 级注册（GET/HEAD 本质是读取），
    // 但 POST 有远端副作用——此处动态升级为强制确认，即使 Auto 模式。
    // B1-2 定案：入口识别保留名字匹配（识别"这个特定工具"的运行时参数
    // 升级逻辑，非分类语义；升级判定本身是参数驱动，不静态打标）
    if (tool.m_name == "web_fetch") {
        try {
            auto argJson = nlohmann::json::parse(call.m_arguments);
            std::string method = argJson.value("method", "GET");
            std::transform(method.begin(), method.end(), method.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (method == "POST") {
                needConfirm = true;
                out.warningText = "  ⎿ ⚠ POST 请求有远端副作用，需确认\n";
            }
        } catch (...) {
            // 参数解析失败：按默认 GET 处理，不额外升级
        }
    }

    // ================================================================
    // Write 工具的 diff 预览 + 确认流程（设计 §2.1 Step 1–7）
    // ================================================================
    // B1：名字匹配 → m_risk == Write（diff 预览触发）
    pc.isWriteTool = (tool.m_risk == CLFToolRisk::Write);

    if (pc.isWriteTool) {
        // --- Step 1: prepareWritePreview ---
        WritePreview preview = prepareWritePreview(m_fileService, call);

        // --- Step 1.5: valid 检查 ---
        if (!preview.valid) {
            out.kind = PrepareOutcome::Kind::Fallback;
            out.fallback.m_toolCallId = call.m_id;
            out.fallback.m_name = call.m_name;
            out.fallback.m_content = preview.errorMsg;
            out.errorKind = "";
            out.displayError = "  ⎿ ✗ " + preview.errorMsg + "\n";
            return out;
        }

        // --- Step 2: 超限阻断（仅需确认模式）---
        if (preview.diffStats.truncated && needConfirm) {
            out.kind = PrepareOutcome::Kind::Fallback;
            out.fallback.m_toolCallId = call.m_id;
            out.fallback.m_name = call.m_name;
            out.fallback.m_content = "File too large to preview diff. "
                               "Use Auto mode.";
            out.errorKind = "";
            out.displayError = "  ⎿ ✗ File too large to preview diff. "
                               "Use Auto mode.\n";
            return out;
        }

        // 预览摘要抄入 pc（finalize 的 diff 行显示 + DiffPreview 事件用）
        pc.previewFilePath = preview.filePath;
        pc.previewHasDiffLines = !preview.diffLines.empty();
        pc.previewDiffTruncated = preview.diffStats.truncated;
        pc.previewAdded = preview.diffStats.added;
        pc.previewRemoved = preview.diffStats.removed;
        pc.previewHunks = preview.diffStats.hunks;

        // --- Step 3: 渲染 diff 预览（分册 4 事件化：行列表 → DiffPreview） ---
        pc.previewDiffLines = buildDiffLines(preview);
        emitDiffPreview(pc);

        // --- Step 4: 模式分流 ---
        if (needConfirm && m_confirmCallback) {
            std::string prompt = "工具 [" + call.m_name + "] 将修改文件: "
                               + preview.filePath;
            if (!m_confirmCallback(prompt)) {
                out.kind = PrepareOutcome::Kind::Fallback;
                out.fallback.m_toolCallId = call.m_id;
                out.fallback.m_name = call.m_name;
                out.fallback.m_content = "[Denied by user] 用户拒绝了该操作。";
                out.errorKind = "permission";
                out.displayError = "  ⎿ ✗ denied\n";
                // 中断检查留给调度器下一轮 fillPool（denied 结果照常提交，
                // 剩余由调度器补"未执行"——与现状 denied+中断路径等价）
                return out;
            }
        }

        // --- Step 5: TOCTOU 校验（C1：经 ICLFFileService::getFileInfo）---
        {
            auto current = m_fileService->getFileInfo(preview.filePath.c_str());
            if ((preview.oldSnapshot.mtime != 0 && current.mtime != 0 &&
                 current.mtime != preview.oldSnapshot.mtime) ||
                (preview.oldSnapshot.size != 0 && current.size != 0 &&
                 current.size != preview.oldSnapshot.size)) {
                out.kind = PrepareOutcome::Kind::Fallback;
                out.fallback.m_toolCallId = call.m_id;
                out.fallback.m_name = call.m_name;
                out.fallback.m_content = "File modified after preview. Please review again.";
                out.errorKind = "";
                out.displayError = "  ⎿ ✗ File modified after preview. Please review again.\n";
                return out;
            }
        }
    } else {
        // 非 Write 类工具（execute_command 等 Command risk）的确认检查
        if (needConfirm && m_confirmCallback) {
            std::string prompt = "工具 [" + call.m_name + "] 需要执行高风险操作。\n"
                               + "参数: " + call.m_arguments;
            if (!m_confirmCallback(prompt)) {
                out.kind = PrepareOutcome::Kind::Fallback;
                out.fallback.m_toolCallId = call.m_id;
                out.fallback.m_name = call.m_name;
                out.fallback.m_content = "[Denied by user] 用户拒绝了该操作。";
                out.errorKind = "permission";
                out.displayError = "  ⎿ ✗ denied\n";
                return out;
            }
        }
    }

    // P0-4: 执行中单行进度（动画帧由 Renderer 附加）——读类工具执行期可见。
    // 并行时多工具先后 showProgress → 单行覆盖先行（用户拍板：最后启动者
    // 显示——设计 §六 验收 4）
    if (m_progressOutput && (m_labels && m_thinkingSec) && !pc.isWriteTool) {
        std::string toolLine = "  ⎿ " + call.m_name
                             + (pc.keyParam.empty() ? "" : "(" + pc.keyParam + ")");
        m_progressOutput->showProgress({toolLine});
    }

    out.kind = PrepareOutcome::Kind::Ready;
    return out;
}

// run — 唯一并发段（§3.3 dispatch）：handler 调用 + 异常捕获 + 失败
// 判定。零 UI 输出、零事件、零统计（日志经 CLFLogger 互斥安全）。
// ok 判定在 run 内完成（formatToolResult 纯函数）——settle 时失败信号
// 即对调度器可见（fail-fast 停止派发，dsh §8）；finalize 复用不再重判
CLFToolExecutor::RunOutcome CLFToolExecutor::run(
    const CLFToolCall& call, const PreparedCall& pc) {
    RunOutcome out;
    out.result.m_toolCallId = call.m_id;
    out.result.m_name       = call.m_name;
    const CLFTool& tool = m_tools[pc.toolIndex];
    try {
        CLFLogger::instance().info(
            "[ToolExec] executing: " + call.m_name
            + (pc.keyParam.empty() ? "" : "(" + pc.keyParam + ")"));
        out.result.m_content = tool.m_handler(call.m_arguments);
        // A5-concludesTurn：仅 handler 成功路径复制静态声明（失败分支
        // 保持 false——工具失败不应提前结束回合，设计 §四 T2）
        out.result.m_concludesTurn = tool.m_concludesTurn;
    } catch (const std::exception& e) {
        out.exceptionCaught = true;
        out.result.m_content = std::string("Tool execution error: ") + e.what();
        out.displayError = e.what();
        if (out.displayError.size() > 100)
            out.displayError = out.displayError.substr(0, 100) + "…";
        CLFLogger::instance().error(
            "[ToolExec] " + call.m_name + " failed: " + e.what());
    }

    // 失败判定（settle 时信号——调度器 fail-fast 依据；并发段内纯函数安全）
    if (out.exceptionCaught) {
        out.ok = false;
        out.resultText = out.displayError;
    } else {
        auto rd = formatToolResult(out.result.m_content);
        out.ok = rd.ok;
        out.resultText = rd.text;
        out.resultLines = rd.lines;
        out.resultChars = rd.chars;
    }
    return out;
}

// finalize — 提交游标内（模型序串行）：结果格式化 + 统计/详情行 +
// ToolFinished 事件（✓/✗ 行全文 core 拼好——displayResult）。Fallback
// 结果不经 run（prepare 合成），ok 恒 true——不触发失败静默（denied/
// blocked 非 handler 失败，后续照常——现状语义）。
CLFToolExecutor::FinalizeOutcome CLFToolExecutor::finalize(
    const CLFToolCall& call, const PreparedCall& pc, RunOutcome&& raw,
    bool isFallback, const std::string& errorKind,
    const std::string& displayError, const std::string& warningText) {
    FinalizeOutcome out;
    out.result = std::move(raw.result);

    if (isFallback) {
        // 分册 4：✗ 行全文随事件（prepare 拼好——displayError）；渲染序
        // ⚠ → ✗ 由适配器按字段序直显
        emitToolFinished(out.result, false, errorKind, "", displayError, warningText);
    } else {
        const CLFTool& tool = m_tools[pc.toolIndex];
        // ok/展示文本已在 run 内判定（settle 时信号——调度器 fail-fast）
        const bool toolOk = raw.ok;
        const std::string& toolResultText = raw.resultText;
        CLFLogger::instance().info(
            "[ToolExec] " + call.m_name + " done, ok=" + (toolOk ? "true" : "false")
            + ", result=" + std::to_string(out.result.m_content.size()) + " chars");
        out.ok = toolOk;

        // 分册 4：✓/✗ 行全文 core 拼好（原 emitContent 段逐字搬移——
        // displayResult；空 = 不显示（渐进读工具成功仅进度条））。
        // F10: 失败（!toolOk）也必须进永久内容——读工具失败的可见性
        std::string displayResult;
        if (!pc.useProgressive || pc.isWriteTool || !toolOk) {
            // 渐进模式下仅写类工具/失败走永久内容；读类工具成功仅 showProgress
            if (pc.isWriteTool && pc.previewHasDiffLines) {
                if (pc.previewAdded + pc.previewRemoved > 0 && !pc.previewDiffTruncated) {
                    displayResult =
                        "  ✓ " + call.m_name + "(" + pc.previewFilePath
                        + ") — +" + std::to_string(pc.previewAdded)
                        + " -" + std::to_string(pc.previewRemoved) + " lines"
                        + ", " + std::to_string(pc.previewHunks) + " hunk"
                        + (pc.previewHunks > 1 ? "s" : "") + "\n";
                } else if (pc.previewDiffTruncated) {
                    displayResult =
                        "  ✓ " + call.m_name + "(" + pc.previewFilePath
                        + ") — written (diff truncated)\n";
                } else {
                    displayResult =
                        "  ✓ " + call.m_name + "(" + pc.previewFilePath
                        + ") — written\n";
                }
            } else if (toolOk && toolResultText.size() <= 200) {
                displayResult = "  ✓ " + call.m_name
                    + (pc.keyParam.empty() ? "" : "(" + pc.keyParam + ")") + "\n";
            } else if (toolOk) {
                displayResult = "  ✓ " + call.m_name
                    + (pc.keyParam.empty() ? "" : "(" + pc.keyParam + ")")
                    + " — " + std::to_string(raw.resultLines) + " lines, "
                    + std::to_string(raw.resultChars) + " chars\n";
            } else {
                auto reason = toolResultText.size() > 100
                    ? toolResultText.substr(0, 100) + "…" : toolResultText;
                displayResult = "  ✗ " + call.m_name
                    + (pc.keyParam.empty() ? "" : "(" + pc.keyParam + ")")
                    + " — " + reason + " (scroll for full detail)\n";
            }
        }

        // ---- 渐进式统计（summary 数据源；执行中单行进度已在 prepare 发射） ----
        // P1-2: search 独立成桶（m_batchSearchCount 在 prepare 计数），不再重复计入 read
        if (m_labels && m_thinkingSec) {
            if (pc.isWriteTool) {
                ++m_batchProgressEdits;
            } else if (tool.m_isRead) {   // B1：名字匹配 → 能力标签
                ++m_batchProgressReads;
            }
            // 2026-09-20：详情行收集（与 summary 同条件——渐进模式才有
            // summary 行；↳ 弱化前缀，成功行仅文件名、失败行带 ✗ 原因）
            std::string dl = "  ↳ " + call.m_name
                + (pc.keyParam.empty() ? "" : "(" + pc.keyParam + ")");
            if (!toolOk) {
                dl += " — ✗ "
                    + (toolResultText.size() > 60
                        ? toolResultText.substr(0, 60) + "…" : toolResultText);
            }
            m_batchDetailLines.push_back(std::move(dl));
        }

        // ToolFinished（errorKind 为空——handler 路径的失败/成功由 ok 表达；
        // 分册 4：✓/✗ 行全文 + ⚠ 行随事件）
        emitToolFinished(out.result, toolOk, "", displayResult, "", warningText);
    }

    // T3: 每次调用末刷新（设计-任务清单UI显示 §3.4）——todo_write 等状态类
    // 工具返回即重绘（原仅靠 turnTimer 1Hz 兜底，最长延迟 1 秒）。
    // finalize 在提交游标内串行——每调用一次（含 fallback，T12 语义）
    if (m_progressOutput) m_progressOutput->requestRefresh();
    return out;
}

// 分册 4：ToolStarted 事件发射（prepare 内——header 条件 core 算好）
void CLFToolExecutor::emitToolStarted(const CLFToolCall& call,
                                      const std::string& keyParam,
                                      bool showHeader) {
    if (!m_eventBus) return;
    CLF::CLFTypes::CLFAgentEvent ev;
    ev.m_type = CLF::CLFTypes::CLFEventType::ToolStarted;
    ev.m_callId = call.m_id;
    ev.m_name = call.m_name;
    ev.m_argsPreview = keyParam;
    ev.m_showHeader = showHeader;
    m_eventBus->publish(ev);
}

void CLFToolExecutor::emitToolFinished(const CLFToolResult& r, bool ok,
                                       const std::string& errorKind,
                                       const std::string& displayResult,
                                       const std::string& displayError,
                                       const std::string& warningText) {
    if (!m_eventBus) return;
    CLF::CLFTypes::CLFAgentEvent ev;
    ev.m_type = CLF::CLFTypes::CLFEventType::ToolFinished;
    ev.m_callId = r.m_toolCallId;
    ev.m_name = r.m_name;
    ev.m_ok = ok;
    ev.m_summary = r.m_content;
    ev.m_errorKind = errorKind;
    ev.m_displayResult = displayResult;
    ev.m_displayError = displayError;
    ev.m_warningText = warningText;
    m_eventBus->publish(ev);
}

// 分册 4：DiffPreview 事件发射（prepare Write 路径——renderDiff 直调移除后；
// 行列表 = buildDiffLines 的截断后最终条目，适配器逐条 emitStyledLine 重建）
void CLFToolExecutor::emitDiffPreview(const PreparedCall& pc) {
    if (!m_eventBus) return;
    CLF::CLFTypes::CLFAgentEvent ev;
    ev.m_type = CLF::CLFTypes::CLFEventType::DiffPreview;
    ev.m_text = pc.previewFilePath;
    ev.m_lines = pc.previewDiffLines;
    ev.m_diffAdded = pc.previewAdded;
    ev.m_diffRemoved = pc.previewRemoved;
    ev.m_diffHunks = pc.previewHunks;
    ev.m_diffTruncated = pc.previewDiffTruncated;
    m_eventBus->publish(ev);
}

// 兼容壳：串行调度（maxParallel=1）——第三批 AgentLoop 切轨前既有调用点
// 零行为变化；qa_CLFToolExecutor 串行等价回归同经此壳
std::vector<CLFToolResult> CLFToolExecutor::execute(
    const std::vector<CLFToolCall>& calls) {
    CLFToolScheduler scheduler(*this, 1);
    auto r = scheduler.schedule(calls);
    return std::move(r.m_results);
}

} // namespace CLF::CLFCore
