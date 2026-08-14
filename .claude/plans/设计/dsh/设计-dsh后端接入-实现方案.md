# 设计-dsh 后端接入 — 实现方案（基于代码侦察，2026-08-14）

> 依据：4 份代码侦察报告（协议服务端 / 事件流样例 / 单 exe 打包 / Python SDK 蓝本）+ 直接源码验证。
> 上游钉住版本：`47f943859bef60e4160492346772ded9b24f765a`（v0.1.0-rc.5），本地 checkout 一致。
> 前置决策（沿用《分析-dsh终端客户端接入.md》决策 1/2/3）：git 版本钉住、终端始终为入口 + 自动降级、版本更新半自动。

---

## 0. 结论

**可行，方案 A（双后端增量接入）落地条件全部满足**。协议面完整（3 方法 + 4 通知，newline-delimited JSON-RPC 2.0 over stdio），官方有 Python/TS 双客户端参考实现可直接对译，CLFCode 现有 `ICLFOutput`/`runTurn` 抽象天然适配。

**唯一实质障碍：Windows 部署形态**。dsh 官方单 exe runtime 明确不含 Windows（platforms.json 仅 linux-x64/arm64、macos-arm64，构建脚本硬编码 `PLATFORMS=['linux','macos']`，架构笔记标注 "Windows is a non-goal"）。因此 Windows 上必须走 **Node runtime 目录分发**（dsh 仓库内已实现过的 dev-only node 载体形态），或自行评估 `@yao-pkg/pkg --sea` win32 target（新工程，未验证）。本方案以 Node 目录分发为主线，单 exe win32 列为备选探索项。

---

## 1. 协议实现契约（C++ 传输层规格）

### 1.1 帧格式（来自 transport.ts / client.py，双方一致）

```
每帧 = JSON.stringify(message) + "\n"        // 紧凑序列化，无空白
```

- **线上绝不会出现多行 JSON**（字符串内换行被转义为 `\n` 两字符）→ C++ 按 `0x0A` 切帧安全。
- 读端必须容错：按行切 → `trim()`（容忍 `\r`）→ 空行跳过 → JSON.parse 失败静默丢弃（服务端行为，Node exe 启动可能往 stdout 打警告行）。
- 帧分类（核心状态机）：
  | 帧内容 | 含义 | C++ 处理 |
  |---|---|---|
  | 有 `id`(str/int) + 有 `method`(str) | 服务端→客户端请求（桥接） | 预留 `respond()`/`respond_error()`；标准流程可不实现但路由不能丢帧 |
  | 有 `id`，无 `method` | 响应 | 按 id 弹 waiter；`error` 成员存在=错误响应；无 waiter 则丢弃（迟到响应） |
  | 无 `id`，有 `method` | 通知 | fan-out 给订阅者 |
- 并发写必须持锁串行化（一次"序列化+写+flush"），防半行。
- stdout 纯协议通道；诊断走 stderr。

### 1.2 方法表（客户端→服务端，共 3 个）

| 方法 | 参数 | 返回 | 要点 |
|---|---|---|---|
| `initialize` | `{cwd, provider, model, maxTokens?}`（camelCase） | `{serverInfo:{name,version}}` | name 恒为 `deepseek-harness-sdk-runtime`；**无版本协商**，客户端校验 name/version 为字符串即可；maxTokens 必须为正安全整数（发送前自校验，否则 -32603） |
| `session/prompt` | `{sessionId, contentBlocks:[{type:"text",text}]}` | `{messageId}` | messageId 仅"入队受理凭证"；未知 sessionId 懒创建 agent+session，已知 id 复用（延续会话状态）；**无 per-prompt 完成标记** |
| `shutdown` | — | `{}` | 先回响应帧再 flush→dispose→exit(0)；幂等；**服务端无超时，客户端必须自备** |

- 错误：**只有 `-32603`**（message 文本，无结构化 code/data）；畸形帧从不回错误（-32700/-32600 不会出现）。

### 1.3 通知表（服务端→客户端，共 4 个）

| 通知 | params | 用途 |
|---|---|---|
| `session.event` | `{sessionId, event}` | **主事件流**，无过滤（覆盖运行时所有会话），必须按 sessionId 过滤；event 带 `type`/`seq`/`time`/`data` |
| `session.status` | `{sessionId, status}` | status 仅 `idle`/`running`；**idle 是活动结束的最可靠信号** |
| `subagent.started` | `{parentSessionId, childSessionId}` | 建 parent→child 亲缘链（客户端维护） |
| `subagent.finished` | `{provider, agentId, parentSessionId, childSessionId, status, stopReason, lastAssistantMessage?}` | 只报进程内本地 run；lastAssistantMessage 缺席 ≠ 空输出 |

### 1.4 session.event 类型表（event.type，14 种）

| type | data 要点 | UI 映射 |
|---|---|---|
| `agent/inbox/spliced` | inserted[].id 含 messageId = 本轮起点（receipt 门控） | 状态机内部 |
| `turn/start` / `turn/end` | turn 号；turn/end 带 `reason.kind`（completed/max-tokens/error/…） | 回合边界；reason.kind=单回合结果 |
| `step/start` / `step/end` | turn+step；工具调用只结束 step 不结束 turn | 工具执行层级 |
| `user/message` | 完整 UserMessage（含 plugin 注入、session/title 同级） | 消息流 |
| `session/title` | 标题 | 可忽略/展示 |
| `request/header` / `request/context` | system/tools（脱敏）、provider/model | 调试信息，可忽略 |
| `assistant/chunk` | chunk 子类型（见下）——**流式文本/思考/工具参数都在这里** | 流式渲染 |
| `assistant/message` | 完整 AssistantMessage + usage + sourceEventSeqs + surfaceOp | 消息落定；finalResponse = 倒序最后一条拼 text 块 |
| `tool/call` | callId, name, arguments（**模型产出的原始 JSON 字符串**） | 工具执行开始 |
| `tool/result` | toolCallId 关联；isError 布尔；user 角色消息 | 工具结果（✓/✗） |
| `todo/write` | todos 状态 | 待办展示 |
| `session/end-seed` | {} | 可忽略 |

`assistant/chunk` 的 chunk.type 子类型：`block-start`(blockType∈{reasoning,text,tool-call,…})、`reasoning-delta`（思考增量）、`text-delta`（可见文本增量）、`tool-call-delta`（**argumentsDelta 流式 JSON 碎片，必须缓冲拼接后整体解析**）、`block-end`（含组装好的整块）、`usage`（token 计数，含 reasoningTokens）、`finish`（reason.kind∈{stop,tool-calls,max-tokens,aborted,error}）。

### 1.5 run() 状态机（照抄 Python/TS 双客户端语义）

```
1. session/prompt → messageId
2. 订阅过滤：session 树过滤（subagent.started 维护 child→parent 链，向上追溯带 visited 防环）
3. receipt 门控：忽略此前一切通知，直到 session.event 且 event.type=="agent/inbox/spliced"
   且 event.data.inserted[].id 含 messageId
4. 收集事件（根会话事件进 events；子会话事件只进 notifications）
5. 结束判定：收到本会话 session.status:{"status":"idle"} → 返回
   （finalResponse=倒序最后 assistant/message 拼 text 块；finishReason=倒序最后 turn/end 的 reason.kind）
6. ⚠️ C++ 必须补 Python 缺失的两个超时：
   a) 拿到 messageId 后整轮 idle 等待超时（runtime 卡死防挂起）
   b) shutdown 后进程未退出的强制 kill 等待
```

### 1.6 生命周期（C++ 逐级复刻 shutdown 阶梯）

```
1. 协议 shutdown（有界等待，默认 1s；失败仅记诊断）
2. 关 stdin（EOF 触发 runtime dispose→exit(0) 路径）
3. 进程仍活 → TerminateProcess（Windows）
4. wait(timeout) → 超时 kill → wait
5. fail 所有挂起 waiter（TransportClosedError 等价物，带 exit code + stderr tail 诊断）
6. join reader/stderr 线程
```

进程崩溃感知：stdout EOF → fail 所有 waiter；stderr 必须**单独线程**排空（环形缓冲 400 行，拼进错误消息），否则子进程写满 stderr 管道缓冲会死锁。

### 1.7 环境变量注入（C++ 复刻清单）

| 变量 | 值 | 必需性 |
|---|---|---|
| `DSH_CORDIS_CONFIG` | cordis.yml 绝对路径 | **必需**（runtime 无内置 fallback，缺失直接 exit(1)） |
| `DSH_CWD` | 绝对工作目录 | 必需 |
| `DEEPSEEK_API_KEY` / `DEEPSEEK_BASE_URL` | 模型凭据 | 视模型适配器 |
| `DSH_SESSION_ROOT` | 可选会话根 | 可选 |
| `DSH_RUNTIME_MODE` | `exe`/`node` | Windows 走 node |

---

## 2. 部署形态（Windows）

- **主选：Node runtime 目录分发**——复刻 dsh 仓库内已实现的 dev-only node 载体：闭包树（`runtime/node/` = package.json + node_modules）+ Node ≥22.19（install.ps1 顺带下载/校验，或捆绑 node.exe sidecar）。CLFCode spawn 该进程，走 stdio JSON-RPC。
- **备选探索：`@yao-pkg/pkg --sea` win32 target**（dsh 未验证，属新工程；pkg 本身支持 win32 目标，且非持久 pwsh 工具面不需要 PTY）。
- 发布物需随包附 MIT LICENSE + THIRD_PARTY_NOTICES.md（合规义务）；pkg 产物源码原样无混淆（闭源需另行评估）。
- 包体预估：现 12M → +Node 闭包（150MB 量级，zip 压缩后显著小于此，待实测）。

## 3. Windows 工具面

Windows 版 cordis.yml 草案已落盘：`draft-cordis-win64.yml`（包名已对照钉住版本验证存在）。
- bash 行 → **pwsh 三件套**：`dsh-pwsh-local`（每次全新 `pwsh -Command`，非持久）+ `dsh-pwsh-sandbox`（ACL 受限令牌，Windows 上读不受限/写受限）+ `dsh-tool-pwsh`（模型面工具）
- 平台无关可用：`read`/`write`/`edit`（fs + fs-observation-policy）、`todo_write`、`subagent`（in-process spawn）
- POSIX-only 不可用：PTY/持久 bash（Windows ConPTY 为路线图工作）
- 保留：`sessions`（JSONL+zstd）、`compaction-basic`、`token-meter`、`session-checkpoints`、`agent-spine`、`llm-deepseek`（thinking enabled）

## 4. CLFCode 侧架构

### 4.1 模块划分（新增 `CLFBackend` 层，遵循现有分层）

```
src/CLFBackend/                  # 新增：dsh 后端（依赖 CLFTypes + CLFNetwork 复用 HTTP 无需）
├── CLFJsonRpcClient.hpp/.cpp    # 传输层：spawn/reader/stderr 线程/waiter 表/订阅/close 阶梯
├── CLFJsonRpcTypes.hpp          # 帧/事件/方法结构（nlohmann::json 承载）
├── CLFHarnessSession.hpp/.cpp   # 高层：run() 状态机（receipt 门控 + idle 判定 + 超时）
├── CLFBackendAdapter.hpp/.cpp   # 后端抽象实现：runTurn() → session.run()；事件→ICLFOutput
└── CLFBackendDetector.hpp/.cpp  # runtime 探测：安装目录 → 配置路径 → PATH → 降级判定
```

### 4.2 后端抽象（与现有直连后端并存）

- 新增接口 `ICLFBackend`（或扩展 `CLFAgentLoop`）：`runTurn(input) → {text, finishReason}` + `ICLFOutput*` 注入。现有 `CLFAgentLoop`（直连 DeepSeek）实现为一端，`CLFHarnessBackend` 实现为另一端。
- **事件→ICLFOutput 映射**（复用现有 UI，零改动或小改）：
  | dsh 事件 | ICLFOutput 方法 |
  |---|---|
  | assistant/chunk text-delta | `appendContent`（流式）→ `emitContent`（落定） |
  | reasoning-delta | `appendThinking`（Ctrl+T 折叠保留） |
  | tool/call + tool/result | `showProgress` / `setStatus` / `finishProgress(summary)` |
  | session.status running/idle | `setStatus`（Working → Cooked） |
  | assistant/message | `emitContent`（最终答案） |
  | tool/result isError | `emitError` |
  | 确认类（后续若接 dsh ask-user） | `confirm`（决策点） |
- 退出握手：Esc Esc → `shutdown` → 等回包 → 超时强杀（复用 CLFCommandExec 的 CreateProcess+管道模式，扩展为长期交互 + Job Object 杀树）。

### 4.3 main.cpp 组装

```
探测 dsh runtime（决策 2 拓扑）
  ├─ 有 → CLFBackendAdapter（spawn runtime，initialize 版本握手校验）
  └─ 无/坏 → CLFAgentLoop 直连（状态栏提示降级）
配置：agent_settings.json 新增 "backend": "auto"|"direct"|"dsh"（默认 auto）
```

## 5. 安全模式职责切分（决策点 3）

- 建议：**dsh 后端时客户端四模式降级为"透传"**，以 dsh sandbox-policy（cordis.yml 配置）为准——避免双确认链冲突。
- Windows 上 ACL 沙箱天然"读不受限/写受限"，与 CLFCode 四模式语义相近，映射成本低。
- 具体方案在实现阶段定，需实测 dsh 在 Windows 的确认请求协议形态（ask-user 事件）。

## 6. 会话持久化归属

- 方案 A 下**两套会话格式并存**：CLFCode 直连后端沿用 latest.json/归档；dsh 后端由 dsh 侧 JSONL（zstd）+ compaction 负责。
- 跨后端 `/resume`/`/history` 不承诺互通；dsh 模式按 sessionId 延续 dsh 会话，CLFCode 侧 `/resume` 列表可透出 dsh 会话目录（实现阶段定）。

## 7. 里程碑与工作量

| 阶段 | 内容 | 估时 |
|---|---|---|
| **M0 Spike**（先行） | Windows 手工起 dsh runtime（node 载体）+ cordis.yml（含 pwsh 三件套），脚本验证 initialize→prompt→event→idle 全链路 + 工具实测清单 | 1-2 天 |
| **M1 传输层** | CLFJsonRpcClient（spawn/帧/waiter/订阅/close 阶梯）+ 单测（fake runtime 回放用例，参照 test_client.py） | 2-3 天 |
| **M2 会话层** | CLFHarnessSession run() 状态机 + 事件→ICLFOutput 映射 + 后端抽象接线 + 配置切换 | 2-3 天 |
| **M3 收尾** | runtime 探测/降级、Job Object 杀树、优雅关闭握手、stderr 并入日志、install.ps1 打包 Node 闭包、release.ps1 | 2-3 天 |

**Spike 通过条件**（沿用分析文档）：Windows 下 initialize→prompt→event 流全链路跑通，且 dsh 在 Windows 的可用工具集 ≥ 现有 8 工具（pwsh+fs+todo+subagent 组合预期可满足），或明确净增点（subagent/todo/上下文压缩/多模型）。

## 8. 风险与缓解（新增/修订）

| # | 风险 | 缓解 |
|---|---|---|
| 1 | Windows 无官方单 exe runtime | Node 目录分发（已实现形态）为主线；pkg --sea win32 为备选探索 |
| 2 | Node 闭包体积（150MB 量级） | zip 压缩 + 按需安装（dsh 增强路径非默认）；降级通道保证入口可用 |
| 3 | runtime 卡死导致 idle 永不到达 | C++ 补整轮超时（Python 缺失项） |
| 4 | 工具调用 arguments 流式 JSON 碎片 | 传输层缓冲拼接后整体解析（nlohmann::json） |
| 5 | 子代理事件串台 | 按 sessionId 过滤 + parent→child 亲缘链（Python 同款逻辑） |
| 6 | 双确认链冲突 | dsh 后端时客户端确认透传，以 dsh policy 为准 |
| 7 | 协议 breaking | 传输层隔离 + 钉住版本 + 版本更新半自动（决策 3） |

## 9. 相关文件

- 方案草案：`draft-cordis-win64.yml`（Windows cordis 组合）
- 蓝本报告：`分析/dsh-python-sdk-jsonrpc-蓝本报告.md`（C++ 传输层实现直接依据）
- 前置分析：`分析/分析-dsh终端客户端接入.md`（决策 1/2/3 出处）
- 上游参考：`packages/sdk/protocol/src/transport.ts`、`packages/sdk/server/src/{server,index}.ts`、`python/sdk/src/deepseek_harness/{client,api}.py`、`examples/jsonrpc-agent/tests/snapshots/`
