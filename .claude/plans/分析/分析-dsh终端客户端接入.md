# 问题-CLFCode 终端作为 deepseek-harness 客户端接入

## 背景与目标

deepseek-harness（`dsh`，DeepSeek AI 开源）是"一切皆插件"的 agent 框架（TS/pnpm monorepo，约 200 包）。咱们的 FTXUI 终端是其缺位的内置形态之一（它内置 web/headless 两个 profile，无终端 REPL）。

目标：评估**把 CLFCode 终端作为 dsh 的客户端接入**的可行性——即终端 UI 驱动 dsh runtime，获得它的多模型/沙箱/subagent/MCP 工具能力，同时保留咱们的终端体验。

## 结论先行

**可行，且 dsh 官方已为此场景铺好协议面**（进程外 stdio JSON-RPC，Python SDK 即此接法）。但存在三个实质风险：① dsh 是 developer preview，协议会 breaking；② 部署形态从"单二进制"变为"携带 Node runtime 闭包"；③ Windows 工具面受限（PTY/Bash 类 POSIX-only）。建议**先做 1-2 天 spike 验证**再决定投入，采用"双后端增量接入"而非"纯前端化"。

讨论已确认三项技术决策（见"已确认的决策"）：**git 版本钉住**管理上游（源码不入咱们仓库）、**终端始终为入口**（runtime 缺失自动降级直连模式）、**半自动主手动**的版本更新策略。

2026-08-14 补充：dsh Web 会话中的 agent 独立产出了实现方案与蓝本报告（见"相关产出"），经抽查核实，其纠正了咱们一处关键假设——**官方单 exe runtime 明确不含 Windows**（platforms.json 仅 linux/macos，"Windows is a non-goal"），部署主线改为 Node runtime 目录分发。

## 已核实的技术事实（2026-08-14 仓库代码）

### 1. 协议面：stdio JSON-RPC（官方"进程外客户端"通道）

`dsh-sdk-jsonrpc-server` 插件服务进程外客户端：子进程 stdio 上跑 newline-delimited JSON-RPC 2.0（每行一个 JSON 帧），stdout 只承载协议帧，诊断走 stderr。

| 方向 | 方法 | 说明 |
|---|---|---|
| 客户端→服务端 | `initialize` | 握手（可选 maxTokens 输出上限） |
| 客户端→服务端 | `session/prompt` | 投递用户消息（contentBlocks），立即返回 messageId |
| 客户端→服务端 | `shutdown` | 协议级优雅关闭（先回响应再退出） |
| 服务端→客户端 | `session.event` | 流式推送每条持久化事实（文本增量/工具执行），无过滤 |
| 服务端→客户端 | `session.status` | agent 整体 running/idle 转换 |
| 服务端→客户端 | `subagent.started/finished` | 子代理生命周期（仅 in-process） |

`session.event` 是"每产生一条事实推一条"的流——与咱们 FTXUI 渐进式工具显示、SSE 流式渲染**天然同构**。

**协议实现细节**（dsh agent 代码侦察补充，已对照 transport.ts / client.py 核实）：

- 帧 = 一行紧凑 JSON + `\n`，行内换行被转义，**按 0x0A 切帧安全**；读端跳过空行与非 JSON 行（Node exe 启动可能往 stdout 打警告行）
- 帧三分类：有 id+method = 对端请求（预留 respond，路由不能丢帧）；有 id 无 method = 响应（按 id 弹 waiter，迟到响应丢弃）；无 id 有 method = 通知（fan-out）
- 错误码只有 `-32603`（纯文本 message）；畸形帧不回错误
- `initialize` **无版本协商**：serverInfo.name 恒为 `deepseek-harness-sdk-runtime`，客户端校验字段为字符串即可；maxTokens 必须为正安全整数（自校验，否则 -32603）
- `session/prompt` 的 messageId 仅"入队受理凭证"，**无 per-prompt 完成标记**；未知 sessionId 懒创建，已知复用（延续会话状态）
- `session.status: idle` 是单轮结束的**最可靠信号**
- `session.event` 事件 14 种：turn/start·end、step/start·end、user/message、assistant/message、assistant/chunk、tool/call、tool/result、agent/inbox/spliced、session/title、request/header、request/context、todo/write、session/end-seed
- `assistant/chunk` 子类型：block-start(reasoning/text/tool-call)、reasoning-delta、text-delta、tool-call-delta（**流式 JSON 碎片，必须缓冲拼接后整体解析**）、block-end、usage（含 reasoningTokens）、finish
- **Python SDK 缺两个超时，C++ 必须补**：整轮 idle 等待超时（runtime 卡死防挂起）、shutdown 后进程未退的强制 kill 等待

### 2. 运行时形态

- `dsh-jsonrpc-agent` bin：启动外部 `cordis.yml`（`$DSH_CORDIS_CONFIG` 或位置参数指定），协议配置全在 yml 里，bin 自身零模型内容
- **⚠ 修正（2026-08-14）**：官方单 exe runtime（`dsh-jsonrpc-agent-pkg`）**明确不含 Windows**——platforms.json 仅 linux-x64/linux-arm64/macos-arm64，架构笔记标注 "Windows is a non-goal"。Windows 部署主线 = **Node runtime 目录分发**（仓库内已实现的 dev-only node 载体：`runtime/node/` = package.json + node_modules），install.ps1 捆绑 Node ≥22.19 或 node.exe sidecar；备选探索 `@yao-pkg/pkg --sea` win32 target（未验证，属新工程）
- 环境变量：`DSH_CORDIS_CONFIG`（**必需**，缺失 exit(1)）、`DSH_CWD`、`DEEPSEEK_API_KEY`/`DEEPSEEK_BASE_URL`、`DSH_SESSION_ROOT`（可选）、`DSH_RUNTIME_MODE`（exe/node）
- 生命周期：stdin EOF 立即销毁根上下文（**会切断 in-flight turn**），需客户端显式 `shutdown`；崩溃/挂起场景客户端需自己兜底重启

### 3. Windows 支持现状

- 核心可跑：有 `apps/cli/tests/windows-shell.spec.ts`、`sandbox-windows-acl` 包、credentials-local 显式处理 Windows mode 差异
- **受限面**：持久 PTY/Bash 工具明确标注"requires POSIX terminal environment, not a Windows agent interface"；Landlock 沙箱仅 Linux（Windows 走 ACL 后端）
- 即：Windows 上 dsh 工具面 = pwsh 三件套（pwsh-local / pwsh-sandbox ACL / tool-pwsh）+ fs + todo + subagent + compaction 等，**没有 Linux 上那么全**；2026-08-14 已实测：dsh Web 会话在 Windows 完成 23 步 31 次工具调用全部成功

### 4. 生态定位澄清

"插件"与"客户端"是两条不同接缝：React 前端不是 Cordis 插件，是被 host webserver 插件托管的**客户端**。C++ 二进制进不了 Node 进程，咱们的正确姿势同理是**客户端**。若想以 dsh 生态一等公民出现，可再加薄 host 插件负责 spawn 咱们的二进制（类比 `native/landlock-run` 的 native 打包先例）——属包装，非必需。

## 方案对比

### 方案 0：不接入，只借鉴（基线）

维持现状，从 dsh 借概念自建：MCP client、工具注册表接缝化、会话 append-only 日志。

- 收益：零协议风险，单二进制定位不动摇
- 代价：dsh 的沙箱/多模型/subagent/插件生态全部需要自己造
- 适用：咱们长期本来就计划自建这些能力

### 方案 A：双后端增量接入（推荐候选）

保留直连 DeepSeek 的现有后端，新增"dsh 子进程"后端，配置切换。终端 spawn dsh runtime 子进程，走 stdio JSON-RPC；渲染层不动，新增传输层 + 事件映射。

- 收益：零风险增量；dsh 能力随模式获得；现有功能全保留
- 代价：两套后端长期维护；安全模式（咱们四模式）与 dsh sandbox-policy 形成**双确认链**，需理清职责边界
- 风险：协议 breaking 影响面被隔离在传输层

### 方案 B：纯前端化

agent loop 全交 dsh，咱们只留 UI。会话持久化（latest.json + summarizer）迁到 dsh 侧（JSONL/SQLite + compaction）。

- 收益：CLFCode 大幅瘦身
- 代价：`/resume`、`/history`、系统提示模板等已建成资产要重做或放弃；对 dsh 单向依赖，developer preview 期间风险集中
- 适用：若决心把 CLFCode 定位成"dsh 的终端皮肤"

### 方案 C：薄 host 插件包装（A 的生态化延伸，非独立方案）

在 A 之上加一个 TS 插件，负责在 profile 里安装/启动咱们终端二进制。仅当要进 dsh 插件生态（dsh-plugin topic）时值得做。

## 已确认的决策（2026-08-14 讨论结论）

### 决策 1：上游依赖管理 —— git 版本钉住，源码不入咱们仓库

dsh 对咱们是**运行时依赖**（编译期零依赖，运行时 spawn 其 runtime 子进程），源码不需要进 CLFCode 仓库，也不 fork、不改它的源码。管理方式：

- 云端 fork 镜像（GitHub/Gitee）+ 本机独立克隆（dsh 保留自己的 git 仓库与完整历史）
- 咱们仓库仅存钉住文件 `3rdparty/deepseek-harness.pinned`：commit hash + 版本号 + 更新日期
- 更新 = dsh 仓库 `git pull`（上游的人写）→ 咱们跑适配回归 → bump 钉住文件（咱们适配）
- 发布物使用钉住版本构建的 runtime 闭包，与源码解耦
- 当前钉住：`47f943859bef60e4160492346772ded9b24f765a`（v0.1.0-rc.5，2026-08-13）

### 决策 2：启动拓扑 —— 咱们终端始终是入口

```
用户 → CLFCode.exe（入口，终端 UI）
         ├─ 探测 dsh runtime（安装目录 runtime\dsh\ → 配置路径 → PATH）
         ├─ 有 → spawn runtime 子进程（stdio JSON-RPC，initialize 版本握手校验）
         └─ 无/损坏 → 自动降级直连 DeepSeek 模式 + 状态栏提示（不硬报错）
```

- 与 Python SDK 同拓扑（客户端进程 spawn dsh runtime），是官方设计的接法
- 子进程管理复用 CLFCommandExec 已有模式；新增 Windows Job Object 杀树（dsh 会再 spawn pwsh 等工具进程，退出时整棵树回收，`KILL_ON_JOB_CLOSE`）
- 退出握手：Esc Esc → 发 `shutdown` → 等回包 → 超时强杀
- dsh 的 stderr 并入 `doc/log/clf_agent.log`，双进程诊断归一处
- 已接受代价：包体 12M → 预估 50-100M（Node runtime 闭包）；冷启动 +1-2s
- 降级通道使入口永远可用，dsh 只是增强路径——这依赖方案 A 的双后端架构

### 决策 3：版本更新 —— 半自动、主手动

全自动跟进会把上游破坏性变更（developer preview 常态）自动注入发布物；全手动会积累适配债。切分：

| 环节 | 方式 | 说明 |
|---|---|---|
| 检测新版本 | 自动 | 检查脚本/定时任务对比上游 tag 与钉住文件，发现新版本通知 |
| 决定是否更新 | 手动 | 读 release notes，按需排期（有降级通道，更新不是紧急事件） |
| 拉取 + 回归 | 自动 | pull 上游 → 重建 runtime → 跑协议回归套件 → 输出 diff 报告（协议方法/工具清单/事件字段变化） |
| 适配传输层 | 手动 | 按 diff 报告改，唯一需要人脑的环节 |
| 打包发布 | 自动 | release.ps1 按新钉住版本构建 runtime 闭包进 zip |

补充防线：release.ps1 发布前预检——钉住版本落后上游 N 个版本即警告，保证不会在不知情的情况下落后。

## 待澄清的认知问题（讨论中，逐项澄清后回填结论）

### A. 决策级认知

| # | 问题 | 为什么重要 |
|---|---|---|
| 1 | dsh 在 Windows 上的真实能力面（工具清单实测） | spike 前需明确"要验证什么"；投入产出比的核心变量 |
| 2 | 战略风险：dsh 官方路线图是否会做终端 UI | 若官方自做 TUI，咱们作为其客户端的价值需重新评估 |
| 3 | MCP 生态接入后咱们具体多出哪些工具 | "接入"的主要收益来源之一，需算清账 |

### B. 技术认知（协议与机制）

| # | 问题 | 为什么重要 |
|---|---|---|
| 4 | 会话模型映射：dsh 多会话 + 无过滤事件流 → 咱们单会话 UI | /resume /history 的对应关系 |
| 5 | 确认链 UX：dsh ask-user / sandbox-policy 的确认请求在协议里的形态 | 咱们 Zone 5 确认栏如何接它的确认请求 |
| 6 | 沙箱本质区别：应用层拦截（咱们四模式）vs OS 级隔离 | 是否把四模式让渡给 dsh policy，风险表 #4 的前置认知 |
| 7 | reasoning_content 在 dsh 协议里的流式形态 | Ctrl+T 思考折叠能否保留 |

### C. 工程与部署

| # | 问题 | 为什么重要 |
|---|---|---|
| 8 | `dsh-jsonrpc-agent-pkg` 单 exe runtime 的形态 / 体积 / 构建方式 | 决定 install.ps1 打包方案 |
| 9 | MIT 随包附带义务 + THIRD_PARTY_NOTICES 在发布物里落地 | 发布合规 |
| 10 | 回退与卸载路径（spike 不满意 / dsh 转向） | 两套会话格式的用户数据善后 |

建议讨论顺序：先 1、2（可能直接改变结论），再 4、5（协议映射细节）；C 组随 spike 带着问题看代码。

### 已回填结论（2026-08-14）

- **#7 reasoning 流式形态（已解）**：推理以 `blockType:"reasoning"` 独立块流式传输（reasoning-delta），与正文 text 块分开——Ctrl+T 折叠可原样保留；2026-08-16 spike 实测坐实（reasoning-delta 正常流出）
- **#1 Windows 能力面（已解）**：2026-08-16 spike 实测：read/write/pwsh/todo_write/subagent 五类工具全部可用；**fs 通道独立于沙箱（read-only 下写不受限）**；pwsh 受 read-only 约束（ConstrainedLanguage）且写被拒；工具面 ≥ 咱们 8 工具并明确净增（subagent/todo/多模型/上下文压缩装配）。详见《测试/spike/Spike报告.md》
- **#4 会话模型映射（已解）**：14 种事件类型表已到手（见"协议实现细节"）；`agent/inbox/spliced` 为消息队列增删操作——建议渲染最终投影而非队列操作。spike 实测补充：**事件先于 session/prompt 响应**（receipt 门控须缓冲回溯）；sessionId 复用与已落盘日志碰撞（客户端每次新 id，/resume 走 runtime 自身恢复）；每轮双 spliced（首个带消息、次个空插入）
- **#5 确认链 UX（已解）**：spike 实测——当前 cordis 组合**未装配审批服务**，pwsh 写升级重试（sandbox_permissions+justification）直接报 `requires approval, but no approval service is composed`；**确认请求不进入 JSON-RPC 协议**，咱们 Zone 5 确认栏无对接面。若需 dsh 内确认链，须装配审批服务插件并摸清其桥接形态
- **#8 单 exe runtime 形态（已解）**：Windows 是 non-goal，无官方单 exe；主线改 Node 目录分发（见"运行时形态"修正）

## 关键风险与不确定点

| # | 风险 | 说明 | 缓解 |
|---|---|---|---|
| 1 | **developer preview breaking changes** | dsh 官方明示"THERE WILL BE COMPATIBILITY-BREAKING CHANGES"，协议方法表可能变动 | 传输层隔离；spike 时锁定 rc 版本；跟进其 CHANGELOG |
| 2 | **部署形态变化** | 现在 12M 单 exe；官方单 exe 不含 Windows（non-goal），接入后需 Node 22+ runtime 目录分发（预估 150MB 量级，zip 压缩后待实测） | install.ps1 捆绑 Node runtime 闭包；备选 pkg --sea win32 待探索；降级通道保证入口可用 |
| 3 | **Windows 工具面受限** | PTY/Bash/Landlock 均 POSIX-only；Windows 只剩 fs/pwsh/ACL 子集 | spike 时实测 Windows 下可用工具清单，对比咱们现有 8 工具是否有净增 |
| 4 | **安全模型双确认链** | 咱们四模式在客户端拦一道，dsh sandbox-policy 在 runtime 拦一道；策略冲突时用户困惑 | 方案 A 下建议：dsh 后端时客户端确认降级为"透传"，以 dsh policy 为准 |
| 5 | **子进程生命周期** | stdin EOF 切断 in-flight turn；runtime 崩溃/挂起/升级重启 | 复用 CLFCommandExec 子进程管理经验；断线重连 + 会话恢复策略需设计 |
| 6 | **会话持久化归属** | 双后端时两套会话格式并存；跨后端 /resume 语义不清 | 方案 A 按后端分目录存；不承诺跨后端恢复 |

## 工作量粗估

| 阶段 | 内容 | 估时 |
|---|---|---|
| Spike（先行） | 手工起 `dsh-jsonrpc-agent` + cordis.yml（含 jsonrpc-server），用简单脚本/现有 SDK 验证 Windows 下协议往返 + 可用工具清单 | 1-2 天 |
| A 方案实现 | JSON-RPC 行帧传输（nlohmann/json 已有）+ 子进程管理 + 事件→CLFUI 映射 + 配置切换 | 3-5 天 |
| 启动拓扑收尾 | runtime 探测/降级、Job Object 杀树、优雅关闭握手、stderr 并入日志、release.ps1 打包 runtime | 2-3 天 |
| A 方案收尾 | 安全策略职责理清、断线恢复、测试（协议回放用例） | 2-3 天 |

Spike 通过条件：Windows 下 initialize→prompt→event 流全链路跑通，且 dsh 在 Windows 的可用工具集 ≥ 咱们现有 8 工具（或明确净增点）。

> 实现方案已产出：《设计/dsh/设计-dsh后端接入-实现方案.md》（M0-M3 里程碑与上表对齐，协议状态机/关闭阶梯/事件映射规格齐备，待 spike 验证后归档定稿）。

## 待决策点

1. ~~是否引入 Node runtime 依赖~~ → 方向已定：终端入口 + runtime 子进程 + 自动降级通道（决策 2）；**立项与否仍以 spike 结果为准**
2. ~~方案 A / B 选择~~ → 已定：方案 A 双后端增量（决策 2 的降级行为即依赖此架构）
3. 安全模式双确认链的职责切分（客户端四模式 vs dsh sandbox-policy，见风险表 #4）——设计阶段定
4. dsh 工具面在 Windows 的实测清单出来后，评估"接入"与"自建 MCP"的投入产出比——spike 输出

## 相关产出（2026-08-14，dsh Web 会话 agent 独立产出，抽查核实通过，已移至 `dsh/` 子目录与咱们的文档隔离）

- `设计/dsh/设计-dsh后端接入-实现方案.md` — 实现方案（M0-M3，待 spike 验证）
- `设计/dsh/draft-cordis-win64.yml` — Windows cordis 组合草案（包名全部核实存在）
- `分析/dsh/dsh-python-sdk-jsonrpc-蓝本报告.md` — Python SDK 客户端蓝本（C++ 对译依据）

## 涉及文件（若立项，为设计阶段范围）

- `src/CLFNetwork/`（新增 JSON-RPC 传输）或独立 `CLFBackend` 层
- `main.cpp`（组合根注入后端选择）
- `src/CLFUI/`（事件映射适配，预期零改动或小改）
- 发布脚本（install.ps1 / release.ps1 的 runtime 打包）
