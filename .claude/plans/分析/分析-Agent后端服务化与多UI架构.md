# 分析：Agent 后端服务化与多 UI 架构

> 状态：**已落地 ✅（2026-10-14）**——本分析为事件化重构（`归档/归档-事件化重构.md`）的上游动机与定调依据；步骤 0-4 实施验收完成归档，9 条定调全落档；步骤 5（跨进程协议与新 UI）待用户排期激活
> 关联：阶段 3（第三方集成）泛化前置分析；上游动机 = 用户发现 CLFAgentLoop 承担 TUI 显示职责

---

## 一、背景与目标

### 1.1 需求来源

用户计划将 agent 接入其他项目、作为**后端服务**运行，新 UI 界面可能是 **Qt** 或 **HTML**（Web）。当前架构假设"CLFCode 只有一个 TUI"，该假设被新需求推翻。

### 1.2 目标（本分析要回答的问题）

1. 当前 core（CLFCore）与 UI（CLFTUI）的耦合点全景——哪些是合理的接口抽象、哪些是 TUI 特化泄漏
2. 目标架构形态：core 以什么合同面向任意 UI（TUI / Qt / HTML / 跨进程）
3. 从现状到目标的过渡路线与关键决策点

### 1.3 与历史定调的关系（设计文档定调提取，详见 §八）

- 阶段 2 §七③ 定案原文："UI 插件：不迁——UI 是宿主侧本身；**可替换 UI 非项目目标**"——前提被 1.1 新需求推翻，该定案需修订
- 阶段 2 §九 协议适配器预留：接口按"请求抽象/响应抽象"设计、**不按 OpenAI 字段设计**——本次事件协议同理：**不按 TUI 字段设计**，两条预留同一精神
- 阶段 3 分册 ask_user 缺口："无 ABI 通道无需求，将来以新回调/新服务接口表达"——新 UI（HTML）下 ask_user 类交互很可能成为真实需求，应纳入首版事件协议评估
- 阶段 3 出口标准：激活条件 = 阶段 2 完成 + 用户排期——本分析即该排期的前置输入

---

## 二、当前架构全景（取证，2026-10-12 亲验）

### 2.1 依赖方向（正确的部分，应保持）

```
CLFTUI ──▶ CLFCore ──▶ CLFTypes(ICLFOutput 接口)
  实现接口               依赖接口
```

- CLFCore **零 include CLFTUI**（grep 验证）——依赖倒置成立，ADP/SDP 合规
- `ICLFOutput`（CLFTypes，17 方法四窄接口：内容 4 / 进度状态 8 / 交互 2 / 辅助 3）是 C3 拆窄成果，ISP 良好
- 装配点唯一：`main.cpp:161-167`——`agent.setOutput(&terminal)` + `CLFRepl repl(agent, historyDir, &terminal, &pluginManager)`

### 2.2 Core→UI 输出耦合清单（越界部分）

| # | 位置 | 内容 | 判定 |
|---|------|------|------|
| 1 | CLFAgentLoop.cpp:177 | 状态行文案 `m_labels.working + " for " + s + "s…"` | ⚠️ TUI 文案在 core |
| 2 | CLFAgentLoop.cpp:531-533 (appendWorked) | `"\n \n✻ " + worked + " for ..." + "\n \n"` 拼进 **finalContent** | ⚠️ 装饰格式在 core，且**落库** |
| 3 | CLFAgentLoop.cpp:562-564 (finishTurn) | `"\n📋 任务清单（全部完成）:"` 收尾行拼进 finalContent | ⚠️ 同上 |
| 4 | CLFAgentLoop.cpp:865 (emitInterrupted) | `"\n⏹ 已中断\n"` | ⚠️ 同上 |
| 5 | CLFAgentLoop.cpp:447/:513 | Exception 行、kTurnCapNotice 触顶提示 | ⚠️ 同上 |
| 6 | CLFToolExecutor.cpp:718-721 | progressSummary "● thought for Xs · 本轮 X tok" | ⚠️ 摘要行格式在 core |
| 7 | AgentLoop 状态通道调用 | setStatusKind(Running/Done/Warn/Error) | ✅ 领域语义，合理 |
| 8 | AgentLoop 内容转发 | 流式 chunk emitContent、appendThinking/clearThinking | ✅ 内容生产者，合理 |
| 9 | AgentLoop 中断协议 | onInterrupt 回调注册/清空 | ✅ 进程内信号，合理（跨进程需演进） |

### 2.3 UI→Core 输入耦合

| # | 位置 | 形态 | 判定 |
|---|------|------|------|
| 1 | CLFRepl.cpp:117 | `m_agent.setConfirmCallback(...)`——**阻塞式** `std::function<bool(string)>` 绑定 Terminal 确认条 | ⚠️ 同步阻塞；异步 UI 需请求-响应协议 |
| 2 | AgentLoop.cpp:73 | `m_output->onInterrupt(callback)`——UI 线程触发、core 侧 atomic 标志 | ✅ 现形态可用；跨进程需消息化 |
| 3 | CLFRepl | 用户输入 → `agent.runTurn(input)` 同步调用 | ⚠️ 调用方向 UI→core 同步；服务化后输入是异步消息 |

### 2.4 数据与显示耦合（落库）

- 装饰行（✻/📋/⏹）混入 `finalContent` → `finishTurn` `addMessage("assistant", finalContent)` → `CLFSessionFileCtx::appendTurn`（消息序列差集）落 jsonl
- **resume 回显依赖这些行**：历史会话流的视觉完整性靠"落库内容=显示内容"保证（设计初衷）
- 现状落库数据模型已是"消息序列"（CLFMessage 向量），但 **assistant content 内混有呈现装饰**——数据模型半成形、内容不纯

### 2.5 接口抽象不完整（TUI 特化泄漏）

| # | 证据 | 说明 |
|---|------|------|
| 1 | CLFRepl.cpp:144/:411 `dynamic_cast<CLFTerminal*>(m_output)` | 折叠块切换等 TUI 专属能力不在接口上，UI 层对具体类型转型 |
| 2 | `setStatus` 覆盖式单行状态槽 | TUI 状态栏概念；Qt/HTML 是徽章/卡片 |
| 3 | `showFoldedBlock`/`emitStyledLine(LineStyle)` | 折叠交互、diff 着色枚举——TUI 呈现概念，语义应是"摘要+可展开内容""行类型" |
| 4 | `setStatusHold` | 状态槽"高优先级保持"——TUI 专属 |
| 5 | CLFTerminal::gray/cyan/bold 静态工具 | UI 层内使用（banner 拼装），尚可接受 |

### 2.6 现状小结

一句话：**依赖方向正确（接口倒置），但接口合同是"TUI 形状"的**——同步阻塞确认、覆盖式状态槽、样式枚举、装饰文案进数据。这些是"进程内、单 TUI"假设的产物，服务化后全部需要演进。

---

## 三、目标架构（事件协议方向，草案）

### 3.1 核心原则（含用户架构定调 2026-10-12）

用户预想架构（四点，本分析采纳为机制定案）：
1. **UI 只负责渲染显示**——不关心业务逻辑，纯呈现
2. **请求发起在 UI，但不直接调用接口**——发事件（含内容）
3. **后端接收事件 → 读取内容 → 调用发送接口**——AgentLoop 订阅"用户输入事件"后走自己的流程
4. **回复以事件方式推送，谁需要谁订阅**——发布-订阅：TUI/落库/新 UI/调试各自订阅；落库成为订阅者后装饰行自然出数据

由此推演的设计原则：
1. **core 发语义事件，不发文案/样式**——✻/📋/⏹、"Working for"、diff 着色由各 UI 自己决定呈现
2. **交互异步化**——confirm 请求-响应事件对、中断信号、用户输入均为消息，不再阻塞调用
3. **落库与显示分离**——会话数据模型独立（消息序列 + 元数据），UI 从数据重建视觉；装饰不进数据
4. **协议层可序列化**——同一事件流：进程内直接调用 / 跨进程 JSON 传输（协议适配器多协议预留点）
5. **事件总线是唯一通道**——ICLFOutput 不再是 core 的合同，降级为 CLFTerminal 内部"事件→渲染"的实现细节；本地订阅者同步分发、跨进程传输异步

### 3.2 事件协议形态（草案，待设计细化；已含 §3.9 查证结论）

**双通道分层**（Claude Code 双信封同款）：
- **语义帧（主通道）**：保序不丢、有界队列
- **内容增量（旁路通道）**：渲染加速用，溢出可合并/丢弃

输出事件（core → UI，语义帧）：
- `turn_started{input, turnId}` / `content_delta{text}`（旁路，纯文本无装饰）/ `reasoning_delta{text}`（旁路）/ `thinking_cleared`
- `tool_started{callId, name, args概要}` / `tool_finished{callId, name, ok, summary, errorKind}`（完成可乱序，callId 关联）
- `status_changed{kind}`（Running/Done/Warn/Error 语义保留）
- `todo_updated{items}`（结构化，替代 📋 拼行）
- `turn_finished{turnId, workedSec, todoSnapshot}`（✻ Worked 行由 UI 拼）
- `interrupted`

输入请求（UI → core）：
- `user_input{text}` / `confirm_response{requestId, accepted}` / `interrupt`

**请求-响应原语**：confirm = core 发 `confirm_requested{requestId, prompt}` → 阻塞等 `confirm_response{requestId}`（超时默认拒绝）——用户定调：确认必须等事件、严格把关。

**消息稳定 id**：turnId / callId / requestId 为协议关联键（jsonl 落库与显示流跨面对齐用——现状 jsonl 无稳定 id，设计期补）。

### 3.3 各层职责

| 层 | 职责 |
|----|------|
| CLFCore | 编排 + 领域状态 + 事件发射 + 会话数据落库（数据模型） |
| 协议层（新增/复用） | 事件序列化/传输（进程内直调 / JSON / 未来多协议） |
| CLFTUI | 事件消费者 + TUI 渲染；用户操作 → 请求事件（保持现有观感逐字对齐） |
| Qt/HTML UI（新） | 各自的事件消费者与渲染 |

### 3.4 过渡形态（TUI 零破坏）

```
Core ──事件──▶ 适配器(事件→ICLFOutput) ──▶ CLFTerminal（现有实现不动）
                    │
                    └──▶（未来）JSON codec ──▶ WebSocket ──▶ HTML UI
```

### 3.5 可复用资产（设计文档取证发现）

**dsh 蓝本已跑通"事件协议驱动 UI"路线**（`设计/dsh/设计-dsh后端接入-实现方案.md`）：
- 帧协议：newline-delimited JSON-RPC 2.0 over stdio（并发写锁串行化、stdout 纯协议通道）
- `session.event` 主事件流：14 种事件类型（turn/start|end、assistant/chunk|message、tool/call|result、todo/write、session/status 等），按 sessionId 过滤，带 type/seq/time
- **§4.2 事件→ICLFOutput 映射表**：text-delta→emitContent、reasoning-delta→appendThinking、tool call+result→showProgress/finishProgress、status→setStatus、确认类→confirm——**证明"事件→现有 UI"适配路径在本项目已有实证蓝本**
- 教训（可借鉴）：idle 是活动结束最可靠信号；客户端必须补整轮等待超时 + 强杀兜底；订阅每轮重建、filter 过滤根会话

**克制先例（B3-2）**：阶段 1 曾评估"todo 变化走事件通道（onTodoChanged）"，定案零收益不加——事件协议设计应覆盖"UI 需要什么"而非"core 发生什么"，本节 3.2 草案照此裁剪。

---

### 3.6 工具执行并发策略（机制级定案，2026-10-12 用户调研 + pro 吸收）

**机制权威参考**：用户调研文档 `分析/分析-agentloop工具调用调度机制.md`（dsh 0.2.1-alpha.1 源码级分析，16 节——四阶段流水线 / 提交游标 / 滚动池 / fail-closed / 取消语义全机制）。

**现状取证**（CLFToolExecutor.cpp:429）：`execute(calls)` 单线程 for 循环**严格串行**——一次响应的 N 个 tool_calls 顺序执行、按声明序回填 context、再进下一轮 API。墙钟 = Σt。

**协议语义基础**：OpenAI 兼容协议下一次响应的多个 tool_calls 默认可并行（有依赖的调用模型分轮发）；工具执行是回合耗时大头，并行收益显著。

**机制定案（吸收 dsh + Claude Code 查证）**：

1. **混合调度**：每个 step 内按并发模式动态分组——连续并行安全调用并发、独占调用串行且构成屏障（屏障持续到其提交完成）；`maxParallelToolCalls` 默认 10、1 = 完全串行
2. **并发安全声明（fail-closed）**：CLFTool 加函数式 `isConcurrencySafe(arguments)`——仅显式声明且严格 true 才并行；未声明/异常/非 true 一律独占；**逐参数判定**（读模式并行、写模式独占）；纯调度元数据**不进工具描述**（对照 dsh：不泄漏给模型）
3. **四阶段流水线**（"策略串行、执行并行"核心）：`prepare`（守卫：确认/权限/中断检查，**串行闸门**）→ `dispatch`（工具体，**唯一并发段**）→ `finalize`（post 处理：结果归一化/落库，串行）→ 提交——**confirm 归 prepare 阶段：并发下确认天然互斥**（解决两工具同时要确认的难题）
4. **提交游标（模型序落盘）**：committed 沿模型序连续推进，队首未完成后面必须等（head-of-line blocking 为保序代价）——与既有"按声明序回填 context"一致，游标为具体实现
5. **滚动池**：完成即补位（fillPool + 谁先 settle 谁释放槽位）；启动前**惰性重分类**（插件热卸载/注册表变更安全——我们 /plugin unload 是真实场景）
6. **取消语义**：停止补充 → 排空已启动（照常提交）→ 未启动补合成"未执行"结果——**与既有 appendNotExecutedResults 协议闭合同构** ✓；CLFProcessRunner Job Object 杀树承载广播式取消
7. **失败静默**：首个失败停止新派发、排空在途、以首个失败上报
8. **concludesTurn**：已有 A5 机制 ✓ 与 dsh 同构
9. **配置**：maxParallelToolCalls 进配置表（26 字段 +1）；volatile 热改后续评估（先启动配置）

**与事件化的关系**：**先事件化、后并发化**——第一批保持顺序执行（事件序 = 执行序，TUI 逐字零变化）；事件协议第一天预留 callId + 序号（tool/call、tool/result 配对，dsh 同款）。

**分阶段**：第一批（事件化，顺序执行）→ 第二批（并发执行，协议已预留）→ 第三批（跨进程 + 新 UI）。

### 3.7 runTurn 拆分（事件化前置重构，2026-10-12 用户定调）

**现状取证**：`CLFAgentLoop::runTurn` 单函数约 440 行（:83-521）——God Function：3 个 lambda（防御/usage/计时器）、流式+同步双路径、错误重试、触顶收尾全在体内。

**拆分方案（5 个 Section 函数，runTurn 瘦身为纯编排）**：

| Section | 职责 | 未来事件发射点 |
|---------|------|---------------|
| `prepareTurn(userInput)` | 中断检查、摘要 cooldown、轮初记账、状态点、计时器启动 | `turn_started{input}` |
| `requestAssistantTurn(...)` | 单次 API 请求：流式/同步收敛 + 错误重试 + applySelfWorkedMark + accumulateUsage | `content_delta{text}` / `reasoning_delta` |
| `executeToolCalls(toolCalls)` | addAssistantToolCalls + executor.execute + 结果回填 + concluded 判定 | `tool_started/tool_finished{callId}` |
| `finishTurn(...)`（已有） | 清单收尾 + worked + 落库 + 状态点 | `turn_finished{workedSec, todos}` |
| `runWrapUp(...)` | 触顶：wrap-up 请求 + 提示 + worked | 同上 |

**纪律**：① 纯结构重构、行为零变化——lambda 提为成员函数，逻辑原样搬移，ctest + 实机观感逐字回归兜底 ② turnTimer/thinking 计时器 RAII 生命周期覆盖整个回合，拆函数不破坏析构序 ③ 每个 Section 即事件发射点骨架——拆分即事件化铺路。

**历史同款先例**：A1 批次（CLFRepl::run() 拆 CLFReplView/CLFInputHandler）+ C2a 批次（AgentLoop 拆角色三对象）——本次为第三波，达成 2.4 分册"AgentLoop 纯编排"原始目标。

### 3.8 用户定调记录（2026-10-12，逐条）

1. **分发模式**：参考 Claude Code CLI 与 dsh 策略（查证结论见 §3.9）——倾向异步队列
2. **confirm**：**要等事件**（阻塞等 confirm_response，不状态机化）——确认是安全关键点："需要确认的，必须严格检查是否执行，归用户解释"
3. **无人订阅不丢弃**：需异步处理 + 缓冲。**取证修正**：现有存盘（CLFSessionFileCtx::appendTurn → CLFSessionManager::appendTurn ofstream）是**同步**的，无异步机制可复用——事件缓冲需新设计
4. **协议头文件**：新增零依赖协议头（如 CLFEvents）——**该头文件即协议规范**，新增 UI 以它为合同
5. **状态行计时**：UI 自己计时（turn_started 事件后 UI 起 timer）——core 不负责显示计时
6. **Tips 静默判定**：显示层自己判定（订阅内容事件自计时）——刚修的 busy 感知逻辑保留在 UI 层，数据源从 activityCount 迁移为事件活动
7. **老会话还原**：现状痛点（记录不够、resume 显示不全——collectEchoLines 跳过 tool 消息、summary 行不回显）——本次大改重做落库/恢复模型：落库改事件订阅者、恢复按事件流重放，动的较多
8. **TUI 接口不动**：确认——步骤 0-3 期间 ICLFOutput/CLFTerminal 零改动，事件→ICLFOutput 适配器为 TUI 侧新增，现有渲染路径逐字保持
9. **E2E 往后排**：先解决自己的问题（事件化改造优先），E2E 实施排到改造之后

### 3.9 分发模式参考查证（2026-10-12，Claude Code CLI 官方文档/社区源码分析 + dsh 蓝本）

**Claude Code CLI 机制**（官方文档确定 + 社区源码分析高可信，细节随版本漂移）：
- **内核**：单线程异步生成器 + while(true) 状态机（pull 语义）——慢消费者在 yield 点挂起反压上游，**不无界缓冲**；不是每工具一线程
- **事件分层（双信封）**：语义帧（assistant/user/result 粗粒度）与 SSE 细帧（stream_event）**分通道**——token 增量不进主通道，只作渲染加速
- **渲染**：同一事件循环协作式调度（~16ms 节流），非独立线程；危险在消费者侧同步重活卡唯一线程
- **工具并发**：并行 + 默认上限 10（CLAUDE_CODE_MAX_TOOL_USE_CONCURRENCY）；**按工具分级**：Read/Grep 永远并发安全、Bash 仅只读可并行、Edit/Write 永不并发安全；**结果按请求序产出**（完成可乱序）；tool_result 必须同一条消息按 id 配对（违反 API 400）
- **审批**：`control_request`/`control_response` 阻塞握手 + request_id + 60s 超时默认拒绝——**总线上的请求-响应原语**（await future + resolve）
- **落库**：同一事件源、两个出口——transcript writer 是订阅者（富信封落盘、树结构 parentUuid），显示流瘦信封上屏；message.id 跨面对齐（勿按记录 uuid 关联）

**dsh 蓝本**：JSON-RPC 2.0 帧 + 每订阅内部队列 + filter（session 树过滤）——订阅队列模式。

**对我方的结论**（C++ 无 async generator，用订阅队列等效）：
1. **内核保持同步执行流**（AgentLoop 单线程状态机，拆分后更清晰）——执行流语义不变，confirm 阻塞等事件
2. **事件发射 = 入每订阅者队列**（push + **有界队列 + 溢出策略**）：语义帧保序不丢；token 增量旁路通道可合并/丢弃（渲染加速用）
3. **慢消费者不阻塞内核**：订阅者各自线程消费队列；单生产者保序
4. **双通道分层**：语义帧（turn_*/tool_*/confirm_*）主通道 + 内容增量旁路通道
5. **请求-响应原语**：confirm = request_id 关联 + 阻塞等响应 + 超时默认拒绝（用户定调：要等事件）
6. **工具并发分级**：复用现有能力标签（B1 批次 m_risk/m_isSearch/m_isRead）扩展 isConcurrencySafe 分级——Read/搜索安全并发、Write 永不并发、命令按只读判定；结果按请求序回填
7. **落库**：writer 订阅者 + 富信封；消息稳定 id 跨面对齐（jsonl 现状无稳定 id——设计期补）

### 3.10 实施路线（六步，每步独立可验收）

```
步骤 0：runTurn 拆函数（纯结构，行为零变化）        ← 事件化前置
步骤 1：CLFEventBus + 事件类型定义（CLFTypes 层，纯数据）
步骤 2：事件发射点挂接 + 事件→ICLFOutput 适配器（TUI 零变化）
步骤 3：文案职责迁移（✻/📋/⏹ 归 UI）+ jsonl 落库改事件订阅者（装饰出数据）
步骤 4：工具执行并发（协议已预留 callId/序号）
步骤 5：JSON 协议层 + 跨进程传输 + 新 UI（Qt/HTML）
```

步骤 0-3 = 第一批"摘干净显示"（用户定调：先不聚焦新 UI）；步骤 4-5 = 第二批/第三批。

---

## 四、差距分析（现状 → 目标）

| 维度 | 现状 | 目标 | 差距 |
|------|------|------|------|
| 输出合同 | 文本行 + 样式枚举 + 状态槽 | 结构化事件 | 中（事件定义 + 适配器） |
| 交互 | 阻塞 confirm + 回调中断 | 异步请求-响应 | 大（confirm 流程改造） |
| 文案职责 | core 拼装饰行 | UI 拼 | 小（迁移 5 处 + 适配器拼回） |
| 落库 | 装饰混入 assistant content | 纯数据模型 | 中（jsonl 兼容性需处理） |
| 进程边界 | 单一进程 | 可跨进程 | 协议层序列化 |
| 历史定调 | 阶段 2 §七③ 单 TUI 前提 | 多 UI | 文档修订 |

---

## 五、路线选项（待用户拍板）

- **A. 一步到位全量事件化**：core 重构为纯事件发射，TUI 重写为事件消费者。收益最大，风险最大（观感回归工作量大）。
- **B. 增量过渡（倾向）**：① 事件协议定义（CLFTypes，纯数据）→ ② 事件→ICLFOutput 适配器（TUI 零变化，ctest 全绿验证）→ ③ core 文案职责迁移 + 落库/显示分离 → ④ JSON 协议层 + 新 UI 接入。每步独立可验收。
- **C. 新 UI 另起炉灶**：Qt/HTML 各自独立实现 agent 调用。违背复用原则（CRP/CCP），否决倾向。

---

## 六、关键决策点（待用户拍板）

1. **进程模型**：同进程库（Qt 直接 link clf_core）/ 独立服务进程（HTTP/WebSocket，HTML 必需）
2. **事件协议落地形态**：类型化 struct + JSON codec（可序列化）还是直接 JSON 对象流
3. **ask_user 是否纳入首版协议**：新 UI 下"向用户提问"很可能需要；dsh 侧无此通道（审批在 IPC 侧）——纳入则协议含 `prompt_requested/prompt_response` 事件对
4. **排期**：与挂起任务的关系（E2E 自动化回归待实施、阶段 3/dsh 决策门挂起）——本分析产出后是否排入设计
5. **jsonl 兼容性**：老会话文件里的装饰行如何处理（resume 时过滤/保留）

---

## 七、风险与开放项

- 事件协议设计过度/不足的平衡（过早抽象 vs 接口反复）——以"现有 TUI 观感逐字对齐"为验收基准
- confirm 异步化涉及 ToolExecutor 执行流改造（阻塞语义 → 状态机/协程）
- 平台层/插件层是否受波及（插件 handler 不直接触 UI，影响预计为零——待设计期确认）
- 与 dsh 阶段 3 的关系：本需求是阶段 3 的泛化，dsh 若未来接入可复用同一协议层

---

## 八、历史定调提取（2026-10-12 Explore agent 取证，出处均已核实）

1. **阶段 2 §七③ UI 不插件化**（`归档-阶段2-自身插件化设计方案.md` §二 论证定案③ + §七③）：UI 是插件宿主的宿主侧（main 极薄后 UI 就是 main 的组成）；"UI 插件"唯一场景 = 可替换 UI 实现，**非本项目目标**。§六出口标准第 6 条：core/basic/UI 内建。
2. **插件不推 UI**（2.1 分册）：9 个 handler 均同步返回，执行期进度由宿主侧（CLFToolExecutor）负责；给 ABI 加 onProgress 属无源之功能——UI 输出面全部留在宿主侧。
3. **阶段 2 §九 协议适配器预留**（不埋雷硬约束）：clf_plugin_api 头不得暴露协议细节；接口按"请求抽象/响应抽象"设计、不按 OpenAI 字段设计；单协议行为等价为回归基线。
4. **阶段 3 dsh 资产**：JSON-RPC 2.0 帧协议 + session.event 14 事件 + 订阅观察者（§5.1 session 树过滤）+ §4.2 事件→ICLFOutput 映射表 + 客户端两超时（整轮 idle、shutdown 强杀）+ 确认链在 IPC 侧。
5. **ask_user 缺口定案**：无 ABI 通道无需求（dsh 审批链在 IPC 侧完成）；将来以新回调/新服务接口表达，必要时 ABI 版本 +1，不改变 UI 内建定案。
6. **C3 拆窄初衷**（`归档-阶段1-代码审查与模块重构.md`）：ICLFOutput 原是 god 接口；拆四窄接口 + B5 纪律（新方法一律默认实现，别顺手拆接口）。
7. **B3-2 克制先例**：todo 变化事件通道（onTodoChanged）评估后定案零收益不加——事件协议按 UI 需要裁剪。
8. **jsonl 会话协议**（`归档-会话追加式保存.jsonl.md`）：行类型 header/turn/todo_snapshot/complete/summary；对齐 dsh 事件流（每次 todo/write 即落盘）；未知类型静默跳过安全网。

---

## 九、结论

1. 现状：**依赖方向正确，合同是 TUI 形状**——文案/样式/同步交互/装饰进数据四处泄漏，是"进程内单 TUI"假设的产物
2. 目标（用户定调）：**事件总线唯一通道**——core 发语义事件、UI 各自呈现（含计时/静默判定）、数据与显示分离、协议头文件即规范；confirm 阻塞等事件（安全关键点）；无人订阅缓冲不丢弃
3. 路径：六步增量（拆函数 → 事件总线 → 适配器 → 文案迁移+落库订阅者 → 并发 → 跨进程+新 UI）；TUI 零破坏承诺；老会话恢复模型随批重做；E2E 排后
4. 并发调度：机制级定案已吸收（dsh 调研为权威参考：混合调度/四阶段流水线/提交游标/fail-closed/取消合成——§3.6）
5. 下一步：分析定稿拍板 → 设计文档（协议字段级定义 + runTurn 函数级签名 + 调度器设计 + 分步实施计划）→ 测试文档
