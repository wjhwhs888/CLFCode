# 设计-UI信息展示借鉴（P0 + P1）

> 依据：《分析/分析-UI信息展示借鉴.md》（dsh Web UI 展示设计对照 + 咱们白噪声源盘点）
> 原则：不改 ICLFOutput 六通道合同结构；所有改动落在发射端数据组织 + CLFUI 渲染层；每项独立可测、可回滚。
> 范围：P0 五项 + P1 三项。审批卡强化、恢复回显折叠、消息级时间戳、ContextMeter 留待 P2（本设计不含）。
> **2026-08-14 深度审查修订**：全部涉及代码逐行读毕。修订 6 处（F1-F9 见附录），其中 P0-3 经核实为"已实现"而删除，P1-2 由"独立统计条"改为"增强现有 summary 行"。
> **2026-08-14 外部审查（dsh agent）裁决**：5 点意见经逐条核实**全部采纳**（F10-F14 见附录）——含 1 个实质风险（读工具失败可见性）、2 处文案笔误、1 个潜伏缺陷（工具执行期界面冻结）、1 个设计空白（状态点生命周期）。
> **2026-08-14 外部审查第三轮裁决**：TurnGuard 覆盖缺陷（F20）**采纳**——接线改为显式全表；:262/:318 补入 Error 清单（F21）；迭代上限 :335 定为 Warn。

## 0. 现状代码地图（2026-08-14 已核实）

| 环节 | 文件 | 现状 |
| --- | --- | --- |
| 显示合同 | `src/CLFTypes/ICLFOutput.hpp` | 六通道：内容/状态/确认/中断/进度块/思考分离 |
| 缓冲与发射 | `src/CLFUI/CLFTerminal.{hpp,cpp}` | 行缓冲 + lineStyles 并行数组（diff 着色用）；m_statusText 单槽；m_progressLines 瞬时槽；thinking 独立缓冲 |
| 渲染器 | `src/CLFUI/CLFRepl.cpp` Renderer | 行渲染 + 3 样式着色；思考折叠行"Thought for Xs (ctrl+t 展开)"；statusLine = statusText 非空才渲染 dim 文本；modeLine 模式色 auto绿/analyze青/edit橙/manual灰 |
| Agent 循环 | `src/CLFCore/CLFAgentLoop.cpp` | turnTimer 每秒 setStatusTextOnly("Working for Xs…")（:70）；**"⏹ 已中断" 9 处、两版文案**（\n 前缀有无），均伴随 clearThinking()；"✻ Worked for Xs" 进内容+上下文（:305-308，m_labels.worked 默认 "Worked"） |
| 工具执行 | `src/CLFCore/CLFToolExecutor.cpp` | 渐进模式 3 行 progress {thinking 前缀 / 工具行 / 结果行}（:539-540）；写工具 diff 预览（信息行+@@ 头+"..." 分隔+行号着色）；summary 行"Thought for Xs，read N, edited M (ctrl+t)"（:547-565）；✗ 行已 100 字符截断（:505/515） |
| 统计 | `CLFTypes.hpp:60` ToolStats | `searchCount/readCount/totalCalls` 已收集；**totalCalls/searchCount 未在 UI 展示**（read/edited 进了 summary） |
| 搜索截断 | `CLFSearchContent.cpp:22` | 停表式：500 行满即停，尾部不可见 |
| diff | `CLFDiff.{hpp,cpp}` | LCS + 渲染行数超限 → truncated=true + 部分 diffLines（最多约 3000 行）；renderDiff 超限时"truncReason + 部分 diff 全部渲染"——**3000 行灌进滚动区是真实缺口** |

## 1. 设计决策（D1 已拍板，其余默认通过）

- **D1 状态色语义（已拍板）**：analyze 青(CyanLight) → 紫(Magenta)；蓝(CyanLight) 让给 running 状态点。FTXUI 色板有 Magenta，可行。
- **D2 合同演进**：ICLFOutput 新增 `virtual void setStatusKind(StatusKind) { }` **带默认实现**——现有 MockOutput/实现零破坏；StatusKind ∈ {None, Running, Done, Warn, Error}。另增 `virtual void requestRefresh() { }` 默认空实现（CLFTerminal 覆写为 PostEvent(Custom)，见 D4/F13）。**两通道清除协议**：setStatus("") 只清文本、setStatusKind(None) 只清种类，互不联动——调用方需显式清各自通道（`CLFThinkingIndicator.cpp:23` 的 setStatus("") 不动 kind）。
- **D3 截断两层分离**：模型侧（search_content 结果进上下文，token 预算职责）→ head 240 + tail 240 环形缓冲（总预算 500 不变）；UI 侧（diff 渲染纯显示）→ headTailCap 16+16。
- **D4 动画驱动**：不新增定时器线程。running 态动画帧由 Renderer 按 `steady_clock` 时间差推进（8 帧 ⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏，100ms/帧）。**刷新源两层**：流式期间 reasoning/text 增量事件自然驱动；工具执行期（无流式事件）由 turnTimer 每秒 tick 调 `requestRefresh()` 驱动（1Hz 最低帧率）。**顺带修复潜伏缺陷**：setStatusTextOnly/showProgress 均不主动刷新（CLFTerminal.cpp:234-248），工具执行期界面本就冻结——1Hz 驱动同时修复状态计时冻结。不新增线程，CPU 无空转。
- **D5 中断标记收敛**：CLFAgentLoop 9 处 + CLFToolExecutor 1 处 → 统一 helper（文案/样式/clearThinking/Warn 状态点一并收敛）。

## 2. 变更项设计

### P0-1 错误首行即摘要

- 现状：`CLFTerminal::emitError`（:286-288）输出红 ✗ + 全消息；AgentLoop 流式错误走 emitError（:169/185/235）；ToolExecutor 错误行已有 100 字符截断（达标不动）
- 改动：`CLFTerminal::emitError` 单点收敛——取首行，超 200 显示列截断加 "…"；`preview.errorMsg`（:390）若含换行同步首行化（实施时先验证 previewEdit 的 m_error 是否可能多行）
- 规格：错误折叠摘要 = 错误首行，error 色常显

### P0-2 head/tail 截断

- 改动 A（模型侧）：`CLFSearchContent` 停表式改为环形缓冲——`resultCount < 240` 入 head；否则入 tail 环形缓冲（容量 240，挤掉最旧）；停止条件 `>= kMaxResults(500)` 不变；输出 = head + `[中间省略 N 行]` + tail + 原 skippedLarge 段。N = 500 - 480 = 20（恰好满时）
- 改动 B（UI 侧）：`renderDiff`（CLFToolExecutor.cpp:175-282）emit 循环前先做 headTailCap 规划：diffLines 渲染行 > 32 时只 emit 前 16 行 + Context 样式"  … 其余 M 行" + 后 16 行；@@ 头、"..." 分隔行不受截断（hunk 结构完整）。truncReason 信息行保留在顶部
- 规格：显示侧最多 34 行（16+1+16+信息行/头），模型侧总预算不变

### P0-4 工具执行中最小化（dsh ToolRow 形态）

- 现状：3 行 progress = {"● Thinking for Xs", "  ⎿ tool(target)", "     ✓/✗ result"}（:532-540）；其中 thinking 前缀与状态栏 "Working for Xs…" **信息重复**（白噪声源）
- 改动：progress 收窄为**单行** `  ⎿ tool_name(target)`，尾部动画帧由 Renderer 按 D4 附加；thinking 前缀删除（状态栏负责计时）；resultLine 删除（成功结果归 summary，与现状语义一致——读类工具本来就不进永久内容）。**失败可见性保留**（F10）：非写工具失败（!toolOk）时发射永久 ✗ 行——:476 条件放宽为 `!useProgressive || isWriteTool || !toolOk`，复用 :504-508 失败行逻辑（errorMsg 100 字符截断）
- 写工具 diff 预览**保留**（核心价值，dsh 没有的差异化）；showProgress 唯一调用方是 ToolExecutor:540，改动面收敛
- 规格：执行中 = 一行含工具名+目标+动画；完成后 = summary 单行（现状语义）

### P0-5 中断标记单点收敛

- 现状：AgentLoop 9 处（:105/173/179/199/224/229/249/280/294）+ ToolExecutor 1 处（:332"  ⎿ ⏹ 已中断"）；文案两版（"\n⏹ 已中断\n" 与 "⏹ 已中断\n"）；每处均伴 clearThinking()
- 改动：`CLFAgentLoop::emitInterrupted()` 私有 helper——统一文案 `\n⏹ 已中断\n` + clearThinking() + `setStatusKind(Warn)`，9 处收敛；ToolExecutor 的 `⎿` 前缀版保留前缀（它是工具层级标记，不是 turn 级中断）
- 规格：turn 级中断消息全局唯一路径；工具层中断标记保持层级区分
- **幂等性裁决**（F17）：**不加 once 语义**——所有中断 emit 点后立即 return（:105/173/179/199/224/229/249/280/294），一轮内重复发射不可达；once 标志反而引入"每 turn 重置"的可变状态。测试兜底：T6 覆盖三个中断时点（流式中/重试等待中/工具执行中）各自恰好一条

### P1-1 四态状态点（StateDot）

- 改动：snapshot 增 `statusKind`；Renderer 的 statusLine 渲染条件改为 **statusText 非空 || statusKind != None**（F5 缺口修正：0-15s 内 running 点必须可见）；状态点：done=绿 `●`、warn=琥珀 `●`、error=红 `✕`、running=蓝(CyanLight) `●`+D4 动画帧；计时文本 **≥15s 才显示**（turnTimer :70 加阈值条件）
- 接线（F20 修订：**TurnGuard 析构不设 kind**——它在全部 return 路径执行，无条件 Done 会覆盖刚设的 Warn/Error；TurnGuard 保持现状只清文本）。**显式接线全表**（F21）：
  - Running：runTurn 开始（:53 附近）
  - Done：正常完成点——:314（完成块 return）
  - Warn：中断（P0-5 helper 各点）+ 迭代上限 :335（"Exceeded maximum tool call iterations" 属任务未完成，对齐 dsh max-tokens=warning 语义）
  - Error：**return 全表**——:169-170（流式 Stream error）、:185-186/:235-236（fatal HTTP）、:188-189/:238-239（Too many errors）、:261-264（Unexpected finish_reason，F21 新发现）、:316-318（catch 内 maxRetries return，F21 新发现）；**异常路径**由 CLFRepl::submit catch（:562-569）补 Error（F19，异常展开时无 return 点接线，Repl 侧兜底）
- modeLine 的 analyze 色按 D1 调整（CLFRepl.cpp:237）
- **状态点生命周期**（F14/F18）：Done/Warn/Error 常亮至下一轮 runTurn 开始（Running 覆盖）——零新增机制，turn 结果（含错误/中断）在空闲期持续可见；None 的**具体调用点**：`/resume`、`/clear` 命令处理后（CLFCommandDispatcher 处理链内定位）——新会话语义从干净状态开始；`/exit` 无需（进程结束）；TurnGuard 不设 None 也不设 Done（F20）；两通道清除协议见 D2

### P1-2 统计信息增强 summary 行（替代独立 StatsLine）

- 审查结论（F3）：独立 StatsLine 与现有 summary 行（read/edited 计数）和 "✻ Worked for Xs"（时长）**重复**；正确做法是增强 summary 行
- 改动：summary 行（CLFToolExecutor.cpp:547-565）从 "● thought for Xs，read N files, edited M files" 扩展为 "● thought for Xs，**N 工具** (read A · search B · edited C)"——totalCalls 与 searchCount 用局部计数器（m_stats 在 summary 之后才赋值 :567-569）。文案以 `m_labels->thought` 为准（默认小写 "thought"，F12）
- 时长归属不变："✻ Worked for Xs"（已进内容与上下文，不动）
- token 类字段（TTFT/输入输出）待 API usage 数据打通后追加（后续项）

### P1-3 思考单行折叠摘要

- 改动：折叠行在 running 态追加**实时尾行截断**（thinkingLines 最后一行，80 列截断）；完成态追加**首行摘要**；`ctrl+t` 交互不变
- 规格：折叠行 = `Thought for Xs · <摘要> (ctrl+t 展开)`——"Thought for"（大写）是 CLFRepl.cpp:207 硬编码，与 summary 的 `m_labels->thought`（小写）是**两处不同字符串**（F12），改造时勿混用；顺手修正 CLFRepl.hpp:53 注释（Ctrl+O → Ctrl+T）

## 3. 不改的部分（明确排除）

ICLFOutput 六通道结构、五区布局、快捷键体系、diff LCS 核心与超限保护、diff 顶部信息行与 @@ 头（审查确认已达标，F1）、安全确认流程、恢复回显折叠（P2）、消息级时间戳（P2）、ContextMeter（P2）、审批卡强化（P2）。

## 4. 测试方案（测试驱动，先写测试后改实现）

新增单测（Boost.UT，`src/test/`）：

| # | 用例 | 断言 |
|---|---|---|
| T1 | `headTailCap` 纯函数 | 恰好 N 行不截断；N+1 行出中间标记；空输入；单行；CJK 多字节不劈半 |
| T2' | search_content 环形截断 | 恰好 500 结果 → head 240 + tail 240 + "[中间省略 20 行]"；不足 480 → 无省略；>500 结果行为不变 |
| T2'' | renderDiff head/tail | truncated + 大 diffLines → emit 行数 ≤ 16+16+标记；@@ 头与 "..." 行不被截掉 |
| T3 | `emitError` 首行化 | 多行消息→仅首行；超宽→截断含 "…"（MockOutput 断言） |
| T4 | 状态点状态机 | Running→Done / Running→Warn / Running→Error→clear 序列，snapshot.statusKind 正确；statusText 空时 statusKind 仍可见（渲染条件）；Done 不自动清（F14 生命周期）；**中断/错误 return 路径 kind 不被 TurnGuard 覆盖**（F20：模拟中断 return → 断言 kind 仍为 Warn） |
| T5 | summary 增强 | totalCalls/searchCount/read/edited 计数正确；无工具调用时不发射（"无数据分组消失"） |
| T6 | `emitInterrupted` | 三个中断时点（流式中/重试等待中/工具执行中）各自触发 → 每时点恰好一条中断消息、文案唯一（MockOutput 计数）；kind=Warn |

回归：现有 ctest 全部用例（P0 第一批 13 条 + 历史用例）。

人工验收：五子棋用例观感对比（改造前后对照）；一次含大 diff 的写文件流程（head/tail 截断目检）；一次长任务（≥15s 计时出现 + 状态点动画）；对照分析文档 4 条白噪声源逐项确认。

## 5. 里程碑与实施顺序

| 阶段 | 内容 | 估时 |
|---|---|---|
| M1 | P0-1/P0-2/P0-4/P0-5 + **ICLFOutput 增 requestRefresh + turnTimer 1Hz 驱动**（P0-4 动画依赖，F13）+ T1-T3、T6 | 1 天 |
| M2 | P1-1/P1-3 + summary 增强 + T4、T5 + D1 色映射 | 1-1.5 天 |
| M3 | 回归 + 人工验收（五子棋 + 白噪声清单对照） | 0.5 天 |

每阶段结束 ctest 全绿才进下一阶段；任一变更可独立 revert。

## 6. 风险与回滚

| # | 风险 | 缓解/回滚 |
|---|---|---|
| R1 | analyze 色变更影响老用户习惯 | D1 已拍板；回滚=映射表一行 |
| R2 | 动画导致 CPU 空转/帧率异常 | D4 事件驱动，静止即停；最坏=现状 |
| R3 | setStatusKind 破坏 MockOutput | 带默认实现，零破坏 |
| R4 | search_content 输出格式变化影响模型侧 | 总行数预算不变（481≤500）；回归用例覆盖 |
| R5 | diff 截断后用户看不到完整改动 | 截断行数 16+16 可调常量；"… 其余 M 行" 提示存在；确认流程不变 |

## 7. 涉及文件

- `src/CLFTypes/ICLFOutput.hpp`（StatusKind 枚举 + setStatusKind 默认虚函数）
- `src/CLFUI/CLFTerminal.{hpp,cpp}`（emitError 首行化、snapshot 扩展 statusKind、动画帧推进）
- `src/CLFUI/CLFRepl.{hpp,cpp}`（状态点渲染、渲染条件修正、思考折叠摘要、modeLine 色映射、注释修正）
- `src/CLFCore/CLFAgentLoop.{hpp,cpp}`（emitInterrupted 收敛、StatusKind 接线、turnTimer 15s 阈值）
- `src/CLFCore/CLFToolExecutor.cpp`（progress 单行、headTailCap 应用、summary 增强、错误首行化）
- `src/CLFTools/CLFSearchContent.cpp`（环形缓冲 head/tail）
- `src/test/`（T1-T6 新增用例）

## 附录：深度审查修订记录（2026-08-14）

| # | 原设计 | 审查发现 | 修订 |
|---|---|---|---|
| F1 | P0-3 diff footer 计数 + 多 hunk 分隔 | 计数已在 diff 信息行与 ✓ 行两处显示；"..." 分隔与 @@ 头已实现；单文件 diff 无聚合需求 | **删除 P0-3** |
| F2 | "diff 超限整体拒绝"；search 取前段丢尾部 | 超限实为 truncReason + 最多 3000 行部分渲染；search 是停表式 | P0-2 spec 修正（环形缓冲 + renderDiff 应用点） |
| F3 | 独立 StatsLine（含耗时） | 与 summary 行、"✻ Cooked" 重复；totalCalls/searchCount 已收集未展示 | P1-2 改为 summary 增强 |
| F4 | "12 处中断标记" | 实为 9+1 处、两版文案、均伴 clearThinking | P0-5 精确化 + helper 含 Warn |
| F5 | statusLine 渲染条件未提及 | statusText 空时 running 点不可见（0-15s） | P1-1 补渲染条件 |
| F6 | 未说明 progress 现状 | 3 行中 thinking 前缀与状态栏重复（白噪声实证） | P0-4 删除前缀 |
| F7 | — | CLFRepl.hpp:53 注释 Ctrl+O 实为 Ctrl+T | P1-3 顺手修正 |
| F8 | D1 待拍板 | FTXUI 有 Magenta，可行 | 已拍板通过 |
| F9 | T2 测 footer 计数 | 随 P0-3 失效 | 替换为 T2'/T2'' 截断用例 |
| F10 | P0-4 删 resultLine 后读工具失败不可见（外部审查点 1） | :536-538 resultLine 是渐进读工具唯一反馈；:476 条件排除了永久内容 | 采纳：失败时保留永久 ✗ 行（条件加 `\|\| !toolOk`） |
| F11 | "Cooked" 文案笔误（外部审查点 2） | 默认标签 `worked="Worked"`（CLFTypes.hpp:162），main 未覆盖 | 采纳：全文改 "Worked" |
| F12 | summary "Thought" 大小写（外部审查点 3） | `m_labels->thought` 默认小写；CLFRepl 折叠行硬编码大写，两处不同字符串 | 采纳：文案引用精确化 |
| F13 | D4 刷新源缺口（外部审查点 4） | setStatusTextOnly/showProgress 不主动刷新 → 工具执行期界面冻结（潜伏缺陷）+ 动画停 | 采纳：ICLFOutput 增 `requestRefresh()` 默认空实现；turnTimer 每秒驱动（1Hz） |
| F14 | 状态点生命周期未定义（外部审查点 5） | Done 后无清除路径 → 绿点常亮 | 采纳：常亮至下一轮 Running 覆盖；两通道清除协议入 D2 |
| F15 | Error 接线漏 :169（第二轮复审） | :169 流式 hadError 的 emitError 未列入接线清单 | 采纳：Error 接线全覆盖（:169/:185/:235/:189/:239 + Repl catch） |
| F16 | 1Hz 驱动需 null 检查（第二轮复审） | :66 已有 m_output 检查，requestRefresh 须在其内（非交互模式 m_output=null） | 采纳：实现注意 |
| F17 | helper 需 once 语义（第二轮复审） | 引用场景不存在（:199 是 return 非 continue；所有 emit 点后立即 return，重复发射不可达） | 部分采纳：不加 once 标志；T6 扩展为三时点中断用例兜底 |
| F18 | None 调用点未列清单（第二轮复审） | 设计只给了方向 | 采纳：补具体点（/resume、/clear 处理后；/exit 无需；TurnGuard 不设） |
| F19 | Repl catch 绿点配红字不一致（我方补充发现） | :562-569 异常时 TurnGuard 已设 Done，红字 ✗ 异常与绿点矛盾 | 采纳：catch 分支补 setStatusKind(Error) |
| F20 | TurnGuard 析构 Done 覆盖 Warn/Error（第三轮复审） | TurnGuard 在所有 return 路径执行（含中断/错误 return），无条件 Done 会盖掉刚设的 Warn/Error | 采纳：TurnGuard 只清文本不设 kind；Done 改显式接线（:314） |
| F21 | return 全表核对（我方补） | :262 Unexpected finish_reason、:318 catch 内 return 不在原接线清单；:335 迭代上限 kind 未定 | 采纳：接线全表补齐；:335 定 Warn（对齐 dsh max-tokens=warning） |
