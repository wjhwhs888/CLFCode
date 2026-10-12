# 设计-事件化重构-1-AgentLoop拆分与事件总线（总纲步骤 0-2）

> **生命周期状态**：① 规划中 → ② 评审 → ③ 实施 → ④ 验证 → ⑤ 归档
> **创建**：2026-10-12（pro 执笔）
> **定位**：第一批核心分册——runTurn 函数级拆分（步骤 0）+ 事件协议头字段级定义（步骤 1）+ 事件总线机制与 TUI 适配器（步骤 2）。文案迁移/落库恢复在分册 2，并发调度在分册 3
> **硬约束**：步骤 0-2 全程 **ICLFOutput / CLFTerminal / CLFRepl 零改动**，TUI 观感逐字对齐（输出快照对比为验收）

---

## 一、现状结构与拆分方案（步骤 0）

### 1.1 现状取证

`CLFAgentLoop::runTurn`（CLFAgentLoop.cpp:79-521，约 440 行）结构分解：

| 行段 | 内容 |
|---|---|
| :79-104 | 中断检查 emitInterrupted / 摘要 cooldown + appendSummaryLineNow + rebuildSystemMessage |
| :111-118 | setTurnStartMsgCount + addMessage("user") + m_lastToolStats 重置 + m_turnStartTokens 快照 |
| :124-140 | modelDeclaredEnd 标志 + **applySelfWorkedMark lambda**（自发结束标记防御） |
| :141-154 | **accumulateUsage lambda**（W-usage 累计） |
| :156 | setStatusKind(Running) |
| :158-188 | turnTimer 50ms（状态行计时 + 转圈动画）+ TurnGuard（析构停表 + setStatus("")） |
| :190-194 | finalContent / consecutiveErrors / concluded 声明 |
| :196 | 主循环 `for (iteration < m_maxToolCallIterations)` |
| :200-203 | thinkingSec + thinkingTimer 1s（每迭代） |
| :206-209 | 迭代前中断检查 |
| :210-213 | buildChatRequest（window.apply 截断） |
| :217-270 | 流式路径（postJsonStream + 回调：chunk emitContent / reasoning appendThinking / usage feed / hadError） |
| :272-295 | hadError / interrupted / wasAborted 早退处理 |
| :296-310 | handleHttpError 重试（HttpErrorAction 三态） |
| :312-328 | 流式 parsed 赋值 + applySelfWorkedMark |
| :330-371 | 同步路径（postJson + 中断/wasAborted/错误重试 + parse + applySelfWorkedMark） |
| :373-378 | accumulateUsage(parsed) |
| :380-385 | finish_reason 检查（isValidFinish） |
| :387-395 | finalContent 累积（多段文本 + length 截断提示） |
| :397-428 | hasToolCalls 分支：concluded-break / 中断 / addAssistantToolCalls / executor.execute / addToolResult / continue |
| :430-440 | 完成分支：finishTurn(finalContent, turnStart, finishKind, modelDeclaredEnd) return |
| :442-454 | catch 异常 → 重试（consecutiveErrors 达上限返回错误） |
| :457-462 | concluded 后处理：finishTurn return |
| :464-520 | 触顶路径：中断检查 / kTurnCapWrapUpPrompt + wrap-up 同步请求 / kTurnCapNotice / appendWorked / Warn / return |

### 1.2 拆分方案（5 个 Section 函数）

**runTurn 瘦身后骨架**（编排层，约 80 行）：

```cpp
std::string CLFAgentLoop::runTurn(const std::string& userInput) {
    if (!prepareTurn(userInput)) return "[Interrupted]";   // ① 回合准备（含首中断检查）

    TurnTimerGuard timer(m_output, m_labels);              // ② RAII：50ms 计时器 + 状态行
    std::string finalContent;
    int consecutiveErrors = 0;
    bool concluded = false;
    bool modelDeclaredEnd = false;

    for (int iteration = 0; iteration < m_config.m_maxToolCallIterations; ++iteration) {
        TurnThinkingClock thinking;                        // ③ RAII：thinkingSec 1s 计数
        if (m_interrupted) { emitInterrupted(); return "[Interrupted]"; }

        // ④ 单次请求（流式/同步收敛 + 重试 + 防御 + usage）
        TurnRequestResult req = requestAssistantTurn(iteration, consecutiveErrors,
                                                     modelDeclaredEnd);
        if (req.m_action == TurnRequestResult::Action::ReturnError)
            return "[Error] " + req.m_errorText;
        if (req.m_action == TurnRequestResult::Action::ReturnInterrupted)
            return "[Interrupted]";

        // ⑤ 文本累积
        if (!req.m_response.m_content.empty()) {
            if (!finalContent.empty()) finalContent += "\n";
            finalContent += req.m_response.m_content;
        }
        if (finalContent.empty() && req.m_response.m_finishReason == "length")
            finalContent = "(响应被截断 — 思考时间过长，token 已达上限)";

        // ⑥ 工具执行段
        if (CLFProtocolAdapter::hasToolCalls(req.m_response)) {
            if (concluded) break;                          // A5：声明结束仍发工具 → 丢弃
            if (m_interrupted) { emitInterrupted(); return "[Interrupted]"; }
            ToolExecutionOutcome oc =
                executeToolCalls(req.m_response.m_toolCalls, thinking);   // ⑦
            if (oc.m_interrupted) return "[Interrupted]";
            if (oc.m_concluded) concluded = true;
            continue;
        }

        // ⑧ 完成收尾（自然停）
        return finishTurn(finalContent, timer.turnStart(), finishKindOf(req.m_response),
                          modelDeclaredEnd);
    }

    if (concluded)                                          // A5：concluded-break 收尾
        return finishTurn(finalContent, timer.turnStart(),
                          CLF::CLFTypes::ICLFOutput::StatusKind::Done, modelDeclaredEnd);

    return runWrapUp(finalContent, timer.turnStart());      // ⑨ 触顶收尾
}
```

**各函数签名与职责**（均为 CLFAgentLoop 私有成员；搬移 = 逻辑原样、仅重组）：

| 函数 | 签名 | 搬移内容 | 行为约束 |
|---|---|---|---|
| `prepareTurn` | `bool prepareTurn(const std::string& userInput)` | :83-104 + :111-118 + :156（摘要/记账/状态点）；lambda 定义改为成员 `applySelfWorkedMark` / `accumulateUsage` | 返回 false = 中断早退；真中断路径 emitInterrupted 照旧 |
| `requestAssistantTurn` | `TurnRequestResult requestAssistantTurn(int iteration, int& consecutiveErrors, bool& modelDeclaredEnd)` | :210-378 全部（buildChatRequest + 流式/同步 + 错误重试 + 防御 + usage） | consecutiveErrors 引用传入传出（重试计数语义保持）；流式回调内的 emitContent/appendThinking 保持原样（分册 2 再迁移）。**防御意义（总纲决策 24③）**：拆分后全程序模型请求唯一入口 = 本函数（流式/同步/wrapUp 三处收敛单点）——"程序零自动提交"从事实变为可审计结构 |
| `executeToolCalls` | `ToolExecutionOutcome executeToolCalls(const std::vector<CLFToolCall>& toolCalls, TurnThinkingClock& thinking)` | :410-427（addAssistantToolCalls + executor + 结果回填 + concluded 判定） | executor 构造参数原样搬移（m_tools/m_securityPolicy/.../&thinkingSec） |
| `finishTurn`（已有） | 微调：签名不变，内部顺序不变 | 现有 :538-586 | 零改动（分册 2 才动文案/落库） |
| `runWrapUp` | `std::string runWrapUp(std::string& finalContent, std::chrono::steady_clock::time_point turnStart)` | :464-520（wrap-up 请求 + kTurnCapNotice + appendWorked + Warn） | 触顶语义逐字保持 |

**lambda → 成员函数**：

```cpp
// 原 runTurn 内 lambda 提为私有成员（签名不变、体原样搬移）
void applySelfWorkedMark(CLFAssistantResponse& parsed, bool& modelDeclaredEnd) const;
void accumulateUsage(const CLFAssistantResponse& usageSrc);
```

**新增 RAII 辅助**（私有嵌套，CLFAgentLoop.cpp 匿名命名空间或私有 struct）：

```cpp
// TurnTimerGuard：封装 turnTimer 50ms + TurnGuard 析构顺序（原 :158-188）
//   - 构造：启动 CLFPeriodicTimer（状态行计时 + requestRefresh）
//   - 析构：timer.stop() → setStatus("")（顺序与原 TurnGuard 一致）
//   - turnStart() 返回构造时点（finishTurn/appendWorked 计时基准）
// TurnThinkingClock：封装 thinkingSec + thinkingTimer（原 :200-203），
//   析构即 stop；提供 sec() 供 executor 注入
```

### 1.2b Section 内部二级函数化（2026-10-12 用户定调：单函数几百行是问题，C++ 要封装复用）

**粒度纪律**：每个函数 ≤ ~50 行；一个函数 = 一个操作；同一逻辑出现两次必须收敛为函数复用（不允许复制粘贴两遍）。

| 二级函数 | 职责 | 收敛的重复 |
|---|---|---|
| `buildRequestBody(iteration)` | window.apply + buildChatRequest + 日志 | 流式/同步/wrapUp 三处请求体构造（wrapUp 为变体：stream=false） |
| `streamRequest(body, consecutiveErrors, modelDeclaredEnd)` | 流式请求 + 回调分发 + 错误/中断早退 | — |
| `handleStreamDelta(delta, acc, firstContent, thinking)` | 单个 SSE chunk：content/reasoning/usage/error 分类处理（现 :243-269 内联 27 行提为函数） | — |
| `syncRequest(body, consecutiveErrors)` | 同步请求 + 中断/wasAborted/错误处理 | — |
| `finalizeResponse(parsed, modelDeclaredEnd)` | applySelfWorkedMark + accumulateUsage + isValidFinish 检查 | **流式/同步两路径后处理完全同构（现跑两遍）——收敛单点** |
| `accumulatePartialUsage(acc)` | 中断早退点的 partial usage 提取 + accumulateUsage（现 :281-287 内联） | 与 finalizeResponse 内 usage 共用 accumulateUsage |
| `abortTurn(reason)` | emitInterrupted + 返回 "[Interrupted]" 的统一早退（现散落 6+ 处同构） | **中断早退模式收敛** |
| `handleHttpErrorAction(action, response)` | HttpErrorAction 三态 → 对应早退（现 switch 跑两遍） | **流式/同步两路径错误分支收敛** |
| `requestWrapUpBody()` | 触顶 wrap-up 请求体（stream=false 变体）+ 同步请求 + 失败降级 | 与 buildRequestBody/syncRequest 复用 |

**复用关系**：requestAssistantTurn = buildRequestBody → (streamRequest ∥ syncRequest) → finalizeResponse；触顶 = requestWrapUpBody → finalizeResponse（同构复用）；所有中断点走 abortTurn。

### 1.3 步骤 0 验证

- ctest 全绿（39/39 基线）
- 实机回归：正常对话 / 流式 / 同步 / 中断 / 触顶 wrap-up / 异常重试 / 自发 Worked 防御——**输出逐字对比**（同一输入序列的屏幕快照前后一致）
- 提交点：独立 commit（refactor 纯搬移），不混入步骤 1-2 代码

---

## 二、事件协议头（步骤 1）

### 2.1 设计定案

- **位置**：`src/CLFTypes/CLFEvents.hpp`（CLFTypes 层——零依赖仅 stdlib + CLFTypes 既有 POD；CLFTodoItem 已在本层 ✓）
- **协议头即规范**（用户定调）：新增 UI（Qt/HTML）以本头为合同；第三批跨进程时补 JSON codec（同头结构序列化）
- **双通道分层**（§3.9 查证）：
  - **语义帧**（主通道）：保序不丢——TurnStarted/ToolStarted/ToolFinished/StatusChanged/TodoUpdated/TurnFinished/Interrupted/ConfirmRequested
  - **增量帧**（旁路通道）：渲染加速——ContentDelta/ReasoningDelta/ThinkingCleared（溢出可合并丢弃）
- **稳定关联键**：`turnId`（每回合唯一，uuid 式）、`callId`（tool_calls 已有 m_id）、`requestId`（confirm 请求-响应对）、`seq`（总线分配全局序号）
- **请求-响应原语**：`ConfirmRequested{requestId,prompt}`（core → UI）↔ `ConfirmResponse{requestId,accepted}`（UI → core）——core 阻塞等响应，**超时默认拒绝**（用户拍板 2026-10-12：参照 Claude Code control 协议，**60 秒**；超时 = 拒绝，挂机场景不无限卡任务）

### 2.2 字段级定义

```cpp
// src/CLFTypes/CLFEvents.hpp
namespace CLF::CLFTypes {

// 事件类型（core → UI 输出 + UI → core 输入 + 请求-响应）
enum class CLFEventType : uint8_t {
    // —— 语义帧（主通道，保序不丢）——
    TurnStarted,      // 回合开始（payload: input）
    ToolStarted,      // 工具启动（payload: callId/name/argsPreview）
    ToolFinished,     // 工具完成（payload: callId/name/ok/summary/errorKind）——完成可乱序
    StatusChanged,    // 状态点（payload: statusKind 数值 = ICLFOutput::StatusKind）
    TodoUpdated,      // 清单变化（payload: todos 全量快照）
    TurnFinished,     // 回合结束（payload: workedSec/todos/turnEndKind）
    Interrupted,      // 回合被中断
    // —— 增量帧（旁路通道，可合并丢弃）——
    ContentDelta,     // 主内容增量（纯文本无装饰）
    ReasoningDelta,   // 推理增量
    ThinkingCleared,  // 清空当前回合推理
    // —— 请求-响应 ——
    ConfirmRequested, // core → UI：请求确认（payload: requestId/prompt）
    // —— UI → core ——
    UserInput,        // 用户输入（payload: text）
    ConfirmResponse,  // 确认应答（payload: requestId/accepted）
    InterruptRequest, // 中断请求（无 payload）
};

// 载荷：单 struct + 按类型使用字段（tagged union 手工版——JSON codec 直写，
// 字段未用置默认；比 std::variant 序列化友好，设计评审可再议）
struct CLFAgentEvent {
    CLFEventType m_type{CLFEventType::TurnStarted};
    uint64_t     m_seq{0};          // 总线分配全局序号（单生产者保序）
    std::string  m_turnId;          // 回合稳定 id（TurnStarted 起所有同回合事件共用）
    // 内容/输入
    std::string  m_text;            // ContentDelta / ReasoningDelta / UserInput / TurnStarted.input
    // 工具
    std::string  m_callId;          // ToolStarted / ToolFinished
    std::string  m_name;            // ToolStarted / ToolFinished
    std::string  m_argsPreview;     // ToolStarted（extractKeyParam 结果）
    bool         m_ok{true};        // ToolFinished
    std::string  m_summary;         // ToolFinished（summary 行文本，UI 自行呈现）
    std::string  m_errorKind;       // ToolFinished（interrupted/timeout/...）
    // 状态/回合
    int          m_statusKind{0};   // StatusChanged（数值 = StatusKind 枚举序）
    long long    m_workedSec{0};    // TurnFinished
    std::vector<CLFTodoItem> m_todos;   // TodoUpdated / TurnFinished 快照
    // 确认
    std::string  m_requestId;       // ConfirmRequested / ConfirmResponse
    std::string  m_prompt;          // ConfirmRequested
    bool         m_accepted{false}; // ConfirmResponse
};

} // namespace CLF::CLFTypes
```

### 2.2b 协议 v1 事件全集（2026-10-12 讨论定稿，唯一权威总表）

**21 个事件**，四类：

**① Live 输出（core → UI，语义帧主通道）**：

| 事件 | 载荷 | 触发点 |
|------|------|--------|
| `TurnStarted` | turnId、text(输入) | prepareTurn |
| `ToolStarted` | callId、name、argsPreview | 调度器 dispatch 前 |
| `ToolFinished` | callId、name、ok、summary、errorKind | 提交游标（完成可乱序） |
| `StatusChanged` | statusKind(枚举数值) | 状态点各接线处 |
| `TodoUpdated` | todos(全量快照) | 清单任何变化 |
| `TurnFinished` | turnId、workedSec、todos、capReached(触顶标志) | finishTurn/runWrapUp |
| `Interrupted` | — | 中断早退点 |

**② Live 输出（增量旁路通道，可合并丢弃）**：

| 事件 | 载荷 | 触发点 |
|------|------|--------|
| `ContentDelta` | text | 流式 chunk |
| `ReasoningDelta` | text | 推理增量 |
| `ThinkingCleared` | — | 新回合/中断清推理 |

**③ 请求-响应 + 输入（双向）**：

| 事件 | 方向 | 载荷 |
|------|------|------|
| `ConfirmRequested` | core → UI | requestId、prompt |
| `ConfirmResponse` | UI → core | requestId、accepted（60s 超时默认拒绝） |
| `UserInput` | UI → core | text |
| `InterruptRequest` | UI → core | — |
| `ResumeRequested` | UI → core | sessionFile |

**④ History 重放（core → UI，分册 2 定案）**：

| 事件 | 载荷 | 渲染 |
|------|------|------|
| `HistoryHeader` | model、startedAt、skills | 会话头弱化行 |
| `HistoryUser` | text | "● 用户" 弱化行 |
| `HistoryAssistant` | text | 内容（折叠块内） |
| `HistoryToolTrace` | callId、name、ok、summary | `↳ 工具名 摘要` |
| `HistorySummary` | text | 📦 摘要折叠行 |
| `ResumeDone` | — | 回显结束 |

**格式定案**：单 struct + 按类型使用字段（tagged union 手工版——与项目 POD 惯例一致，JSON codec 直写；每字段注释钉死使用事件）。稳定关联键：`seq`（总线分配全局序号）/ `turnId`（回合）/ `callId`（工具配对）/ `requestId`（确认对）。

### 2.3 事件归属决策表（哪些现逻辑对应哪些事件）

| 现状代码 | 事件 |
|---|---|
| runTurn :156 setStatusKind(Running) | `StatusChanged{Running}` |
| 流式回调 :254 emitContent(chunk) | `ContentDelta{chunk}` |
| 流式回调 :261 appendThinking | `ReasoningDelta` |
| :83 clearThinking | `ThinkingCleared` |
| executor 内 header/进度（ToolExecutor） | `ToolStarted` / `ToolFinished` |
| todo 快照写入点（markTodosDirty + appendTodoSnapshotNow） | `TodoUpdated{todos}` |
| finishTurn :575 appendWorked + :562 清单行 + :581 setStatusKind | `TurnFinished{workedSec,todos,kind}`（分册 2 才移除 core 侧拼行） |
| emitInterrupted :865 | `Interrupted` |
| confirm 回调（ToolExecutor→AgentLoop→Terminal） | `ConfirmRequested` / 阻塞等 `ConfirmResponse` |
| CLFRepl 用户输入 → runTurn | `UserInput` |
| onInterrupt 回调 | `InterruptRequest` |

---

## 三、事件总线机制（步骤 2）

### 3.1 CLFEventBus 设计（CLFTypes 层新增 `CLFEventBus.hpp/.cpp`）

```cpp
namespace CLF::CLFTypes {

// 通道策略（订阅时声明，逐订阅生效）
enum class ChannelPolicy {
    Lossless,   // 语义帧：有界队列满 → 溢出策略 = 等待/阻塞生产者？【否——见 3.2】
    Coalescing, // 增量帧：满时合并/丢弃旧增量（保留最新）
};

class CLFEventBus {
public:
    using Handler = std::function<void(const CLFAgentEvent&)>;

    // 订阅：返回句柄；handler 在订阅者线程被调（见 3.3）；队列容量构造可配
    // （默认：Lossless 1024 / Coalescing 256）
    CLFSubscription subscribe(Handler handler, ChannelPolicy policy,
                              size_t capacity = 0);

    // 发射（单生产者 = AgentLoop 工作线程）：seq 总线分配；按订阅拷贝入队
    void publish(CLFAgentEvent ev);

    // 请求-响应原语（confirm 专用）：publish ConfirmRequested 后阻塞等
    // ConfirmResponse（requestId 匹配）；超时返回 false（默认拒绝）
    bool publishRequestAndWait(CLFAgentEvent request, std::chrono::milliseconds timeout);

    // 启动/停止分发线程（订阅者消费循环）
    void start();
    void stop();   // 停止后未消费事件：Lossless 保留缓冲可 drain（无人订阅缓冲不丢弃语义）
private:
    struct SubscriptionState {
        Handler handler;
        ChannelPolicy policy;
        // 每订阅者队列 + cv + 线程（见 3.3）
    };
    std::atomic<uint64_t> m_nextSeq{1};
    std::vector<std::unique_ptr<SubscriptionState>> m_subs;  // 锁保护
    std::mutex m_mutex;
    // 请求-响应等待表：requestId → promise（cv/超时）
};

} // namespace
```

### 3.2 溢出与背压策略（定案）

- **语义帧（Lossless）**：队列满时**丢弃该订阅的最新事件？不可**——语义帧保序不丢是硬约束。策略：队列满 → **阻塞生产者**（publish 内 cv 等待订阅者消费）——但慢消费者会拖慢内核，与"慢消费者不阻塞内核"矛盾。
  **定案（两层）**：① 队列容量默认充足（1024，单回合事件量级 < 100）——实际不会满；② 真满时**阻塞生产者**（语义帧不允许丢，宁慢不错）——与 Claude Code pull 反压同语义；③ 语义帧消费者设计纪律：handler 内只做快操作（入 UI 队列/落库追加写），重活由订阅者自己的队列/线程再转一道
- **增量帧（Coalescing）**：满时**丢弃队内最旧增量、保留最新**（渲染只需要最新画面；ContentDelta 合并 = 累积文本入队一条）
- **无人订阅缓冲**（用户定调：不丢弃）：**审查裁剪（2026-10-12）**——Writer 常驻装配（main 必装配）后"无人订阅"场景几乎不存在；定案：默认记录队列降为**有界事件历史环**（最小实现，容量 4096，满时丢弃最旧——仅 qa 断言与未来跨进程重连用；跨进程重连完整缓冲在分册 4 激活时再做）

### 3.3 线程模型（定案）

- **生产者**：AgentLoop 工作线程（单生产者 → 全局 seq 保序天然成立）
- **每订阅者一线程**：订阅时创建消费线程，drain 自己的队列 → 调 handler（try/catch 隔离——订阅者异常不拖死总线/生产者）
- **请求-响应**：publishRequestAndWait 在**生产者线程**阻塞等（cv + timeout）——confirm 语义 = 阻塞等事件（用户定调 4）
- **TUI 线程桥**：CLFTerminalEventAdapter 的 handler 把事件转投 FTXUI 事件循环（PostEvent——沿用 CLFTerminal::requestRefresh 既有机制）——**所有 ICLFOutput 调用保持在 UI 线程**，避免跨线程调 FTXUI

### 3.4 CLFTerminalEventAdapter（CLFTUI 侧新增 `CLFTerminalEventAdapter.hpp/.cpp`）

**定位**：总线订阅者 → ICLFOutput 映射（TUI 零破坏的关键）。映射表：

| 事件 | 适配器动作（ICLFOutput） |
|---|---|
| StatusChanged | `setStatusKind(StatusKind(m_statusKind))` |
| ContentDelta | `emitContent(m_text)`（保持现有流式观感） |
| ReasoningDelta | `appendThinking(m_text)` |
| ThinkingCleared | `clearThinking()` |
| ToolStarted | `setStatus(name + "…")` / showProgress 单行（沿用现有 executor 观感——初期可先只映射 ToolFinished，ToolStarted 留待分册 3 列表化） |
| ToolFinished | `finishProgress(summary)` + 详情行（分册 1 最小映射：保持与现 emitContent 时序一致） |
| TodoUpdated | 无（todo 面板走现有 AgentLoop→ReplView 直读路径，分册 2 迁移） |
| TurnFinished | 无动作（✻/📋 行分册 2 才由适配器拼；分册 1 core 仍拼 → 适配器不重复） |
| Interrupted | 无动作（⏹ 行分册 2 迁移） |
| ConfirmRequested | `confirm(m_prompt)` → 回 `ConfirmResponse`（经总线 reply 通道） |

**装配**（main.cpp / CLFRepl 构造）：bus 创建 → adapter 订阅（Coalescing for 增量、Lossless for 语义）→ agent.setEventBus(&bus)。

### 3.5 步骤 2 发射点挂接（代码改动面）

- AgentLoop：新增 `setEventBus(CLFEventBus*)` 成员；各 Section 内原 m_output 调用处**并行**发事件（分册 1 = 双轨：ICLFOutput 直调保留 + 事件发射新增——逐字对齐的保障，分册 2/3 再切轨）
- ToolExecutor：构造加 bus 指针（默认 nullptr 不发射）；ToolStarted/ToolFinished 发射点
- confirm 双轨：分册 1 保留现有阻塞回调路径；总线请求-响应原语**接入但不切换**（qa 验证后分册 2 切换）
- 发射点清单 = §2.3 归属决策表（逐点实施）
- **⚠ 审查修正（2026-10-12）：步骤 2 适配器"只记录不渲染"**——若适配器此时即接线渲染，ContentDelta 等事件会与直调路径**双份显示**。定案：步骤 2 适配器仅做事件计数/序列断言（qa 用），**零 ICLFOutput 调用**；步骤 3 切轨时同步"移除 core 直调 + 适配器启用渲染映射"（输出逐字对比作为切轨验收）

---

## 四、测试计划

### 4.1 新增 qa 套件

| 套件 | 用例 | 覆盖 |
|---|---|---|
| qa_CLFEvents | E1 事件字段默认值 / E2 seq 递增 / E3 turnId 传播 | 协议头基础 |
| qa_CLFEventBus | B1 订阅接收（单/多订阅者）/ B2 保序（1000 事件 seq 严格递增）/ B3 Lossless 满阻塞生产者 / B4 Coalescing 满丢旧留新 / B5 慢消费者不阻塞其他订阅者 / B6 订阅者异常隔离（handler 抛异常不影响总线）/ B7 事件历史环（有界、满丢最旧）/ B8 publishRequestAndWait 配对成功 / B9 超时默认拒绝 / B10 错误 requestId 不影响等待表 | 总线机制全集 |
| qa_CLFTerminalEventAdapter | A1 ContentDelta→emitContent / A2 StatusChanged→setStatusKind / A3 ConfirmRequested→confirm→ConfirmResponse 往返 / A4 ToolFinished→finishProgress | 映射保真 |
| qa_CLFAgentLoop（扩展） | L1 runTurn 拆分后行为等价（既有用例全绿即证） | 拆分回归 |

**⚠ qa 线程纪律（审查补充 2026-10-12）**：boost::ut 在静态初始化期执行——总线/适配器测试**禁用内部线程**（构造参数 startDispatcher=false，手动 publish + 手动 drain；CLFTipsBar startTimer=false 同款惯例）；涉及真实线程的用例（阻塞生产者/请求-响应）用主线程模拟或条件变量短等待，禁止静态期起后台线程。

### 4.2 既有回归

- ctest 39/39 全绿（拆分前后各一轮，行为零变化证据）
- 输出快照对比：正常对话/中断/触顶/确认/自发 Worked 五场景前后逐字一致（快照存入 `测试/` 目录）

---

## 五、验收标准（步骤 0-2 出口）

1. ctest 全绿（含新增 3 套件）+ --version 冒烟
2. **TUI 观感逐字对齐**：五场景输出快照前后一致
3. ICLFOutput/CLFTerminal/CLFRepl **零改动**（git diff 验证）
4. 总线单测覆盖 §4.1 全部用例
5. 实机验收：用户正常使用一轮完整对话无感知差异

## 六、风险与开放项

- **双轨期事件与直调的时序偏差**：emitContent 直调 vs 事件异步入队——若观感出现闪烁/乱序，排查点 = 适配器 PostEvent 时序（设计已约束所有 ICLFOutput 调用回 UI 线程）
- **阻塞生产者（Lossless 满）**：理论场景（单回合事件 < 100 条 vs 容量 1024）——真出现 = 消费者死锁信号，日志 warn 告警
- **确认超时时长**：定案 60s 超时默认拒绝（用户拍板，参照 Claude Code）——与现状 CLFTerminal::confirm 无限等待为**预期行为变化**（挂机 60s 自动拒绝而非无限卡），实机验收关注点
- **confirm 双轨切换点**：分册 2 才切总线路径——分册 1 总线原语仅 qa 验证
