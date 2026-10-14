// CLFAgentLoop.cpp — Agent 主循环实现（tool-calling 编排 + 流式/同步双路径）

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFMessageCodec.hpp"
#include "CLFCore/CLFRetryPolicy.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"
#include "CLFCore/CLFStreamAccumulator.hpp"
#include "CLFTypes/CLFPeriodicTimer.hpp"
#include "CLFTypes/CLFTextUtil.hpp"
#include "CLFTypes/CLFEventBus.hpp"   // 事件化重构步骤 2：双轨发射
#include "CLFTypes/CLFEvents.hpp"
#include "CLFNetwork/CLFThinkingIndicator.hpp"
#include "CLFCore/CLFToolExecutor.hpp"
#include "CLFCore/CLFToolScheduler.hpp"   // 分册 3 第三批：工具执行切轨调度器
#include "CLFNetwork/CLFHttpClient.hpp"
#include "CLFCapabilities/FileOps/CLFFileServiceImpl.hpp"
#include "CLFPluginApi/CLFFileService.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

namespace CLF::CLFCore {

namespace fs = std::filesystem;

namespace {

// A5-触顶收尾（设计-工具调用循环上限机制改造 §3.2）：
// 循环耗尽后追加给模型的收尾提示（仅存 context 内存，不进 jsonl）
constexpr const char* kTurnCapWrapUpPrompt =
    "已达本轮工具调用上限。请总结已完成的工作与剩余工作，不要调用工具。";

// （分册 2 块 1：kTurnCapNotice 迁适配器——core 不再拼提示文案）

} // anonymous namespace

CLFAgentLoop::CLFAgentLoop(const CLFAgentConfig& config,
                           std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> httpClient,
                           const CLFTimerLabels& labels,
                           CLF::CLFPluginApi::ICLFFileService* fileService)
    : m_config(config)
    , m_labels(labels)
    , m_context()
    , m_window(config.m_maxContextWindow)
    , m_httpClient(httpClient ? httpClient
                              : std::make_shared<CLF::CLFNetwork::CLFHttpClient>(
                                    config.m_apiBaseUrl, config.m_apiKey))
    , m_fileService(fileService)
    , m_securityPolicy(CLFSecurityPolicy::modeFromString(config.m_securityMode))
    , m_summaryCache(m_config, m_httpClient) {
    if (!m_fileService) {
        // C1 兜底：进程内默认实现；阶段 2 试点经构造参数注入 DLL 工厂实例
        m_fileServiceOwner = std::make_unique<CLF::CLFCapabilities::CLFFileServiceImpl>();
        m_fileService = m_fileServiceOwner.get();
    }
    m_httpClient->setTimeout(config.m_maxResponseDelaySec);
    m_securityPolicy.setCommandAllowlist(config.m_commandAllowlist);  // S2-2
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

// ============================================================================
// 事件化重构步骤 0：回合 RAII 辅助（runTurn Section 拆分，2026-10-13）
// ============================================================================

// ② 回合计时 RAII（原 runTurn :158-188 搬移）：
// Timer #2——转圈动画驱动（50ms = 20Hz）。用 CLFPeriodicTimer（条件变量
// 唤醒）而非 sleep 轮询：后者 join 时平均要空等 ~0.8s。异常兜底已在定时器
// 内部（B1 教训：线程体逸出异常会 std::terminate，静默退出码 3）。
// F13 升级（2026-09-09 D4 定案）：1Hz → 20Hz——CPR 查询关闭后转圈动画
// 失去事件源；50ms 驱动让转圈（100ms/帧）恢复流畅，仅 turn 生命周期内驱动。
// 分册 2 块 1（设计 4.2 审查补充）：状态行计时文案迁适配器自计时
// （StatusTimer）——guard 职责只剩转圈 requestRefresh，一分为二。
class CLFAgentLoop::TurnTimerGuard {
public:
    explicit TurnTimerGuard(CLFAgentLoop* loop)
        : m_loop(loop)
        , m_out(loop->m_output)  // 析构清理用构造时快照（原 TurnGuard 语义）
        , m_timer(std::chrono::milliseconds(50), [this]() {
              if (!m_loop->m_output) return;
              m_loop->m_output->requestRefresh();
          }) {}

    ~TurnTimerGuard() {
        m_timer.stop();               // 立即唤醒退出
        if (m_out) m_out->setStatus("");
    }

    // finishTurn 的计时基准（原 turnStart 局部变量）
    std::chrono::steady_clock::time_point turnStart() const { return m_turnStart; }

private:
    CLFAgentLoop* m_loop;
    CLF::CLFTypes::ICLFOutput* m_out;
    std::chrono::steady_clock::time_point m_turnStart{std::chrono::steady_clock::now()};
    CLF::CLFTypes::CLFPeriodicTimer m_timer;
};

// ③ 迭代思考计时 RAII（原 runTurn :200-203 搬移）：Timer #1——只计数
// （不调用 output，安全），供 executor 渐进显示注入（sec()）。
// 声明顺序要紧：m_sec 必须先于 m_timer，析构逆序才能保证定时器线程
// 停止后计数器才被销毁（原注释语义）。定时器析构即 stop，无需额外 guard。
class CLFAgentLoop::TurnThinkingClock {
public:
    TurnThinkingClock()
        : m_timer(std::chrono::seconds(1), [this]() { m_sec.fetch_add(1); }) {}

    std::atomic<int>& sec() { return m_sec; }

private:
    std::atomic<int> m_sec{0};
    CLF::CLFTypes::CLFPeriodicTimer m_timer;
};

// ============================================================================
// 回合编排（事件化重构步骤 0：runTurn 拆 5 Section + 二级函数化，
// 纯搬移零行为变化——分册 1 §1.2）
// ============================================================================

std::string CLFAgentLoop::runTurn(const std::string& userInput) {
    // ① 回合准备（日志/中断重置/摘要/记账/状态点；摘要期被中断 → 早退）
    if (!prepareTurn(userInput)) return std::string("[Interrupted]");

    // ② 回合计时 RAII：50ms 状态行计时 + 转圈动画驱动（析构停表复位状态行）
    TurnTimerGuard timer(this);
    std::string finalContent;
    int consecutiveErrors = 0;
    // A5-concludesTurn：任一工具结果声明回合结束 → 停止工具循环
    // （但仍给模型一次收尾发言机会，见 hasToolCalls 分支的 concluded break）
    bool concluded = false;
    // 模型自发回合结束宣告（2026-09-23 用户拍板）：applySelfWorkedMark
    // 命中即置位——尊重宣告：保留标记行、截断其后内容、丢弃已声明的
    // tool_calls（宣告结束 → 不执行）；收尾不重复拼 worked（模型已宣告）
    bool modelDeclaredEnd = false;

    for (int iteration = 0; iteration < m_config.m_maxToolCallIterations; ++iteration) {
        // ③ 迭代思考计时 RAII（1s 计数，供 executor 渐进显示；析构即停）
        TurnThinkingClock thinking;
        // 每次循环迭代前检查中断
        if (m_interrupted) return abortTurn();
        try {
            // ④ 单次模型请求（流式/同步收敛 + 重试 + 防御 + usage）
            TurnRequestResult req =
                requestAssistantTurn(iteration, consecutiveErrors, modelDeclaredEnd);
            if (req.m_action == TurnRequestResult::Action::ReturnError)
                return std::string("[Error] ") + req.m_errorText;
            if (req.m_action == TurnRequestResult::Action::ReturnInterrupted)
                return std::string("[Interrupted]");
            if (req.m_action == TurnRequestResult::Action::Retry) continue;

            // ⑤ 累积文本（思考模型可能在 reasoning_content 中耗尽 token，
            // content 为空但 finish_reason 为 "length" 或 "stop"）
            if (!req.m_response.m_content.empty()) {
                if (!finalContent.empty()) finalContent += "\n";
                finalContent += req.m_response.m_content;
            }
            if (finalContent.empty() && req.m_response.m_finishReason == "length") {
                finalContent = "(响应被截断 — 思考时间过长，token 已达上限)";
            }

            // ⑥ tool_calls → 执行并继续循环
            if (CLFProtocolAdapter::hasToolCalls(req.m_response)) {
                // A5-concludesTurn：工具已声明回合结束，模型却仍发工具调用 →
                // 丢弃（不执行、不回填、不 addAssistantToolCalls）。
                // 协议安全：模型文本已在上方累积进 finalContent，无悬空
                // tool_calls 需 result 配对；break 后经 finishTurn() 正常收尾，
                // 不落触顶路径（设计 §3.1 改动 3）
                if (concluded) break;
                // 中断检查：流式被 abort 后可能累积了 tool call，执行前再检查
                if (m_interrupted) return abortTurn();
                // ⑦ 工具执行（addAssistantToolCalls + executor + 结果回填）
                ToolExecutionOutcome oc = executeToolCalls(
                    req.m_response.m_toolCalls, req.m_response.m_content, thinking);
                if (oc.m_interrupted) return std::string("[Interrupted]");
                if (oc.m_concluded) concluded = true;
                continue;
            }

            // ⑧ 完成 → 收尾汇聚点（A5 §3.2.1）：自然停 / concluded-break 共用
            // A5-C：max-tokens 收尾——finish_reason == "length" 时状态点
            // Warn（现状 Done）；已生成内容保留不回滚（设计 §3.3）。
            // 边界：length 且带 tool_calls 不落此分支（上方已 continue）
            const auto finishKind = (req.m_response.m_finishReason == "length")
                ? CLF::CLFTypes::ICLFOutput::StatusKind::Warn
                : CLF::CLFTypes::ICLFOutput::StatusKind::Done;
            return finishTurn(finalContent, timer.turnStart(), finishKind,
                              modelDeclaredEnd);

        } catch (const std::exception& e) {
            if (++consecutiveErrors >= CLFRetryPolicy::kMaxRetries) {
                if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Error);
                return std::string("[Error] Exception: ") + e.what();
            }
            if (m_output) m_output->emitContent(
                "\n✗ Exception: " + std::string(e.what())
                + " — retry " + std::to_string(consecutiveErrors)
                + "/" + std::to_string(CLFRetryPolicy::kMaxRetries) + "\n");
            // 可中断等待
            for (int w = 0; w < 20 && !m_interrupted; ++w)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // A5-concludesTurn：模型未按声明停止（发了工具调用被丢弃）→
    // 已给最终发言机会，直接复用完成分支收尾（Done，设计 §3.1 改动 3）
    if (concluded) {
        return finishTurn(finalContent, timer.turnStart(),
                          CLF::CLFTypes::ICLFOutput::StatusKind::Done);
    }

    // ⑨ 触顶收尾（A5-B：优雅收尾，设计 §3.2）
    return runWrapUp(finalContent, timer.turnStart());
}

// ============================================================================
// Section 函数（runTurn 拆分）
// ============================================================================

// ① 回合准备（原 runTurn :80-156 搬移；applySelfWorkedMark / accumulateUsage
// 两个 lambda 已成员化）：日志 → 中断重置 → 清推理 → 自动摘要 → 记账 →
// Running 状态点。返回 false = 摘要期间被中断（emitInterrupted 已发射）
bool CLFAgentLoop::prepareTurn(const std::string& userInput) {
    CLFLogger::instance().info("[Turn] start, input="
        + (userInput.size() > 80 ? userInput.substr(0, 80) + "..." : userInput));
    // 事件化重构步骤 2：回合稳定 id + TurnStarted 双轨发射（归属表 §2.3）
    m_currentTurnId = "turn-" + std::to_string(++m_turnSeq);
    if (m_eventBus) {
        CLF::CLFTypes::CLFAgentEvent ev;
        ev.m_type = CLF::CLFTypes::CLFEventType::TurnStarted;
        ev.m_turnId = m_currentTurnId;
        ev.m_text = userInput;
        m_eventBus->publish(ev);
    }
    m_interrupted = false;  // 新 turn 开始时重置中断标志
    if (m_output) m_output->clearThinking();  // 清空上一轮推理内容
    if (m_eventBus) {
        CLF::CLFTypes::CLFAgentEvent ev;
        ev.m_type = CLF::CLFTypes::CLFEventType::ThinkingCleared;
        ev.m_turnId = m_currentTurnId;
        m_eventBus->publish(ev);
    }
    m_lastReasoningSize = 0;  // 重置推理增量追踪
    // S3-1: 自动摘要（先于 addMessage 与任何 API 调用；频控 + 阈值双条件，
    // 设计 §S3-1）——生成（同步 LLM，失败自动规则降级）→ 落盘 summary 行
    // → 重建系统提示（摘要经 Builder 段落注入，system 永不截断）
    static constexpr int kSummaryCooldownTurns = 10;
    // A2（2026-09-23 中断时效性 §十二）：摘要入口检查——摘要同步 LLM 期间按
    // ESC：请求本身可被 abort（onInterrupt → httpClient->abort），但入口与
    // 返回后原本无中断检查
    if (m_config.m_contextCompression
        && !m_interrupted
        && m_summaryCache.turnsSinceLast() >= kSummaryCooldownTurns
        && shouldSummarize()) {
        CLFLogger::instance().info("[Summary] auto trigger (remaining window below "
            + std::to_string(m_config.m_autoSummaryThreshold) + ")");
        generateAndCacheSummary();
        // A2：返回后检查——摘要期间的中断在此被观察到；user 消息尚未入库，
        // 提前返回不产生会话残留
        if (m_interrupted) {
            emitInterrupted();
            return false;
        }
        appendSummaryLineNow();
        rebuildSystemMessage();
        m_summaryCache.resetCooldown();
    } else {
        m_summaryCache.noteTurn();
    }
    // J3: 轮初消息数（appendTurnLine 的差集基准；user 消息计入本轮新增）
    m_sessionFileCtx.setTurnStartMsgCount(m_context.getMessages().size());
    m_context.addMessage("user", userInput);
    m_lastToolStats = {};
    // 2026-09-20：本轮 token 快照（"本轮实际使用"展示口径——回合内所有
    // API 调用的 usage 之和；触顶 wrapUp 在 summary 显示之后累加、计入
    // 下一轮，与累计生命周期一致）
    m_turnStartTokens = m_totalTokensUsed;
    // P1-1: 状态点接线——Running 于 turn 开始
    if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Running);
    // 事件化重构步骤 2：StatusChanged 双轨发射
    if (m_eventBus) {
        CLF::CLFTypes::CLFAgentEvent ev;
        ev.m_type = CLF::CLFTypes::CLFEventType::StatusChanged;
        ev.m_turnId = m_currentTurnId;
        ev.m_statusKind =
            static_cast<int>(CLF::CLFTypes::ICLFOutput::StatusKind::Running);
        m_eventBus->publish(ev);
    }
    return true;
}

// ④ 单次模型请求：buildRequestBody → (streamRequest ∥ syncRequest) → finalizeResponse
// 防御意义（总纲决策 24③）：拆分后常规回合的模型请求唯一入口 = 本函数
// （流式/同步两路径收敛单点；触顶 wrapUp 复用 buildRequestBody 走
// requestWrapUpBody——无重试语义）——"程序零自动提交"从事实变为可审计结构
CLFAgentLoop::TurnRequestResult CLFAgentLoop::requestAssistantTurn(int iteration, int& consecutiveErrors,
                                                                   bool& modelDeclaredEnd) {
    const std::string body = buildRequestBody(m_config);
    CLFAssistantResponse parsed;
    if (m_config.m_stream) {
        TurnRequestResult r = streamRequest(body, iteration, consecutiveErrors, parsed);
        if (r.m_action != TurnRequestResult::Action::Continue) return r;
    } else {
        TurnRequestResult r = syncRequest(body, consecutiveErrors, parsed);
        if (r.m_action != TurnRequestResult::Action::Continue) return r;
    }
    // 两路径后处理收敛：自发结束标记 + usage + finish_reason 检查
    return finalizeResponse(parsed, modelDeclaredEnd);
}

// ⑦ 工具执行段（原 runTurn :410-427 搬移）：addAssistantToolCalls →
// 调度器 schedule → 结果回填（入库前截断）→ concluded/中断判定。
// 分册 3 第三批（2026-10-14）：切轨 CLFToolScheduler（混合并发调度，
// maxParallelToolCalls 配置注入）——未声明并发安全的工具 = 独占 =
// 串行现行为，声明一个放开一个
CLFAgentLoop::ToolExecutionOutcome CLFAgentLoop::executeToolCalls(
    const std::vector<CLFToolCall>& toolCalls, const std::string& assistantContent,
    TurnThinkingClock& thinking) {
    m_context.addAssistantToolCalls(toolCalls, assistantContent);
    CLFToolExecutor executor(m_tools, m_securityPolicy,
                             m_confirmCallback, m_lastToolStats,
                             m_fileService, m_output, m_output,
                             &m_interrupted, &m_labels, &thinking.sec(),
                             m_eventBus);   // 步骤 2：ToolStarted/ToolFinished 双轨
    ToolExecutionOutcome outcome;
    CLFToolScheduler scheduler(executor, m_config.m_maxParallelToolCalls);
    auto r = scheduler.schedule(toolCalls);
    for (const auto& result : r.m_results) {
        // C2b：tool result 内容截断在入库前（容器不再含内容策略）
        m_context.addToolResult(
            result.m_toolCallId, result.m_name,
            CLFTextUtil::truncateToolResult(result.m_content));
        if (result.m_concludesTurn) outcome.m_concluded = true;
    }
    // 失败静默上报（§3.6"以首个失败上报"）：最小落地 = 日志记录——行为
    // 保持（结果链已完整回填：失败结果 + [skipped] 合成，模型自行决定
    // 后续）。回合提前终止等更强语义待用户实机感受后拍板（记录偏差）
    if (r.m_firstFailure) {
        CLFLogger::instance().warn(
            "[Turn] first tool failure (quiesced): " + r.m_firstFailure->m_name);
    }
    if (m_interrupted) {
        emitInterrupted();
        outcome.m_interrupted = true;
    }
    return outcome;
}

// ⑨ 触顶收尾（原 runTurn :464-520 搬移，A5-B：优雅收尾，设计 §3.2）：
// 触顶 = 阶段完成点，非报错。循环耗尽时 context 尾部是 tool result，
// 模型无总结机会 → 触顶收尾请求（同步 + 独立 try/catch：循环内 catch
// 管不到这里，任何失败不逸出、不重试，降级返回已累积内容 + 提示文案）
std::string CLFAgentLoop::runWrapUp(std::string& finalContent,
                                    std::chrono::steady_clock::time_point turnStart) {
    if (m_interrupted) return abortTurn();
    m_context.addMessage("user", kTurnCapWrapUpPrompt);
    CLFLogger::instance().info("[Turn] max tool iterations reached ("
        + std::to_string(m_config.m_maxToolCallIterations) + "), wrap-up request");
    if (!requestWrapUpBody(finalContent)) return std::string("[Interrupted]");
    // 顺序：总结文本 → ✻ worked（分册 2 块 1 迁适配器 §3.2.1）。触顶提示
    // 文案同迁（适配器按 TurnFinished.capReached 渲染）——finalContent 纯净
    // P1-1: 迭代上限——任务未完成语义，Warn（对齐 dsh max-tokens=warning）
    if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Warn);
    // 事件化重构步骤 2：TurnFinished 双轨发射（触顶路径——capReached 标志）
    // 分册 2：m_text = finalContent（落库数据源——writer 据此重建 assistant 消息）
    if (m_eventBus) {
        CLF::CLFTypes::CLFAgentEvent ev;
        ev.m_type = CLF::CLFTypes::CLFEventType::TurnFinished;
        ev.m_turnId = m_currentTurnId;
        ev.m_text = finalContent;
        ev.m_workedSec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - turnStart).count();
        ev.m_todos = m_todoStore.getTodos();
        ev.m_capReached = true;
        m_eventBus->publish(ev);
    }
    return m_config.m_stream ? std::string() : finalContent;
}

// ============================================================================
// 二级函数（Section 内部拆分）
// ============================================================================

// C2b：发 API 前窗口截断（system 永不截断，尾部保留）。
// 流式/同步/触顶 wrapUp 三处请求体构造收敛（wrapUp 传 stream=false 变体）
std::string CLFAgentLoop::buildRequestBody(const CLFAgentConfig& cfg) const {
    return m_protocolAdapter.buildChatRequest(
        m_window.apply(m_context.getMessages()), m_tools, cfg);
}

// 流式请求：日志 → postJsonStream（回调分发见 handleStreamDelta）→
// 错误/中断早退 → 成功回填 parsed
CLFAgentLoop::TurnRequestResult CLFAgentLoop::streamRequest(const std::string& body, int iteration,
                                                            int& consecutiveErrors,
                                                            CLFAssistantResponse& parsed) {
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
                handleStreamDelta(line, acc, firstContent, thinking,
                                  interrupted, hadError, errorMsg);
            });

    // 错误处理
    if (hadError) {
        if (m_output) m_output->emitError("Stream error: " + errorMsg);
        if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Error);
        return {TurnRequestResult::Action::ReturnError, errorMsg, {}};
    }
    if (interrupted || m_interrupted) {
        // W-usage：中断早退前累计已到达的 usage（中断早于服务端
        // usage 发出 → acc 各字段为 0，helper 内 gate 自然跳过）
        accumulatePartialUsage(acc);
        return interruptedResult();
    }
    if (response.m_wasAborted) {
        // httplib 层检测到中断（abort → stop → 阻塞读返回）→ 直接返回，不重试
        return interruptedResult();
    }
    if (!response.m_error.empty()) {
        // A2：错误重试收敛至 handleHttpError（原内联 ~25 行）
        return httpErrorResult(response.m_error, consecutiveErrors);
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
    // P2-4: 流式 usage（缺失保持 0——不估猜）
    parsed.m_usagePrompt     = acc.getUsagePrompt();
    parsed.m_usageCompletion = acc.getUsageCompletion();
    parsed.m_usageTotal      = acc.getUsageTotal();
    parsed.m_usageCacheHit   = acc.getUsageCacheHit();
    parsed.m_hasCacheField   = acc.getHasCacheField();
    return {TurnRequestResult::Action::Continue, {}, {}};
}

// 单个 SSE chunk 分类处理（原 runTurn 流式 lambda 内联 27 行提为函数）：
// error / usage / content+reasoning / finish_reason 四路
void CLFAgentLoop::handleStreamDelta(const std::string& line, CLFStreamAccumulator& acc,
                                     bool& firstContent,
                                     CLF::CLFNetwork::CLFThinkingIndicator& thinking,
                                     bool& interrupted, bool& hadError,
                                     std::string& errorMsg) {
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
        // P2-4: usage chunk 的 choices 为空数组，须在 choices 过滤之前单独投喂
        if (delta.contains("usage"))
            acc.feedUsage(delta["usage"]);
        if (delta.contains("choices") && !delta["choices"].empty()) {
            const auto& choice = delta["choices"][0];
            if (choice.contains("delta")) {
                std::string chunk = acc.feedDelta(choice["delta"]);
                if (!chunk.empty()) {
                    if (firstContent) { thinking.stop(); firstContent = false; }
                    if (m_output) m_output->emitContent(chunk);
                    // 事件化重构步骤 2：ContentDelta 双轨发射
                    if (m_eventBus) {
                        CLF::CLFTypes::CLFAgentEvent ev;
                        ev.m_type = CLF::CLFTypes::CLFEventType::ContentDelta;
                        ev.m_turnId = m_currentTurnId;
                        ev.m_text = chunk;
                        m_eventBus->publish(ev);
                    }
                }
                // 推理过程 → 独立通道（UI 层 Ctrl+O 折叠/展开）
                // 只追加上次检查后新增的推理内容（避免重复累积）
                if (acc.hasReasoning() && m_output) {
                    const auto& reasoning = acc.getReasoning();
                    if (reasoning.size() > m_lastReasoningSize) {
                        m_output->appendThinking(
                            reasoning.substr(m_lastReasoningSize));
                        // 事件化重构步骤 2：ReasoningDelta 双轨发射
                        if (m_eventBus) {
                            CLF::CLFTypes::CLFAgentEvent ev;
                            ev.m_type = CLF::CLFTypes::CLFEventType::ReasoningDelta;
                            ev.m_turnId = m_currentTurnId;
                            ev.m_text = reasoning.substr(m_lastReasoningSize);
                            m_eventBus->publish(ev);
                        }
                        m_lastReasoningSize = reasoning.size();
                    }
                }
            }
            if (choice.contains("finish_reason")) acc.feedDelta(choice);
        }
    } catch (const nlohmann::json::exception&) {}
}

// 同步请求：postJson → 中断/wasAborted/错误早退 → 成功解析回填
CLFAgentLoop::TurnRequestResult CLFAgentLoop::syncRequest(const std::string& body, int& consecutiveErrors,
                                                          CLFAssistantResponse& parsed) {
    CLF::CLFNetwork::CLFThinkingIndicator thinking(m_httpClient.get(), m_output);
    CLF::CLFNetwork::CLFHttpResponse response =
        m_httpClient->postJson("/v1/chat/completions", body);
    thinking.stop();

    if (m_interrupted || response.m_wasAborted) {
        // W-usage：响应已完整返回——从 body 提取 usage 累计
        // （连接失败等 wasAborted 场景 body 空 → parse 默认 0，no-op）
        accumulateUsage(
            m_protocolAdapter.parseAssistantResponse(response.m_body));
        return interruptedResult();
    }
    if (!response.m_error.empty()) {
        // A2：错误重试收敛至 handleHttpError（与流式路径同款）
        return httpErrorResult(response.m_error, consecutiveErrors);
    }

    consecutiveErrors = 0;
    parsed = m_protocolAdapter.parseAssistantResponse(response.m_body);
    return {TurnRequestResult::Action::Continue, {}, {}};
}

// 流式/同步两路径后处理收敛（原两处同构跑两遍 → 单点）：
// 自发结束标记 → usage 累计 → finish_reason 检查
CLFAgentLoop::TurnRequestResult CLFAgentLoop::finalizeResponse(CLFAssistantResponse& parsed,
                                                               bool& modelDeclaredEnd) {
    // 模型自发结束标记：截断其后内容 + 丢弃 tool_calls
    applySelfWorkedMark(parsed, modelDeclaredEnd);
    // P2-4/R3: 只累计已落定的 usage（正常解析路径；W-usage 2026-09-23：
    // 中断早退点已共用同一 helper——"发生了的请求就是发生了"）。
    // 缓存命中率显示：会话累计（底部常亮参数行；与 m_totalTokensUsed
    // 同生命周期不重置）。仅响应携带缓存字段才累计——第三方 provider
    // 无此字段时不显示（"无法统计"），区别于 DeepSeek 真实零命中（显示 0%）
    accumulateUsage(parsed);
    // finish_reason 检查
    if (!CLFProtocolAdapter::isValidFinish(parsed)) {
        if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Error);
        return {TurnRequestResult::Action::ReturnError,
                "Unexpected finish_reason: '" + parsed.m_finishReason + "'", {}};
    }
    return {TurnRequestResult::Action::Continue, {}, parsed};
}

// W-usage：流式中断早退点——partial usage 提取 + 累计（原 runTurn :281-287 内联搬移）
void CLFAgentLoop::accumulatePartialUsage(const CLFStreamAccumulator& acc) {
    CLFAssistantResponse partial;
    partial.m_usagePrompt     = acc.getUsagePrompt();
    partial.m_usageCompletion = acc.getUsageCompletion();
    partial.m_usageTotal      = acc.getUsageTotal();
    partial.m_usageCacheHit   = acc.getUsageCacheHit();
    partial.m_hasCacheField   = acc.getHasCacheField();
    accumulateUsage(partial);
}

// 编排层中断早退单点（原散落 6+ 处的同构两行收敛）：
// emitInterrupted（统一文案 + 清推理 + Warn 状态点）+ 中断文案
std::string CLFAgentLoop::abortTurn() {
    emitInterrupted();
    return std::string("[Interrupted]");
}

// 请求层中断早退单点：emitInterrupted + ReturnInterrupted 结果
CLFAgentLoop::TurnRequestResult CLFAgentLoop::interruptedResult() {
    emitInterrupted();
    return {TurnRequestResult::Action::ReturnInterrupted, {}, {}};
}

// HttpErrorAction 三态 → 对应早退结果（流式/同步两路径错误分支收敛；
// 重试计数/可中断等待在 handleHttpError 内）
CLFAgentLoop::TurnRequestResult CLFAgentLoop::httpErrorResult(const std::string& error,
                                                              int& consecutiveErrors) {
    switch (handleHttpError(error, consecutiveErrors)) {
    case HttpErrorAction::ReturnError:
        return {TurnRequestResult::Action::ReturnError, error, {}};
    case HttpErrorAction::ReturnTooMany:
        return {TurnRequestResult::Action::ReturnError, "Too many errors: " + error, {}};
    case HttpErrorAction::ReturnInterrupted:
        return interruptedResult();
    case HttpErrorAction::Continue:
        return {TurnRequestResult::Action::Retry, {}, {}};
    }
    return {TurnRequestResult::Action::Retry, {}, {}};  // 不可达（枚举全覆盖，防御 C4715）
}

// 触顶 wrap-up 请求（stream=false 变体）+ 失败降级。
// 返回 false = 请求期间被中断（emitInterrupted 已发射，调用方直接返回文案）
bool CLFAgentLoop::requestWrapUpBody(std::string& finalContent) {
    try {
        CLF::CLFNetwork::CLFThinkingIndicator thinking(m_httpClient.get(), m_output);
        // 收尾请求走同步传输 → 请求体必须非流式：m_stream=true 时 buildChatRequest
        // 会带 "stream":true，DeepSeek 返回 SSE 文本，postJson 同步读回整串
        // "data: ..." 无法 JSON 解析（2026-09-02 实机 parse_error.101 根因——
        // 同步传输与流式请求体的协议不匹配）
        CLFAgentConfig wrapUpConfig = m_config;
        wrapUpConfig.m_stream = false;
        const std::string body = buildRequestBody(wrapUpConfig);
        CLF::CLFNetwork::CLFHttpResponse response =
            m_httpClient->postJson("/v1/chat/completions", body);
        thinking.stop();
        if (m_interrupted || response.m_wasAborted) {
            emitInterrupted();
            return false;
        }
        if (response.m_error.empty()) {
            CLFAssistantResponse wrapUp =
                m_protocolAdapter.parseAssistantResponse(response.m_body);
            // 响应的 tool_calls 忽略（收尾请求只取文本）
            if (!wrapUp.m_content.empty()) {
                if (!finalContent.empty()) finalContent += "\n";
                finalContent += wrapUp.m_content;
                m_context.addMessage("assistant", wrapUp.m_content);
                if (m_output && m_config.m_stream)
                    m_output->emitContent(wrapUp.m_content + "\n");
            }
        }
        // 失败（含 response.m_error 非空）→ 降级：收尾 user 消息保留在
        // context（连续 user 消息 API 兼容，下轮"继续"时提示语义仍有效）
    } catch (const std::exception& e) {
        CLFLogger::instance().warn("[Turn] wrap-up request failed: "
            + std::string(e.what()));
    }
    return true;
}

// 模型自发回合结束宣告防御（原 runTurn 内 lambda 提为成员，签名/体原样搬移）：
// "✻ <worked> for" 是 CLFCode 专属 UI 格式——模型回复中出现即视为宣告
// 回合结束。尊重宣告：保留标记行、截断其后内容（防"模拟用户追问"
// 自问自答）、丢弃已声明的 tool_calls（宣告结束 → 不执行）；收尾不
// 重复拼 worked（模型已宣告）。模型正常回复不可能含该字面——出现即
// 思考异常，止损正确
void CLFAgentLoop::applySelfWorkedMark(CLFAssistantResponse& parsed,
                                       bool& modelDeclaredEnd) const {
    const std::string mark = "\n✻ " + m_labels.worked + " for";
    size_t pos = parsed.m_content.find(mark);
    if (pos == std::string::npos
        && parsed.m_content.rfind("✻ " + m_labels.worked + " for", 0) == 0) {
        pos = 0;   // 内容首行即标记
    }
    if (pos == std::string::npos) return;
    modelDeclaredEnd = true;
    const size_t lineEnd = parsed.m_content.find('\n', pos + 1);
    parsed.m_content.erase(lineEnd == std::string::npos
                               ? parsed.m_content.size() : lineEnd);
    parsed.m_toolCalls.clear();
    CLFLogger::instance().warn(
        "[Turn] model declared end (self worked mark) — truncated trailing content");
}

// W-usage（2026-09-23 中断时效性 §13.5 用户拍板）：中断路径也累计 usage——
// "发生了的请求就是发生了"。helper 供中断早退点与正常累计点共用；
// 中断发生在服务端发出 usage 之前 → 各字段为 0，gate 自然跳过
// （不估算——与既有"缺失保持 0"口径一致）
void CLFAgentLoop::accumulateUsage(const CLFAssistantResponse& usageSrc) {
    if (usageSrc.m_usageTotal > 0) {
        m_totalTokensUsed += usageSrc.m_usageTotal;
        m_lastToolStats.totalTokens = static_cast<int>(m_totalTokensUsed);
        m_lastToolStats.turnTokens = static_cast<int>(
            m_totalTokensUsed - m_turnStartTokens);
        if (usageSrc.m_hasCacheField)
            m_sessionUsage.accumulate(usageSrc.m_usagePrompt, usageSrc.m_usageCacheHit);
    }
}

// ============================================================================
// A5 收尾 helper（设计-工具调用循环上限机制改造 §3.2.1）
// 分册 2 块 1：appendWorked 删除——✻ worked 行迁适配器（TurnFinished 渲染）
// ============================================================================

std::string CLFAgentLoop::finishTurn(
    std::string& finalContent,
    std::chrono::steady_clock::time_point turnStart,
    CLF::CLFTypes::ICLFOutput::StatusKind kind,
    bool modelDeclaredEnd) {
    // T6: todo 全完成收尾（设计-任务清单UI显示 §3.7，2026-09-02）
    // 判定（C2：收敛至 CLFTodoStore::allDoneSnapshot）：非空 && 全部 completed
    // && !todoPanelDone（无 m_todoDirty，§八 补丁 5）
    // 分册 2 块 1：② 显示（📋 收尾行 emit）迁适配器（TurnFinished 渲染）；
    // ① 数据（complete 行落盘）与 ③ 置位保留 core
    if (!m_todoStore.isTodoPanelDone()) {
        const auto todosSnapshot = m_todoStore.allDoneSnapshot();
        if (!todosSnapshot.empty()) {
            // ① 数据：complete 行先落盘（无活动文件跳过；append 内部自兜底不抛）
            const std::string sessionFile = getActiveSessionFile();
            if (!sessionFile.empty()) {
                const std::string line = CLFMessageCodec::serializeCompleteLine(
                    todosSnapshot, CLFSessionManager::timestampNow());
                CLFSessionManager::appendComplete(sessionFile, line);
            }
            // ③ 显示：置位（面板隐藏，等待下一次 create）
            m_todoStore.setTodoPanelDone(true);
        }
    }

    // 分册 2 块 1：✻ worked 行不再拼 finalContent/emit——适配器按
    // TurnFinished 渲染（m_modelDeclaredEnd 时抑制——自发宣告语义保持）
    m_context.addMessage("assistant", finalContent);
    // P1-1: 完成点显式接线（TurnGuard 不设 kind，F20）；kind 由调用方按
    // 结束原因传入：自然停/concluded = Done，max-tokens = Warn（A5 §3.2.1）
    if (m_output) m_output->setStatusKind(kind);
    // 事件化重构步骤 2：TurnFinished 双轨发射（自然停/concluded 路径）
    // 分册 2：m_text = finalContent（落库数据源——writer 据此重建 assistant 消息）
    if (m_eventBus) {
        CLF::CLFTypes::CLFAgentEvent ev;
        ev.m_type = CLF::CLFTypes::CLFEventType::TurnFinished;
        ev.m_turnId = m_currentTurnId;
        ev.m_text = finalContent;
        ev.m_workedSec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - turnStart).count();
        ev.m_todos = m_todoStore.getTodos();
        ev.m_capReached = false;
        ev.m_modelDeclaredEnd = modelDeclaredEnd;  // 自发宣告 → 适配器不拼 ✻
        m_eventBus->publish(ev);
    }
    CLFLogger::instance().info("[Turn] done, content="
        + std::to_string(finalContent.size()) + "chars, tools="
        + std::to_string(m_lastToolStats.totalCalls));
    return m_config.m_stream ? std::string() : finalContent;
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

// ============================================================================
// jsonl 会话文件上下文（设计-会话追加式保存.jsonl §3.9，2026-09-02）
// ============================================================================

void CLFAgentLoop::setActiveSessionFile(const std::string& jsonlPath) {
    // C2：转发 CLFSessionFileCtx（锁随对象）
    m_sessionFileCtx.setActiveSessionFile(jsonlPath);
}

std::string CLFAgentLoop::getActiveSessionFile() const {
    return m_sessionFileCtx.getActiveSessionFile();
}

std::string CLFAgentLoop::beginTurnSession(const std::string& firstInput) {
    // B2（R1 裁决）：原 CLFRepl::submit 轮初两步条件判定收编——
    // UI 不再持有会话策略判定（P0-4 完整关闭）
    if (m_sessionFileCtx.getResumedFrom().empty()) {
        setTodoPanelDone(true);   // J3/J4: 新回合清面板（resume 续写不清空）
    }
    if (getActiveSessionFile().empty()) {
        return beginSessionFile(firstInput);  // J4: 懒创建
    }
    return getActiveSessionFile();
}

void CLFAgentLoop::closeSessionAndReset() {
    // B2：原 cmdClear 五原语序列收编（/clear 语义）
    closeSessionFileWithSummary();  // 生成摘要 → summary 行 → 关闭（文件保留为独立会话）
    setResumedFrom("");             // 新会话语义干净（§6.4-C）
    setTodos({});
    setTodoPanelDone(true);
    clearContext();
}

std::string CLFAgentLoop::beginSessionFile(const std::string& firstInput) {
    // C2：转发 CLFSessionFileCtx（header 序列化所需的 model/skills 由编排层传入）
    return m_sessionFileCtx.beginSessionFile(
        firstInput, m_config.m_modelName, m_loadedSkills);
}

void CLFAgentLoop::appendTodoSnapshotNow() {
    // 事件化重构步骤 2：TodoUpdated 双轨发射（清单变化点——快照落盘处；
    // 归属表 §2.3：todo 快照写入点 → TodoUpdated{todos}）
    if (m_eventBus) {
        CLF::CLFTypes::CLFAgentEvent ev;
        ev.m_type = CLF::CLFTypes::CLFEventType::TodoUpdated;
        ev.m_turnId = m_currentTurnId;
        ev.m_todos = getTodos();
        m_eventBus->publish(ev);
    }
    const std::string path = getActiveSessionFile();
    if (path.empty()) {
        CLFLogger::instance().debug("[TodoSnapshot] skipped: no active session file");
        return;
    }
    const std::string line = CLFMessageCodec::serializeTodoSnapshot(
        getTodos(), CLFSessionManager::timestampNow());
    if (!CLFSessionManager::appendTodoSnapshot(path, line)) {
        CLFLogger::instance().warn("[TodoSnapshot] append failed: " + path);
    }
}

std::string CLFAgentLoop::appendTurnLine() {
    // 轮末 todos 快照：仅本轮操作过 create/update/clear 才带（脏标记读取并清除）
    std::vector<CLFTodoItem> todosSnapshot;
    const std::vector<CLFTodoItem>* todosPtr = nullptr;
    if (m_todoStore.exchangeDirty()) {
        todosSnapshot = m_todoStore.getTodos();
        todosPtr = &todosSnapshot;
    }
    // C2：差集/序列化/追加在 CLFSessionFileCtx
    return m_sessionFileCtx.appendTurn(m_context.getMessages(), todosPtr);
}

void CLFAgentLoop::closeSessionFileWithSummary() {
    // J5: 生成摘要（开关关时 m_valid=false，跳过行写入）→ 追加 summary 行 → 关闭
    generateAndCacheSummary();
    if (getActiveSessionFile().empty()) return;
    appendSummaryLineNow();
    setActiveSessionFile("");   // 文件保留为独立会话（不删不改名）
}

// ============================================================================
// S3-1 摘要：自动触发 + 工具触发（2026-09-02）
// ============================================================================

void CLFAgentLoop::appendSummaryLineNow() {
    // C2：落盘序列化/追加在 CLFSessionFileCtx（空文件/无效摘要内部跳过）
    m_sessionFileCtx.appendSummaryLine(m_summaryCache.cached());
}

bool CLFAgentLoop::shouldSummarize() const {
    // C2：触发判定在 CLFSummaryCache（开关 + 剩余窗口阈值）
    return m_summaryCache.shouldSummarize(m_context.estimateTokens());
}

std::string CLFAgentLoop::compressContextNow() {
    if (!m_config.m_contextCompression) {
        return "上下文压缩已关闭（配置 agent.context_compression = true 开启）";
    }
    generateAndCacheSummary();
    appendSummaryLineNow();
    rebuildSystemMessage();
    const auto& cached = m_summaryCache.cached();
    if (cached.m_valid) {
        CLFLogger::instance().info("[Compress] summary generated, method="
            + cached.m_method);
        return "上下文已压缩为摘要并注入系统提示（" + cached.m_method + "）:\n"
            + cached.m_summary;
    }
    return "摘要生成失败（API 与规则降级均不可用）";
}

void CLFAgentLoop::setModelName(const std::string& name) {
    // S3-2: 运行时切换主模型（不落盘；header 的 model 字段新会话自动准确）
    m_config.m_modelName = name;
    // 按模型名查表覆盖 max_tokens（用户显式声明于配置文件，未命中保持全局值）
    const auto it = m_config.m_modelMaxTokens.find(name);
    if (it != m_config.m_modelMaxTokens.end()) {
        m_config.m_maxTokens = it->second;
        CLFLogger::instance().info("[Model] max_tokens overridden by table: "
            + std::to_string(it->second));
    }
    CLFLogger::instance().info("[Model] switched to: " + name);
}

void CLFAgentLoop::generateAndCacheSummary() {
    // C2：生成+缓存+开关判定在 CLFSummaryCache。
    // C2b：摘要输入 = 窗口内消息（与旧 getMessages 截断语义保真）
    m_summaryCache.generate(m_window.apply(m_context.getMessages()));
}

bool CLFAgentLoop::restoreSession(const std::string& filePath) {
    CLFLogger::instance().info("[Restore] loading: " + filePath);

    std::vector<CLFMessage> messages;
    std::vector<std::string> skills;
    CLFSessionSummary summary;
    std::vector<CLFTodoItem> todos;         // S2-6: 旧会话文件无 todos 字段 → 空清单
    std::vector<CLFTodoItem> completeTodos; // jsonl complete 行（loadJsonl 解析）
    CLFSessionInfo headerInfo;

    // J3: 按扩展名分流——.jsonl 走追加式解析，.json 走覆盖式（旧归档永久兼容）
    const bool isJsonl = filePath.size() >= 6
                      && filePath.compare(filePath.size() - 6, 6, ".jsonl") == 0;
    bool ok;
    if (isJsonl) {
        ok = CLFSessionManager::loadJsonl(filePath, messages, &skills, &summary,
                                          &todos, &completeTodos, &headerInfo);
    } else {
        ok = CLFSessionManager::load(filePath, messages, &skills, &summary, &todos);
    }
    if (!ok) return false;
    setTodos(std::move(todos));   // T2: 走锁内替换（§3.9）

    // J3: resume 续写态（§八 补丁 4）——恢复即进入续写。
    // 2026-09-23 用户拍板（实机回响 D8 重拍）：面板**无条件隐藏**——中断时
    // 快照是陈旧状态（模型 resume 后不逐项核对更新，直接继续做后面的任务，
    // 面板显示"任务 1 进行中"而实际在做任务 N 是误导）；模型第一次调
    // todo_write 时以新数据重现（create/update 置面板显示——现成机制）
    m_sessionFileCtx.setResumedFrom(filePath);
    setTodoPanelDone(true);
    (void)completeTodos;   // 分册 2 块 3：回显走事件重放（Replayer）——此输出保留 API 完整性

    CLFLogger::instance().info("[Restore] loaded: "
                               + std::to_string(messages.size()) + " msgs, "
                               + std::to_string(skills.size()) + " skills, "
                               + "summary=" + std::string(summary.m_valid
                                   ? "yes(" + summary.m_method + ")" : "no"));

    m_context.clear();

    // ② 恢复非 system 消息（全部跳过 system，由后续步骤重建）
    // （分册 2 块 3：① 结构化回显行收集删除——回显走事件重放）
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

// A2：流式/同步错误重试收敛（原两处内联 ~25 行×2，逻辑逐行搬移——
// 致命/超限/中断三态返回指令，调用方 switch 分发；等待段保持可中断）
CLFAgentLoop::HttpErrorAction CLFAgentLoop::handleHttpError(
    const std::string& error, int& consecutiveErrors) {
    if (CLFRetryPolicy::isFatalHttpError(error)) {
        if (m_output) m_output->emitError(error);
        if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Error);
        return HttpErrorAction::ReturnError;
    }
    // 按错误类别定重试上限：其他 4xx = 2（重试 1 次即止）/ 429·5xx·网络 = 3
    const int maxAttempts = CLFRetryPolicy::maxAttemptsForError(error);
    if (++consecutiveErrors >= maxAttempts) {
        if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Error);
        return HttpErrorAction::ReturnTooMany;
    }
    if (m_output) m_output->emitContent(
        "\n⚠ " + error + " — retry "
        + std::to_string(consecutiveErrors) + "/"
        + std::to_string(maxAttempts) + "\n");
    // ③ 可中断等待 (每 100ms 检查一次)
    for (int w = 0; w < 20 * consecutiveErrors && !m_interrupted; ++w)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (m_interrupted) return HttpErrorAction::ReturnInterrupted;
    return HttpErrorAction::Continue;
}

void CLFAgentLoop::emitInterrupted() {
    // P0-5: 统一文案（原 9 处两版文案）——分册 2 块 1：⏹ 行/clearThinking/
    // Warn 状态点迁适配器（Interrupted 事件渲染）；core 只留日志 + 事件。
    // 所有调用点后立即 return——一轮内不可重复发射（F17 裁决）
    // W2 修复（2026-09-23 中断时效性 批C-3）：中断路径零日志痕迹——
    // 补日志（本次破案曾靠 ToolExec 日志间接判别，中断自身不可判读）
    CLFLogger::instance().info("[Turn] interrupted");
    // 事件化重构步骤 2：Interrupted 双轨发射（全部中断早退点单点共用）
    if (m_eventBus) {
        CLF::CLFTypes::CLFAgentEvent ev;
        ev.m_type = CLF::CLFTypes::CLFEventType::Interrupted;
        ev.m_turnId = m_currentTurnId;
        m_eventBus->publish(ev);
    }
}

void CLFAgentLoop::injectSystemPrompt() {
    auto ctx = buildSystemPromptContext();
    m_context.setSystemPrompt(m_systemPromptBuilder.build(ctx));
}

CLFSystemPromptBuilder::Context CLFAgentLoop::buildSystemPromptContext() const {
    CLFSystemPromptBuilder::Context ctx;
    // u8string：string() 按 ANSI 代码页转窄字符，中文路径乱码（v0.4.2 修复）
    ctx.workspaceRoot       = std::filesystem::current_path().u8string();
    ctx.interactionLanguage = m_config.m_interactionLanguage;
    ctx.modelName           = m_config.m_modelName;
    ctx.maxContextWindow    = m_config.m_maxContextWindow;
    // S3-1: 会话摘要经 Builder 段落注入（system 永不截断；压缩触发后 rebuild 生效）
    ctx.sessionSummary      = m_summaryCache.cached().m_valid ? m_summaryCache.cached().m_summary
                                                              : std::string();
    for (const auto& name : m_loadedSkills) {
        std::string content = CLFSkillLoader::getContent(name);
        if (!content.empty()) ctx.skills.emplace_back(name, content);
    }
    return ctx;
}

void CLFAgentLoop::rebuildSystemMessage() {
    auto ctx = buildSystemPromptContext();
    m_context.setSystemPrompt(m_systemPromptBuilder.build(ctx));
}

} // namespace CLF::CLFCore
