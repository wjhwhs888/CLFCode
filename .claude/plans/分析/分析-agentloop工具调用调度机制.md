# dsh Agent Loop 工具调用调度机制分析

> 分析对象：`@deepseek-ai/dsh-agent-loop`（`packages/core/agent-loop`）
> 分析时间：仓库版本线 `dsh 0.2.1-alpha.1`（分支 master）
> 结论一句话：**每个 step 内的工具调用默认并行，但并行度有上限（默认 10），且只有显式声明并发安全的工具才能并行；独占工具构成串行屏障；所有策略阶段与结果落盘严格保持模型顺序。**
>
> **落地状态 ✅（2026-10-14）**：本分析为分册 3（`归档/归档-事件化重构-3-并发工具调度器.md`）的机制权威——四阶段/提交游标/滚动池/失败静默已按本分析落地，实机并行+ESC 验收通过

---

## 0. 结论速览（TL;DR）

| 问题 | 答案 |
| --- | --- |
| 串行还是并行？ | **混合**。按调用自身的并发模式动态分组，连续并行安全调用并发执行，独占调用串行。 |
| 并行度上限？ | `maxParallelToolCalls`，默认 **10**（`DEFAULT_MAX_PARALLEL_TOOL_CALLS`），`1` 即完全串行。 |
| 谁能并行？ | 仅显式声明 `isConcurrencySafe` 且**返回严格 `true`** 的工具。其余一律独占（fail-closed）。 |
| 并发粒度？ | 只有「工具体执行」阶段重叠。pre-execute 守卫、post-execute、结果提交全部串行。 |
| 结果顺序？ | **模型顺序**，与完成顺序无关。 |
| 单个独占调用会阻塞后续并行吗？ | 会。它是屏障，且屏障持续到它的 **commit（含 post-execute）** 完成。 |
| 配置能热改吗？ | 能。`maxParallelToolCalls` 是 volatile 字段，每个工具组开始时重新采样。 |
| 取消时未启动的调用？ | 按模型序补写合成错误结果 `tool call aborted before dispatch`，保证会话可重放。 |

---

## 1. 术语与整体调用链

### 1.1 层级概念

- **turn（轮次）**：一次用户驱动的完整交互周期。
- **step（步骤）**：turn 内的一次「请求模型 → 处理响应」。一个 step 可能产出多个 `tool-call`。
- **tool-call group（工具组）**：本 step 内被判定为同一调度批次的一组调用——要么是一段连续的并行安全调用，要么是单个独占调用。
- **concurrency mode（并发模式）**：每个调用的调度分类，取值 `parallel` 或 `exclusive`。

### 1.2 调用链

```text
Agent.step()                                   packages/core/agent-loop/src/agent.ts
  └─ 请求模型，流式收完 assistant 消息
     └─ toolCalls = message.content.filter(block => block.type === 'tool-call')
        └─ executeToolCalls(loopCtx, turn, step, toolCalls, signal, acceptContext)
           │                                  packages/core/agent-loop/src/tool-calls.ts:60
           ├─ 分类：ctx.tools.executionMode(exec).kind   → parallel | exclusive
           ├─ 分组：mode === 'parallel' ? planned.slice(next) : [first]
           └─ runGroup(...)               packages/core/agent-loop/src/tool-calls.ts:122
              ├─ 有界滚动池（fillPool / commitReady）
              └─ 调用 ToolRuntime 调度器四阶段
```

关键代码位置（`packages/core/agent-loop/src/agent.ts:532-537`）：

```ts
const toolCalls = message.content.filter(block => block.type === 'tool-call')
if (toolCalls.length === 0) return { kind: 'completed' }
const { concluded } = await executeToolCalls(
  this.loopCtx, turn, step, toolCalls, signal,
  context => this.inbox.splice('next-step', this.inbox.nextStep.length, 0, [context]),
)
return concluded ? { kind: 'completed' } : null
```

注意最后一行：`concluded` 为真表示某个工具结果声明了「本轮可以结束了」（`concludesTurn`），loop 直接收尾；否则返回 `null`，驱动层继续下一个 step。

---

## 2. 分类：谁能并行

分类入口是工具注册表的 `executionMode`（`packages/core/tools/src/index.ts:1303`）：

```ts
executionMode(exec: ToolExecutionInput): ToolExecutionMode {
  const tool = this.resolveExecution(exec.name, exec.agent, exec.parent !== undefined)
  if (!tool?.isConcurrencySafe) return { kind: 'exclusive' }
  try {
    const concurrencySafe: unknown = tool.isConcurrencySafe(exec.arguments)
    return concurrencySafe === true ? { kind: 'parallel' } : { kind: 'exclusive' }
  } catch {
    return { kind: 'exclusive' }
  }
}
```

类型定义（`packages/core/tools/src/index.ts:358-360`）：

```ts
export type ToolExecutionMode =
  | { kind: 'parallel' }
  | { kind: 'exclusive' }
```

### 2.1 fail-closed 语义（重要）

以下所有情况**一律判为 exclusive**：

1. 工具定义里没有 `isConcurrencySafe` 字段（未声明）；
2. 工具对当前 agent 作用域不可见 / 未声明 / 解析不到；
3. `isConcurrencySafe` 返回的不是严格 `true`（例如返回 `'yes'` 字符串、`1`、truthy 对象都算）；
4. `isConcurrencySafe` **抛异常**（被 `try/catch` 吞掉）。

设计意图很明确：并发的风险由工具作者显式承担，默认永远是安全的串行。

### 2.2 逐参数判定

`isConcurrencySafe` 接收 `exec.arguments`，因此**同一个工具可以按参数切换模式**。测试 `packages/core/tools/tests/execution-mode.spec.ts:62` 就是这种情况：

```ts
isConcurrencySafe: args => args.mode === 'read',
```

即「读模式可并行、写模式独占」是官方支持的用法。

### 2.3 `isConcurrencySafe` 不会泄漏给模型

`packages/core/tools/tests/execution-mode.spec.ts:124-135` 验证：该字段不会出现在面向模型的 `schemas()` 投影里。它是纯调度元数据。

---

## 3. 分组：串行屏障 + 滚动池

### 3.1 主循环（`tool-calls.ts:85-103`）

```ts
let next = 0
let concluded = false
while (next < planned.length) {
  // Commit before classifying again so registry changes affect unstarted calls.
  const first = planned[next]!
  const mode = ctx.tools.executionMode(first.exec).kind
  const group = mode === 'parallel' ? planned.slice(next) : [first]
  const outcome = await runGroup(ctx, turn, step, group, mode, signal, acceptContext)
  next += outcome.consumed
  concluded ||= outcome.concluded
  if (outcome.aborted) {
    for (const call of planned.slice(next)) appendSkippedToolCall(session, turn, step, call.block)
    return { concluded }
  }
}
return { concluded }
```

要点：

- 遇到独占调用时，组只含它自己（`[first]`），循环 `await` 到它整组跑完才继续 → **屏障**。
- 遇到并行调用时，组取「剩余全部」（`slice(next)`），但真正的边界由 `runGroup` 内部的惰性重分类决定（见 §5），返回的 `consumed` 就是实际消费的数量。
- 循环每轮都会重新分类，因此**注册表在上一组执行期间被修改，会影响尚未启动的调用**。

### 3.2 形态示例

模型一次返回 5 个调用（`R` = 并发安全读，`W` = 独占写）：

| 模型顺序 | 实际执行形态 |
| --- | --- |
| `R1 R2 W3 R4 R5` | `{R1,R2}` 并行 → `{W3}` 独占（屏障）→ `{R4,R5}` 并行 |
| `R1 R2 R3` | 单个组，池内最多 10 个并发（此处全并发） |
| `W1 W2 W3` | 三个各含一个元素的组，严格串行 |
| `R1 R2 R3`（cap=2） | 组内滚动：启动 R1,R2 → R1 完成、提交，启动 R3 → … |

对应测试：`packages/core/agent-loop/tests/tool-calls.spec.ts`
- `:109` 并行安全兄弟真正并发（全部启动后才有一个完成）
- `:126` 两个并行安全调用之间的独占调用形成屏障（产生 3 个组）
- `:311` `maxParallelToolCalls: 1` 完全串行（第一个 settle 前不会有第二个启动）

---

## 4. 组内执行：四阶段流水线与有界滚动池

### 4.1 ToolRuntime 调度器四阶段

并行能力来自一个 symbol-keyed 的内部接口（`packages/core/tools/src/index.ts:465-474`）：

```ts
export interface ToolRuntimeScheduler {
  /** 物化输入、跑有序的 pre-execute/守卫闸门，决定后续阶段。 */
  prepare(exec: ToolExecutionInput): Promise<ScheduledToolPreparation>
  /** 只跑 around-dispatch/工具体阶段。 */
  dispatch(exec: ToolRunContext): Promise<ScheduledToolDispatch>
  /** 跑 post-execute 与定义方内容 finalize，然后物化并通知。 */
  finalize(exec: ToolRunContext, result: ToolExecutionResult): Promise<ToolExecutionResult>
  /** 只跑定义方内容 finalize，然后物化并通知（跳过 post-execute）。 */
  finish(exec: ToolRunContext, result: ToolExecutionResult): ToolExecutionResult
}
```

`prepare` 返回三种结果：

- `{ kind: 'dispatch', exec }` —— 需要真正执行工具体；
- `{ kind: 'post-result', exec, result }` —— 已有结果，但仍需走 post-execute；
- `{ kind: 'final-result', exec, result }` —— 终态结果，跳过 post-execute。

这个拆分是「策略串行、执行并行」得以实现的关键：`prepare` 与 `finalize/finish` 是**有序闸门**，`dispatch` 是**唯一可重叠的段**。

### 4.2 组内状态

`runGroup` 维护（`tool-calls.ts:133-141`）：

```ts
const slots: (Slot | undefined)[] = group.map(() => undefined)
const callSeqs: Array<SessionSeq | undefined> = group.map(() => undefined)
let nextToStart = 0   // 下一个待启动的模型序下标
let committed = 0     // 已提交的连续前缀长度
let started = 0       // 已启动数量
let aborted: boolean = signal.aborted
let concluded = false
let schedulerFailure: { error: unknown } | undefined
```

- `slots[i]` 存第 i 个调用的已 settle 结果（含 `needsPost` 标记）。
- `callSeqs[i]` 存第 i 个 `tool/call` 事件的 seq，供对应 `tool/result` 引用。
- `committed` 只能**沿模型序连续推进**——这是「结果按模型序落盘」的实现核心。

### 4.3 提交游标 `commitReady`（`tool-calls.ts:147-163`）

```ts
const commitReady = async (): Promise<void> => {
  while (committed < group.length) {
    const slot = slots[committed]
    if (slot === undefined) break          // 头部未 settle，停止推进
    const call = group[committed]
    const result = slot.needsPost
      ? await ctx.tools[TOOL_RUNTIME_SCHEDULER].finalize(slot.exec, slot.result)
      : ctx.tools[TOOL_RUNTIME_SCHEDULER].finish(slot.exec, slot.result)
    appendToolResult(session, turn, step, call!.block, result, callSeqs[committed]!)
    for (const context of result.additionalContexts ?? []) acceptContext(context)
    concluded ||= result.concludesTurn === true
    committed++
  }
}
```

- 队首未 settle → 直接 break，后面已完成的调用**必须等**（保序代价）。
- `additionalContexts` 也按模型序注入下一步 inbox，而非按完成序。

### 4.4 启动一个调用 `startCall`（`tool-calls.ts:165-197`）

```ts
const startCall = async (index: number): Promise<void> => {
  const call = group[index]!
  callSeqs[index] = appendToolCall(session, turn, step, call.block)   // 先记 tool/call
  started++
  const prepared = await ctx.tools[TOOL_RUNTIME_SCHEDULER].prepare(call.exec)  // 有序 await
  throwSchedulerFailure()
  switch (prepared.kind) {
    case 'dispatch': {
      const promise = ctx.tools[TOOL_RUNTIME_SCHEDULER].dispatch(prepared.exec).then(
        (outcome) => {
          slots[index] = { exec: prepared.exec, result: outcome.result, needsPost: outcome.kind === 'post-result' }
          return index
        },
        (error: unknown) => { schedulerFailure ??= { error }; return index },
      )
      inFlight.set(index, promise)      // ← 只有这里真正并发
      break
    }
    case 'post-result':
      slots[index] = { exec: prepared.exec, result: prepared.result, needsPost: true }
      break
    case 'final-result':
      slots[index] = { exec: prepared.exec, result: prepared.result, needsPost: false }
      break
  }
}
```

关键观察：

1. `tool/call` 事件在 `prepare` **之前**追加，且 `startCall` 被顺序 `await`，因此**日志里的 `tool/call` 严格按模型序**。
2. `prepare`（含 pre-execute 钩子、权限/守卫校验）是**逐个 await 的**，不重叠。测试 `:360` 验证 `tools/pre-execute` 与 `tools/post-execute` 观察到的顺序恒为模型序。
3. 只有 `dispatch(...)` 返回的 Promise 被放进 `inFlight`，成为并发点。
4. `dispatch` 的 rejection 不直接抛出，而是记进 `schedulerFailure` 并让 promise **正常 resolve**（返回 index），从而让 `Promise.race` 能继续收敛。

### 4.5 填池 `fillPool`（`tool-calls.ts:199-215`）

```ts
const fillPool = async (): Promise<void> => {
  while (!aborted && nextToStart < group.length && inFlight.size < maxParallelToolCalls) {
    const nextCall = group[nextToStart]!
    if (nextToStart > 0 && mode === 'parallel'
      && ctx.tools.executionMode(nextCall.exec).kind !== 'parallel') break   // 惰性重分类
    await startCall(nextToStart)
    nextToStart++
    throwSchedulerFailure()
    await commitReady()
    throwSchedulerFailure()
    if (signal.aborted) aborted = true
  }
}
```

三个终止条件：已取消、已全部启动、在途数达上限。

`maxParallelToolCalls` 的读取发生在 `runGroup` 开头（`tool-calls.ts:132`）：

```ts
const maxParallelToolCalls = ctx.agentLoop.config.maxParallelToolCalls.get()
```

因此是**每个工具组采样一次** —— 组内上限固定，跨组重新读取，热更新在下一个组生效。

### 4.6 收敛循环（`tool-calls.ts:219-236`）

```ts
try {
  await fillPool()
  while (inFlight.size > 0) {
    const settledIndex = await Promise.race(inFlight.values())
    inFlight.delete(settledIndex)
    throwSchedulerFailure()
    await commitReady()
    throwSchedulerFailure()
    if (signal.aborted) aborted = true
    await fillPool()
  }
} catch (error: unknown) {
  schedulerFailure ??= { error }
  await Promise.allSettled(inFlight.values())
  throw schedulerFailure.error
}
```

「谁先完成谁补位」的滚动池：任一派发 settle → 从在途集合移除 → 推进提交游标 → 尝试补充新调用。

---

## 5. 惰性重分类（lazy reclassification）

`fillPool` 里这一行是整套机制的精华：

```ts
if (nextToStart > 0 && mode === 'parallel'
  && ctx.tools.executionMode(nextCall.exec).kind !== 'parallel') break
```

含义：即使当前组是在 `parallel` 模式下开启的，**每个调用启动前都重新判定一次**。若下一个调用此刻已变成独占，立即停止补充（`break`），把剩余部分留给主循环的下一轮，在那儿形成真正的屏障组。

这保证了「注册表在执行过程中被修改」时的正确性。对应测试：

- `tool-calls.spec.ts:153` —— 独占屏障替换掉某工具后，待处理调用被重新分类；
- `tool-calls.spec.ts:194` —— 一个结果观察者（post-execute）把下一个调用变成独占时，停止补充。

同时注意 `startCall` 里 `prepare` 完成后的分支也允许「不发派发直接落结果」（`post-result` / `final-result`），因此 pre-execute 阶段被拒绝（deny）的调用不会进入并发池，但仍按模型序落盘 —— 见测试 `:411`。

---

## 6. 顺序语义汇总

无论并发形态如何，以下全部是**模型序**：

| 顺序项 | 实现位置 | 测试 |
| --- | --- | --- |
| `tool/call` 事件追加 | `startCall` 顺序 await，先于 prepare | `:279` |
| `tools/pre-execute` 钩子观察顺序 | `prepare` 串行 await | `:360` |
| `tools/post-execute` 钩子观察顺序 | `commitReady` 沿游标串行 | `:360` |
| `tool/result` 落盘顺序 | `commitReady` 的连续前缀游标 | `:230`、`:253` |
| `additionalContexts` 注入顺序 | `commitReady` 内按序 `acceptContext` | `:383` |

测试 `:279` 断言了一段真实的日志交错（cap = 2，4 个并行调用）：

```text
tool/call:c1, tool/call:c2, tool/result:c1, tool/call:c3, ...
```

即：c1、c2 先并发启动；c1 完成后立刻提交其结果，并补位启动 c3。`tool/call` 之间、`tool/result` 之间各自有序，整体呈现交错。

而 `tool/result` 的**完整序列**始终是 `[c1, c2, c3, c4]`，与完成顺序无关。

> 设计注解（`tool-calls.ts` 文件头）：
> *"Dispatch may overlap, while policy, results, and result context remain model-ordered."*

---

## 7. 取消（Abort）语义

取消是协作式的，信号由 step 持有并下传给每个调用。

### 7.1 行为

1. **立即停止补充**：`fillPool` 的 `!aborted` 条件与 `if (signal.aborted) aborted = true` 双重检查。
2. **排空已启动**：收敛循环继续 `Promise.race` 直到 `inFlight` 清空，起过的调用结果照常提交（提交也是模型序）。
3. **未启动的补合成结果**，保证重放合法（`tool-calls.ts:238-243`）：

```ts
if (aborted) {
  for (const call of group.slice(started)) appendSkippedToolCall(session, turn, step, call.block)
  return { consumed: group.length, aborted: true, concluded }
}
```

`appendSkippedToolCall`（`tool-calls.ts:250-261`）写入一对 `tool/call` + `tool/result`，错误内容为：

```text
Error: tool call aborted before dispatch
```

错误码 `TOOL_ABORTED_BEFORE_DISPATCH`，`info.name` 为 `AbortError`。

4. **后续组不再执行**：`executeToolCalls` 主循环看到 `outcome.aborted` 后，把剩余所有调用同样补上跳过的合成结果，然后返回。

### 7.2 要点

- 取消**不会丢弃已产生的 `tool/result`** —— 会话历史的完整性优先。
- 独占屏障在并行组取消后**不会被执行**（测试 `:590`）。
- 已启动调用的 `additionalContexts` 会被「寄存」进下一步 inbox（测试 `:522` 的 *parks accepted additional contexts*）。
- 流式文本不丢：README 明确 *"Cancellation preserves streamed text already delivered to the user"*。

对应测试：`tool-calls.spec.ts:459 / :490 / :522 / :590`。

---

## 8. 失败静默（Failure Quiescence）

调度器内部异常的处理契约（`tool-calls.ts:219-236` + `runGroup` 头部）：

1. **停止新派发**：`throwSchedulerFailure()` 在关键节点抛出首个失败。
2. **排空已启动的派发**：`await Promise.allSettled(inFlight.values())`。
3. **以首个失败 reject**，交由所属 step 记录保守的恢复结果后收尾。
4. `schedulerFailure ??= { error }` 保证**只保留第一个**失败（后续失败不覆盖）。
5. 若记录恢复结果本身也失败，原始调度器错误仍被保留（测试 `:725`）。

对应测试：`tool-calls.spec.ts:633`（describe: failure quiescence）、`:808`（先停止新派发并排空已启动实体，再上报首个失败）。

---

## 9. `concludesTurn` 提前收尾

若任一被接纳的结果带 `concludesTurn === true`：

```ts
concluded ||= result.concludesTurn === true      // commitReady 内
```

`runGroup` → `GroupOutcome.concluded` → `executeToolCalls` 返回 `{ concluded: true }` → `agent.ts` 的 step 返回 `{ kind: 'completed' }`，**跳过后续 step**。

注意 `concluded` 采用 `||=` 累积，且不受该组是否被取消影响（取消组也返回已累计的 `concluded`）。

---

## 10. 配置项 `maxParallelToolCalls`

定义（`packages/core/agent-loop/src/index.ts:292-300`）：

```ts
export interface Config {
  /**
   * Maximum parallel-safe calls in flight per agent step. `1` is serial;
   * omission defaults to {@link DEFAULT_MAX_PARALLEL_TOOL_CALLS}.
   */
  maxParallelToolCalls: Volatile<number>
  agents: (AgentOptions & { ... })[]
}
```

| 项 | 值 |
| --- | --- |
| 默认值 | `10`（`constants.ts`: `DEFAULT_MAX_PARALLEL_TOOL_CALLS = 10`） |
| 校验 | `z.number().step(1).min(1)` —— 整数且 ≥ 1；`0` 与 `1.5` 都在插件加载时被拒绝（测试 `:274`） |
| 类型 | `Volatile<number>` —— **volatile Config 字段**，非启动配置 |
| 采样时机 | 每个工具组开始时（`runGroup` 开头）重新 `get()` |
| 生效范围 | 全局，作用于每个由 factory 创建的 agent（测试 `:330`） |
| 热更新 | 支持 `live.update({ maxParallelToolCalls: 1 })`（测试 `packages/core/agent-loop/tests/settings.spec.ts:23-36`） |

配置目录文档：`docs/config-catalog.md:106`、`docs/config-catalog.zh.md:108`。

README 描述（`packages/core/agent-loop/README.md:48`）：

| 字段 | 默认 | 说明 |
| --- | --- | --- |
| `maxParallelToolCalls` | `10` | 每个 step 在途的并行安全工具调用数；`1` 为串行 |

设置页存在对应的配置卡片，命名空间 `settings.agentLoop`，可视化的热调该值。

---

## 11. 内置工具的实际并发声明分布

### 11.1 声明 `isConcurrencySafe: () => true`（默认可并行）

| 工具 | 位置 |
| --- | --- |
| `read` | `packages/fs/.../read.ts:136` |
| `read-image` | `packages/fs/.../read-image.ts:238` |
| `web_fetch` | `packages/web/.../fetch.ts:498` |
| `web_search` | `packages/web/.../search.ts:364` |
| `session_search` | `packages/session-query/tool-session-query/src/index.ts:90` |
| `session_event_search` | 同上 `:103` |
| `session_event_read` | 同上 `:118` |
| subagent 派遣工具 | `packages/subagent/tool-subagent/src/index.ts:470` |

subagent 工具的声明带有一段有信息量的注释：

```ts
// Children never mutate the parent session; the one parent-owned write
// (tasks.start) is a synchronous commutative insertion.
isConcurrencySafe: () => true,
```

即：子 agent 不写父会话；唯一由父方持有的写操作（`tasks.start`）是一次**同步且可交换的插入**，因此并发安全。

### 11.2 未声明（一律独占）

`edit`、`write`、shell/终端执行、`run_code`、todo/plan 类写入、`send_message`、`interrupt_agent` 等**均未声明** `isConcurrencySafe`，走独占路径，天然串行且互为屏障。

> 实践含义：模型在同一 step 里发起的多个文件编辑/命令执行，会是**严格串行**的；只有只读检索类操作能真正并发。

---

## 12. PTC（`run_code`）内的子调用：同一套规则

程序化工具调用（PTC，programmatic tool calling）在 `run_code` 沙箱内发起的子调用，复用**完全相同的并发规则**（`packages/core/tools/src/ptc.ts:404-445` 及 `:608`）。

设计要点（原文注释摘录）：

```text
// The per-run scheduler uses the registry's staged interface and follows
// the same concurrency rules as the native loop. It also follows the
// native loop's SEQUENCING: every ordered stage (the dispatch-start
// append, prepare = pre-execute/guards, finalize/finish = post-execute,
// context deferral, the settle append) runs inside ONE driver lane, so
// ordered policy stages never overlap each other and only the
// around-dispatch/body stage runs concurrently. Starts are strictly
// submission-ordered; results commit in submission order through the
// head-of-line cursor. Consecutive parallel-classified calls overlap up
// to maxParallel; an exclusive call waits for the pool to drain, runs
// alone, and holds its barrier until its COMMIT (post-execute included)
// completes, exactly like a native exclusive group. Classification is
// re-read via executionMode() immediately before each start (a registry
// mutation while queued can flip a call exclusive), matching the native
// scheduler's lazy reclassification.
```

子调用的分类回调：

```ts
classify: () => registry.executionMode(input).kind,
```

内部结构与原生 loop 对应关系：

| 原生 loop | PTC 子调度 |
| --- | --- |
| `startCall` | `PendingDispatch.start()`（含 `tool/ptc-dispatch-start` 追加 + `prepare`） |
| `dispatch` 入 `inFlight` | `entry.flight` |
| `commitReady` 游标 | `commit()`（post-execute + context 延迟 + settle 事件） |
| `executionMode` 重读 | `classify()` |
| 取消跳过 | `abandon()` → reject `run_code run is over (...); <name> tool call abandoned` |

PTC 一次运行内的子调用计数由 `dispatches` 管理；取消时未排队的调用被放弃，已执行体的处理依赖执行器对 abort 信号的反应。

PTC 模式下的额外约束：模型**直接**调用原生工具（无 parent）会被拒绝为 `UNKNOWN_TOOL`，工具体不会执行 —— 测试 `packages/core/agent-loop/tests/tool-calls.spec.ts:882/909`。

---

## 13. 时序示意

### 13.1 `[R1, R2, W3, R4, R5]`，cap = 10

```text
主循环
 ├─ group1 = [R1, R2]  (parallel)
 │    startCall(0) → tool/call:R1 → prepare(R1) → dispatch(R1) ┐ 并发
 │    startCall(1) → tool/call:R2 → prepare(R2) → dispatch(R2) ┘
 │    commitReady → tool/result:R1, tool/result:R2（模型序）
 ├─ group2 = [W3]      (exclusive，屏障)
 │    startCall → tool/call:W3 → prepare → dispatch → 等待完成
 │    commitReady → tool/result:W3
 └─ group3 = [R4, R5]  (parallel)
      ...
```

`W3` 的屏障持续到它的 `tool/result` 落盘（含 post-execute）为止，之后 `R4/R5` 才开始。

### 13.2 组内滚动，cap = 2，四个并行调用

```text
fillPool: 启动 c1, c2            → inFlight = {c1, c2}
race: c1 先 settle
  → 提交 c1 的结果（tool/result:c1）
  → fillPool 补位：启动 c3       → inFlight = {c2, c3}
race: c2 settle → 提交 tool/result:c2 → 补位 c4
race: c3 settle → 提交 tool/result:c3
race: c4 settle → 提交 tool/result:c4
```

日志实际交错：`call:c1, call:c2, result:c1, call:c3, ...`（与测试 `:279` 断言一致）。

---

## 14. 测试覆盖索引

全部位于 `packages/core/agent-loop/tests/tool-calls.spec.ts`：

| 行号 | 用例 |
| --- | --- |
| `:108` | describe **grouping and barriers** |
| `:109` | 并行安全兄弟真正并发（全启动后才有完成） |
| `:126` | 两并行调用之间的独占调用形成屏障（3 组） |
| `:153` | 独占屏障替换工具后，待处理调用被重分类 |
| `:194` | 结果观察者使下一个调用变独占时停止补充 |
| `:229` | describe **model-order results despite out-of-order settlement** |
| `:230` | 后发先至时 `tool/result` 仍按模型序提交 |
| `:253` | 派生历史按模型序配对 call/result，不受日志交错影响 |
| `:273` | describe **rolling pool honors maxParallelToolCalls** |
| `:274` | 插件加载时拒绝非法的全局配置（`0`、`1.5`） |
| `:279` | 至多启动 cap 个，随 settle 补充 |
| `:311` | `maxParallelToolCalls: 1` 完全串行 |
| `:330` | 配置上限作用于每个 factory 创建的 agent |
| `:359` | describe **ordered middleware and additional contexts** |
| `:360` | `tools/pre-execute` 与 `tools/post-execute` 观察到模型序 |
| `:383` | `additionalContexts` 按模型序注入而非 settle 序 |
| `:411` | pre-execute 拒绝/报错被有序记录且不发派发 |
| `:458` | describe **abort handling** |
| `:459` | 进并行组前信号已取消 → 不启动任何调用 |
| `:490` | 有序 pre-execute 期间取消 → 跳过派发并停止启动兄弟 |
| `:522` | 取消后停止补充、提交已启动结果、寄存已接纳的 additional contexts |
| `:590` | 并行组取消后不再执行独占屏障 |
| `:633` | describe **failure quiescence** |
| `:725` | 记录恢复结果也失败时，保留调度器原始错误 |
| `:749` | 隔离子 agent 与同作用域其他会话的待处理请求 |
| `:808` | 先停止新派发并排空已启动实体，再上报首个失败 |
| `:882` | describe **PTC mode native-tool denial through the agent loop** |
| `:909` | PTC 模式下拒绝模型直调原生工具：工具体不执行，会话记 `UNKNOWN_TOOL` |

另有：

- `packages/core/tools/tests/execution-mode.spec.ts` —— 分类本身（默认独占、逐参数、抛异常、非 `true` 返回值、schema 不泄漏）；
- `packages/core/agent-loop/tests/settings.spec.ts:23-36` —— `maxParallelToolCalls` 的热更新与校验。

---

## 15. 代码位置索引

| 关注点 | 位置 |
| --- | --- |
| Step 内拿到 toolCalls 并分发 | `packages/core/agent-loop/src/agent.ts:532-537` |
| 调度器主入口 `executeToolCalls` | `packages/core/agent-loop/src/tool-calls.ts:60` |
| 分组主循环 | `tool-calls.ts:85-103` |
| `parseArguments`（坏 JSON 保留原文，空串 → `{}`） | `tool-calls.ts:105-111` |
| `runGroup` | `tool-calls.ts:122` |
| `maxParallelToolCalls` 采样点 | `tool-calls.ts:132` |
| `commitReady`（提交游标） | `tool-calls.ts:147-163` |
| `startCall` | `tool-calls.ts:165-197` |
| `fillPool`（有界池 + 惰性重分类） | `tool-calls.ts:199-215` |
| 收敛循环 / 失败静默 | `tool-calls.ts:219-236` |
| 取消补合成结果 | `tool-calls.ts:238-243` |
| `appendSkippedToolCall` | `tool-calls.ts:250-261` |
| `appendToolCall` / `appendToolResult` | `tool-calls.ts:263-290` |
| 并发分类 `executionMode` | `packages/core/tools/src/index.ts:1303-1312` |
| `ToolExecutionMode` 类型 | `packages/core/tools/src/index.ts:358-360` |
| `ToolRuntimeScheduler` 接口 | `packages/core/tools/src/index.ts:465-474` |
| `TOOL_RUNTIME_SCHEDULER` symbol | `packages/core/tools/src/index.ts:480` |
| `ScheduledToolPreparation` 联合类型 | `packages/core/tools/src/index.ts:445-448` |
| 默认上限常量 | `packages/core/agent-loop/src/constants.ts` |
| Config 接口与 schema | `packages/core/agent-loop/src/index.ts:292-346` |
| PTC 子调度 | `packages/core/tools/src/ptc.ts:404-447`、`:608` |
| `isConcurrencySafe` 类型声明 | `packages/core/tools/src/schema.ts:508`、`:573`、`:626` |
| README 说明 | `packages/core/agent-loop/README.md:12/37/48/57/75` |
| 配置目录 | `docs/config-catalog.md:106` |

---

## 16. 设计权衡与使用建议

### 16.1 权衡

| 选择 | 收益 | 代价 |
| --- | --- | --- |
| 默认独占（fail-closed） | 未声明工具绝不会因并发而出错 | 新工具若不主动声明，享受不到并行收益 |
| 结果强制模型序 | 会话历史、重放、派生历史天然一致；模型看到的结果顺序与它给出的顺序一致 | 队首慢调用会拖住后面已完成的调用（head-of-line blocking） |
| 策略阶段（pre/post-execute）串行 | 中间件、权限、守卫无需考虑并发 | 钩子里的慢操作无法重叠 |
| 惰性重分类 | 注册表热变更安全 | 每个调用启动前多一次分类开销 |
| 取消时补合成结果 | 会话日志始终「每个 call 都有 result」，重放合法 | 日志里会多出被取消调用的痕迹 |
| volatile 配置 + 每组采样 | 可热调，且不撕裂组内语义 | 同一 step 内不同组可能用不同上限 |

### 16.2 给工具作者的建议

1. 只有真正无副作用或副作用可交换的工具才声明 `isConcurrencySafe`；只读检索是典型场景。
2. 利用**逐参数判定**：写操作独占、读操作并行。
3. 声明函数内**不要抛异常**表达「不安全」——直接返回 `false` 更清晰（抛异常虽也被 fail-closed 兜住，但会掩盖真实 bug）。
4. 返回布尔值本身：返回 truthy 的非 `true` 值等同独占。

### 16.3 给使用者的建议

- 想让文件编辑类操作也并行：需要为相应工具补 `isConcurrencySafe` 声明，且必须自行保证语义安全（同文件冲突、锁竞争等）。
- 想降低并发压力（例如沙箱/IO 受限）：把 `maxParallelToolCalls` 设为 `1` 即完全串行，且可热改，无需重启。
- 观察并发行为：看 session 日志里 `tool/call` 与 `tool/result` 的交错模式 —— `result` 之间恒为模型序。

---

## 附录 A：`runGroup` 关键不变量

1. `committed` 只跨越**连续的**模型序槽位推进，永不跳过。
2. `inFlight.size < maxParallelToolCalls` 是启动新调用的硬约束。
3. `schedulerFailure` 只记录**首个**失败。
4. 非取消路径结束时必有 `committed === started`（否则抛出 `tool-call scheduler: uncommitted settled calls`，该分支被标记为不可达）。
5. `callSeqs[i]` 必在对应 `tool/result` 追加前存在（`tool/result` 通过 `sourceEventSeqs: [callSeq]` 关联来源事件）。

## 附录 B：相关事件与错误码

| 名称 | 说明 |
| --- | --- |
| `tool/call` | 调用启动时追加，携带 `turn`、`step`、`callId`、`name`、`arguments` |
| `tool/result` | 结果提交时追加（`surfaceOp: 'append'`，`sourceEventSeqs: [callSeq]`），可带 `error.info` 与 `meta` |
| `tool/ptc-dispatch-start` | PTC 子调用启动事件 |
| `tool/ptc-dispatch` | PTC 子调用 settle 事件 |
| `TOOL_ABORTED_BEFORE_DISPATCH` | 取消后未派发调用的错误码 |
| `TOOL_ABORTED` | 工具体已调用后取消的错误码 |
| `TOOL_RUNTIME_SCHEDULER` | 调度器 symbol |
| `UNKNOWN_TOOL` | PTC 模式下模型直调原生工具的拒绝错误码 |

---

*文档结束。若仓库后续演进，请以 §15 的代码位置索引为入口核对最新实现。*
