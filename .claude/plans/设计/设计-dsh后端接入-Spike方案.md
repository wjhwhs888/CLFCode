# 设计-dsh 后端接入 Spike 方案（M0）

> 配套（dsh 参考目录，由 dsh Web 会话 agent 产出，仅作参考）：`dsh/设计-dsh后端接入-实现方案.md`（M0-M3 总方案）、`../分析/分析-dsh终端客户端接入.md`（背景与决策）、`../分析/dsh/dsh-python-sdk-jsonrpc-蓝本报告.md`（C++ 对译依据）

## 结论先行

Windows 现场已具备全部执行条件（dsh 克隆位于钉住提交、node_modules 已装、bin 已构建、Node 24 达标）。唯一缺口：**Python 不可用**（WindowsApps 存根）→ 驱动脚本改用 **Node 直写 stdio JSON-RPC**（~150 行），这恰好与 M1 C++ 客户端要实现的 wire protocol 同构，脚本本身即对译模板。

Spike 分 S0-S5 六步，时间盒 1-2 天。**subagent 专项（S3）为独立验收项**，与已定 UI 合同对接（只显示不展开）。

## 现场状态（2026-08-16 已核实）

| 项 | 状态 |
|---|---|
| dsh 克隆 `F:\wjh_work\deepseek-harness` | HEAD = `47f9438`（= 钉住版本 v0.1.0-rc.5），树干净 ✓ |
| node_modules | 已安装 ✓ |
| Node | v24.19.0 ✓（要求 ≥22.19） |
| `dsh-jsonrpc-agent` bin | `packages/examples/jsonrpc-demo/lib/bin.js`，lib 已构建，无需编译 ✓ |
| Python | ✗ 不可执行 → Python SDK（`python/sdk/`）与 `minimal.py` 仅作参考蓝本 |
| 凭据 | `config/agent_settings.local.json`（`connection.api_key` / `base_url`）→ env 注入，**脚本不落盘凭据** |
| cordis 草案 | `dsh/draft-cordis-win64.yml`（dsh 参考目录，包名已核实，未实测运行） |
| 官方示例 | `examples/jsonrpc-agent/cordis.yml`（含 6 处 `!!js` 表达式 + subagent 三件套，可作回退对照） |

## 本次新核实的协议事实（补充进方案）

1. **bin 启动约束**（jsonrpc-demo README 核实）：配置发现顺序 = `$DSH_CORDIS_CONFIG` → 位置参数 `argv[2]`；两者皆无 → stderr 一行 usage 后 exit(1)；**stdout 只承载 JSON-RPC 帧**；stdin EOF 立即销毁根上下文（切断 in-flight turn），须协议级 `shutdown`
2. **llm-deepseek 配置**（`packages/llm/llm-deepseek/src/index.ts` 核实）：`apiKeyEnv` 默认 `DEEPSEEK_API_KEY`；`baseURL` 回退链 `$DEEPSEEK_BASE_URL` → 公共 API；默认模型目录 deepseek-v4-flash / deepseek-v4-pro；**模型由客户端 initialize 传入**（`{cwd, provider, model, maxTokens?}`，provider = `deepseek-official`）
3. `!!js` 表达式被官方示例自身使用（6 处）→ 草案可变配置写法风险降级，但仍需 S0 实测确认外部配置加载路径的行为
4. **⚠ 草案 cordis 装配缺陷（深检发现，草案原样必启动失败）**：`dsh-pwsh-sandbox` 源码声明"无自身 Config"——`mode`/`workspaceRoot` 属于 `ctx.sandboxPolicy`，且其 `inject = ['subprocess', 'sandbox', 'sandboxPolicy']`。草案 `mode: read-only` 挂在 pwsh-sandbox 下是错位字段，且缺 `dsh-sandbox-local`（Windows = ACL restricted-token runner）+ `dsh-sandbox-policy` 两个装配件 → 插件注入失败 → dsh-app-boot 致命退出。**修正**：咱们的冒烟版/定稿 cordis 增加 sandbox-local + sandbox-policy（`mode: read-only` + `workspaceRoot`；schema 已核实 mode ∈ {read-only, workspace-write, danger-full-access}，默认 read-only 兜底），pwsh-sandbox 只留 local 配置（cwd/timeoutMs）。草案缺陷记录在案（dsh 参考目录不动，修正落在咱们自己的 cordis）

## 目标与通过标准

- **P1**：Windows 下 initialize → session/prompt → session.event 流 → session.status:idle 全链路跑通
- **P2**：dsh Windows 工具集 ≥ 咱们现有 8 工具，或明确净增点（subagent / todo / 上下文压缩 / 多模型）
- **P3（subagent 专项）**：生命周期通知 + 子会话事件隔离实测通过（见 S3）
- **P4**：产出协议回放素材（M1 单测的 fake runtime 输入）

## 步骤

### S0 启动冒烟（0.5h）

职责缩窄为**纯冒烟**：不做协议往返（initialize 等移入 S1，脚本第一条），只验证"进程能起、配置能加载"。

- 冒烟用配置：**零 `!!js` 冒烟版** `spike/cordis-smoke.yml`——硬编码 DSH_CWD/DSH_SESSION_ROOT **与 sandbox-policy 的 workspaceRoot**（源码核实：workspaceRoot 无 schema 默认，缺省回退 bin 的 process.cwd()=启动目录而非预期 workspace，必须显式写）；**含深检修正的沙箱装配**（新增 `dsh-sandbox-local` + `dsh-sandbox-policy`（mode: read-only + workspaceRoot），`dsh-pwsh-sandbox` 去掉 mode 字段只留 local 配置）；其余同草案。先证协议链路，再证配置机制（`!!js` 版定稿留到 S1 跑通后实测）
- 冒烟清单：
  1. 进程存活 + stderr 无 fatal：`node packages/examples/jsonrpc-demo/lib/bin.js <cordis-smoke.yml 绝对路径>` + env（DSH_CWD、DEEPSEEK_API_KEY、DEEPSEEK_BASE_URL、DSH_SESSION_ROOT）
  2. 缺配置：无 DSH_CORDIS_CONFIG 且无位置参数 → stderr usage + exit(1)
  3. 畸形行容忍：`printf 'not-json\n' | node ... bin.js <配置>`（one-shot pipe，EOF 紧随可接受）→ 静默跳过不崩，进程正常退出
- 回退链：smoke 版加载失败 → 官方示例对照（注意其含 `dsh-bash-local`，Windows 下可能加载失败，仅用于定位加载器问题）→ 单点替换 bash→pwsh 后重试

### S1 帧驱动脚本（~0.5 天）

新增 `.claude/plans/测试/spike/spike_driver.mjs`（Node，无第三方依赖）。**按 M1 类骨架分五模块写**（"脚本即对译模板"——每个函数头部注释标注对应 M1 类方法，M1 逐函数对译，不做平铺脚本）：

| spike 模块 | 职责 | M1 对译目标 |
|---|---|---|
| `spawnAndEnv()` | bin + 位置参数配置路径；env 注入（DSH_CWD、DEEPSEEK_API_KEY、DEEPSEEK_BASE_URL、DSH_SESSION_ROOT，凭据从 `agent_settings.local.json` 读取不打印） | `CLFJsonRpcClient::spawn` |
| `lineReader()` | 按 `\n` 切帧、跳过空行与非 JSON 行（Node 启动可能打警告行） | `CLFJsonRpcClient` reader 线程 |
| `routeFrame()` | 三分类：id+method = 桥接请求（respond 空结果）；仅 id = 响应（按 id 弹 waiter，迟到响应丢弃）；仅 method = 通知（fan-out 记录） | `CLFJsonRpcClient::handleLine` |
| `runTurn()` | 轮次循环（照抄双客户端语义）：initialize（**首条，验证握手 + serverInfo**）→ session/prompt → receipt 门控（`agent/inbox/spliced` 含 messageId 才起收）→ 本会话 `session.status: idle` 判定结束 → shutdown → 等回包 → 关 stdin → 超时强杀。**两个超时（Python 缺失，必须自补）**：整轮 idle 等待（默认 10 min，可参数化）；shutdown 后 5s 未退 → 强杀 | `CLFHarnessSession::run` |
| `normalizeFrames()` | 归一化（见下） | M1 测试工具（fixture 生成） |

**frames 双轨落盘** `.claude/plans/测试/spike/frames/`：

- 原始帧：全部帧逐行原样存 `raw/*.jsonl`（协议证据，M1 联调参照）
- 归一化副本 `norm/*.jsonl`：**占位符命名自定、字段宁多勿少**——官方两处快照约定已核实不一致（`scripts/snapshots/python-sdk-single-exe/` 用 `{{parent}}`/`{{messageId}}`；`examples/jsonrpc-agent/tests/snapshots/` 用 `{{sessionId}}`/`{{cwd}}`/`{{system}}`/`{{tools}}`），M1 断言由咱们自己消费，不追官方命名；sessionId/messageId/cwd/system/tools 等标识性字段全占位，**父子会话分离**——root 会话 → `{{rootSessionId}}`，子会话按 `subagent.started` 出现序 → `{{childSessionId-N}}`，messageId 同理区分 root 收据与子会话消息；**不可把所有 sessionId 塌成一个占位符**，否则会话树过滤/事件隔离测试（M2 核心语义）没有可用 fixture；time/seq/createdAt 全归零（seq 本就 per-session 从 0 起，归零不影响父子区分）。**M1 回放断言的 fixture 只用归一化副本**（真实模型每跑必变的字段不可进断言）

- 判据：脚本跑通"你好"轮——initialize 握手成功；收到 assistant/chunk 文本增量流 + finish + idle，clean exit；**reasoning 流实测**：记录 reasoning-delta 的数量与形态、block-end 的 reasoning 块结构，与蓝本报告 §4 对照（Ctrl+T `appendThinking` 折叠通道的对接前提）

### S2 工具面实测（~0.5 天）

定向 prompt 诱导，每类工具各一轮。**通用工具 prompt 中性化**（不点名工具名，让模型按工具 schema 自行选择——点名会诱导"照着说"而非真实调用，工具面实测失真）；**subagent 轮例外保留点名**（S3 是专项触发测试，不点名可能整轮不触发，P3 无法验收）：

| 目标工具 | 诱导 prompt |
|---|---|
| fs | "读取 <测试文件> 前 20 行并报告内容" |
| pwsh | "执行命令 Get-Location 并报告当前目录" |
| todo | "为 spike 验证建一个 3 项任务列表" |
| subagent | "派一个子代理总结 <测试文件> 的要点并回报" |

- 产出：**三维对照表**——`工具名 / 被模型选择 / 沙箱下实际可用`（"存在"与"可用"分开：草案 pwsh-sandbox 为 read-only，写操作会被阻断，单独标注）；记录 `tool/call` 实际 toolName 值（与草案包名映射核对）；未被模型主动选择的工具，追加一轮定向点名补测（补测结果单列，不进自发选择统计）
- **沙箱阻断专项**（直接喂决策点 3"四模式 vs dsh policy 职责切分"）：read-only 下写操作被拒实测——**fs 写与 pwsh 写各一次**（read-only 由 sandbox-policy 提供、强制执行 pwsh 通道；fs 通道是否受限未知，须实测分清）：
  - **fs 写诱导 prompt 设计**："创建一个新文件并写入内容"（不是读测试——read-only 下读能过，测不出通道独立性）。**两种结果都记录**：被拒 → fs 通道受沙箱管辖；直接成功 → fs-local 独立于 pwsh-sandbox。这本身就是决策点 3 需要的实测数据
  - **拒绝形态 + 升级路径记录**：① 拒绝标记形态（源码预核实为工具结果内联 `[sandbox: file access denied under <mode> mode]` + `sandbox.denied/enforcement` 字段，read-only 下 pwsh 跑 ConstrainedLanguage——实测对照）；② 若模型带 `sandbox_permissions` + `justification` 升级重试，记录**审批提示在协议里的形态**——这就是待澄清认知问题 #5（dsh 确认请求形态，咱们 Zone 5 确认栏如何对接）的实测答案。注：源码无 `[sandbox: escalation available]` 字面串，升级是参数重试 + 审批提示，非拒绝消息内嵌提示
  - **pwsh 轮 = windows-acl runner 链首次实测**（源码核实：sandbox-local 的 `selectRunner` 懒裁决——首次约束执行才解析，Windows 单候选链**不预 probe**，执行期失败 fail-closed 抛 `SANDBOX_UNAVAILABLE`）。S0 只跑 initialize 撞不到它；S2 pwsh 轮若整体挂掉，**先查 runner 链（受限 token 创建 / ACL）而非工具配置**——三维对照表如实记录此结果

### S3 subagent 专项（~0.5 天）

在 S2 的 subagent 轮上断言：

1. `subagent.started`（parentSessionId / childSessionId）与 `subagent.finished`（status / stopReason / lastAssistantMessage）均收到
2. 子会话事件（childSessionId）只进 notifications，root events 无污染（对照 Python SDK `test_session_run_collects_nested_subagent_tree_without_polluting_root_events` 语义）
3. child→parent 亲缘链可维护（M2 会话树过滤的前置验证）
4. root 侧 tool/call + tool/result 完整 → 可映射咱们 UI 的 `showProgress` / `finishProgress` 通道（已定 UI 合同：只显示不展开）

附加记录：subagent 单次调用时长（供 UI ≥15s 计时显示与整轮超时参数取值）。

### S4 生命周期与异常（~0.5h）

- shutdown 阶梯：shutdown → 回包 → 进程退出（对照 bin README exit 语义）
- stdin EOF 行为：in-flight turn 被切（预期与文档一致，记录实际现象）
- 缺 DSH_CORDIS_CONFIG → exit(1) + stderr usage
- 畸形帧容忍：发一行非 JSON → 静默跳过不崩

### S5 产出与决策（~0.5h）

- spike 报告：全链路结论 + 工具对照表 + 与蓝本报告的出入清单
- `.claude/plans/测试/spike/frames/` 归档为 M1 fake runtime 回放素材
- **产出咱们自己的 cordis 定稿**（新文件，放 `.claude/plans/测试/spike/` 或后续归入 `config/`；**不动 dsh 参考目录**）
- 回填分析文档"待澄清认知问题"（#1 工具面、#4 会话映射、#5 生命周期等）
- 立项决策：按 P1-P4 给出 go / no-go

## 风险与回退

| 风险 | 回退 |
|---|---|
| `!!js` 表达式加载失败 | S0/S1 用零表达式冒烟版（已消解大部分）；官方示例自身以外部配置 + 6 处 `!!js` 跑通（同一加载路径），机制风险不高；草案实测时对照定位 |
| 官方示例含 bash-local（POSIX-only） | 仅用于协议冒烟对照；Windows 工具面以草案为准 |
| 网络 / 模型波动 | 默认 deepseek-v4-pro（initialize 传入），可切 flash |
| subagent 轮模型不调用工具 | 强化 prompt（明确"必须使用 subagent 工具"）；重试 2 次 |
| spike 整体不满意 | 决策 2 降级通道兜底；方案 0（自建）重新评估 |

## 涉及文件

- `F:\wjh_work\deepseek-harness`：只读使用，**不改源码**（决策 1）
- 新增：`.claude/plans/测试/spike/`（spike_driver.mjs + cordis-smoke.yml + frames/（raw + norm）+ cordis 定稿 + spike 报告）
- 修订：分析文档回填（`.claude/plans/分析/分析-dsh终端客户端接入.md`）
- **不涉及 src/**（spike 不动 C++ 代码）；**不动 `.claude/plans/设计/dsh/`、`.claude/plans/分析/dsh/` 参考目录**

## 后续衔接

- 通过 → M1 传输层（CLFJsonRpcClient），frames 归一化副本直接转单测回放用例
- 不通过 / 部分通过 → 按"出入清单"逐项评估是协议理解偏差还是 dsh 缺陷，回填设计文档

## 附录：外部评审意见处置（2026-08-16）

dsh 会话（flash 模型）对方案的 5 条分析意见，逐条核实后裁决：

| # | 意见 | 裁决 | 说明 |
|---|---|---|---|
| A | S0 缩窄为纯冒烟，initialize 往返移入 S1 | ✅ 采纳 | PowerShell 手工写 stdin 帧确属别扭；S1 脚本第一条即 initialize，职责更清晰 |
| B | 诱导 prompt 不点名工具名，防模型"照着说" | ✅ 采纳（修正） | 通用工具中性化；**subagent 轮保留点名**——S3 是专项触发测试，不点名可能不触发导致 P3 无法验收 |
| C | 补 reasoning 流实测（Ctrl+T 折叠通道对接前提） | ✅ 采纳 | 真实缺口；分析文档 #7 仅文档级结论，spike 需实测闭环。已加入 S1 判据 |
| D | frames 按官方 snapshots 约定归一化（占位符 + seq/time 归零） | ✅ 采纳（修正为双轨） | 官方 `{{parent}}`/`{{cwd}}`/`{{messageId}}`/`{{system}}` + `seq:0`/`time:0` 约定已核实属实；改为**原始帧保留 + 归一化副本进断言**，不覆盖原始证据 |
| E | 先出零 `!!js` 冒烟版 cordis，跑通后再测草案 | ✅ 采纳（修正认知） | "受控环境"论据不成立——官方示例本身即外部配置 + 6 处 `!!js` 的已验证路径；但零表达式版作为排障顺序（先协议链路后配置机制）仍值得做 |

### 第二批意见（同日补充）

| # | 意见 | 裁决 | 说明 |
|---|---|---|---|
| 1 | S2 对照表区分"工具存在" vs "沙箱下可用"，并记录 read-only 下写被拒的实际响应形态 | ✅ 采纳（修正） | 对照表改三维（工具名 / 被模型选择 / 沙箱下实际可用）。修正：read-only 挂 **pwsh 通道**，fs 通道是否受限未知 → fs 写与 pwsh 写各测一次。拒绝形态已从 tool-pwsh 源码预核实（内联 `[sandbox: file access denied under <mode> mode]` + denied/enforcement 字段，read-only 下 ConstrainedLanguage），spike 实测对照；产出喂决策点 3 |
| 2 | 归一化占位符以自己 frames 真实字段为准，宁多勿少、命名自定 | ✅ 采纳 | 核实属实：官方两处快照约定不一致（python-sdk 用 `{{parent}}`/`{{messageId}}`，example tests 用 `{{sessionId}}`/`{{cwd}}`/`{{system}}`/`{{tools}}`）→ 不追官方命名，标识性字段全占位 + time/seq/createdAt 归零 |
| 3 | spike 脚本按 M1 类骨架分模块写，逐函数对译 | ✅ 采纳 | "脚本即对译模板"是方案最大价值；改五模块（spawnAndEnv / lineReader / routeFrame / runTurn / normalizeFrames）并附 M1 对译映射表，每函数头部注释标注对应类方法 |

### 第三批意见（轻量提醒，不阻塞）

| # | 意见 | 裁决 | 说明 |
|---|---|---|---|
| 1 | 沙箱阻断专项加断言：记录拒绝时是否带升级提示 | ✅ 采纳（修正） | 源码核实：**无 `[sandbox: escalation available]` 字面串**——真实机制是拒绝标记 + `sandbox_permissions`/`justification` 参数重试 + 审批提示。改为记录拒绝形态 + 升级重试触发的审批提示协议形态（= 待澄清 #5 实测答案，价值大于原文） |
| 2 | fs 写诱导 prompt 设计为"创建新文件并写入内容" | ✅ 采纳 | 读测试在 read-only 下能过，测不出通道独立性；改为写测试且两种结果（被拒 / 成功）都记录，直接是决策点 3 的实测数据 |

### 深检自查（2026-08-16 开工前，Claude Code 复核）

| # | 深检项 | 结论 |
|---|---|---|
| 1 | **草案 cordis 装配缺陷**：pwsh-sandbox 源码声明"无自身 Config"（mode/workspaceRoot 属 `ctx.sandboxPolicy`），`inject = ['subprocess','sandbox','sandboxPolicy']`，草案 `mode: read-only` 错位且缺 sandbox-local + sandbox-policy 两件 | ✗ 缺陷坐实：草案原样必启动失败。修正已写入冒烟版/定稿 cordis 规格（见协议事实 #4） |
| 2 | initialize 签名逐字段核实（client.py L117-135） | ✓ 与方案一致：`{cwd(绝对路径), provider, model, maxTokens?}` → `{serverInfo}` |
| 3 | session/prompt 签名 + 文本块形态（client.py L138-156、api.py normalize_input） | ✓ 与方案一致：`{sessionId, contentBlocks}` → `{messageId}`；文本块 = `{"type":"text","text":...}` |
| 4 | 草案 `toolBash: false` 字段合法性（agent-spine-demo schema） | ✓ `z.union([z.const(false), toolBash.Config])`，源码注释明确"Set `toolBash: false` when another plugin owns the model-facing bash tool" |
| 5 | 草案 tool-pwsh `enableRunInBackground: false` 合法性 | ✓ `z.boolean().default(true)` |
| 6 | 沙箱装配正确形态（sandbox-policy schema + sandbox-local 平台分发） | ✓ mode ∈ {read-only, workspace-write, danger-full-access}（默认 read-only 兜底）+ workspaceRoot；Windows = ACL restricted-token runner |
| 7 | 升级审批链归属 | ✓ pwsh-sandbox 源码："The tool layer owns the escalation approval flow through `ctx.approval`"——与第三批 #1 修正一致 |
| 8 | 官方 minimal 配置对照 | ✓ 印证装配形态（sandbox-local + sandbox-policy + fs-local + spine toolBash:false 同型）；其 sessions compression: none 提示冒烟版可用 none 便于排障 |

### 第四批意见（开工前补充提醒，不阻塞）

| # | 意见 | 裁决 | 说明 |
|---|---|---|---|
| 1 | 冒烟版 workspaceRoot 显式硬编码 | ✅ 采纳 | 源码核实：`workspaceRoot` 无 schema 默认，缺省回退 `process.cwd()`（bin 启动目录）——不是预期 workspace，已写入冒烟版规格 |
| 2 | S2 pwsh 轮 = windows-acl runner 链首次实测 | ✅ 采纳（机制修正） | 源码核实：`selectRunner` 懒裁决、Windows 单候选链不预 probe、执行期 fail-closed（`SANDBOX_UNAVAILABLE`）；已写入专项 triage 注——pwsh 全挂先查 runner 链（受限 token/ACL）而非工具配置 |
| 3 | 归一化占位符父子会话分离 | ✅ 采纳 | 逻辑正确：child sessionId 独立、seq per-session 归零；已定 `{{rootSessionId}}` / `{{childSessionId-N}}` 分离规格，保住 M2 会话树过滤/事件隔离测试的 fixture 结构 |
