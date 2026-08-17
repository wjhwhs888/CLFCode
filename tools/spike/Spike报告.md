# dsh 后端接入 Spike 报告（2026-08-16）

> 配套：方案《.claude/plans/设计/设计-dsh后端接入-Spike方案.md》；本目录即执行代码 + frames 素材所在（自包含）
> 结论：**go** —— 通过标准 P1-P4 全部达成，M1 传输层立项。

## 执行摘要

| 步骤 | 结果 |
|---|---|
| S0 启动冒烟 | ✅ 四项全过（缺配置 exit(1) / 畸形行静默 / 20 插件全树 / 存活 10s stderr 零行） |
| S1 帧驱动脚本 | ✅ 全链路跑通（initialize→prompt→流式文本→reasoning→usage→idle→shutdown→exit 0） |
| S2 工具面实测 | ✅ 5 轮（fs 读 / pwsh / todo / fs 写 / pwsh 写阻断） |
| S3 subagent 专项 | ✅ 4 断言全过 |
| S4 生命周期 | ✅ shutdown 阶梯 8 轮实测 + EOF 切 in-flight turn 坐实 |
| S5 产出 | ✅ cordis 定稿实测通过 + 本报告 + M1 素材 |

环境：dsh 克隆 `F:\wjh_work\deepseek-harness` @ `47f9438`（钉住），Node v24.19.0，模型 deepseek-v4-pro。

## 一、协议事实（M1/M2 实现必读）

### 1.1 与蓝本报告一致（实测确认）

- 帧三分类、空行/非 JSON 行跳过、错误码 -32603 纯文本
- initialize `{cwd, provider, model, maxTokens?}` → `{serverInfo:{name:"deepseek-harness-sdk-runtime", version:"0.0.1"}}`
- session/prompt `{sessionId, contentBlocks:[{type:"text",text}]}` → `{messageId}`
- 14 种事件类型齐全（spliced/turn/step/user/message/session-title/request-header/request-context/assistant-chunk/assistant-message/tool-call/tool-result/step-end/turn-end + session.status）
- assistant/chunk 子类型：block-start(reasoning/text/tool-call)、reasoning-delta、text-delta、block-end、usage（含 reasoningTokens）、finish
- subagent.started/finished 形态与设计文档一致（status=ok、stopReason=completed、lastAssistantMessage 带回）

### 1.2 蓝本未覆盖 / 需修正（实测新增）

1. **事件先于响应**：session/prompt 响应到达前事件已流动，首个 spliced 已带最终 messageId——receipt 门控必须**缓冲 + 响应后回溯过滤**，到达时即判必失败
2. **sessionId 碰撞**：复用已落盘会话 id → `id collision` 错误 → 客户端每次生成新 id；/resume 走 runtime 自身会话恢复
3. **assistant/message 内容块在 `data.message.content`**（非 data.content），含 reasoning 块 + text 块
4. **双 finish 枚举**：chunk finish `reason.kind="stop"` vs turn/end `reason.kind="completed"`
5. **每轮两个 spliced**：首个带 prompt 消息（响应前）、次个 `inserted=[]`（带 `removedCount:1` 的 next-turn 预置）
6. **tool/call 参数是字符串**：`data.arguments` 为序列化 JSON 字符串（非对象）；tool/result 内容块在 `data.message.content[].content[]`
7. **serverInfo.version = "0.0.1"**（呼应 npm 发布版 0.0.1-rc.5，与钉住仓库 0.1.0-rc.5 不一致 → M3 打包核实）
8. **会话 id 形态**：parent 为客户端自定义 id，child 为 runtime 生成 UUID

## 二、Windows 工具面三维对照

| 工具 | 被模型选择 | 沙箱下实际可用 |
|---|---|---|
| read（fs） | ✓ 自发 | ✓ 读不受限 |
| write（fs） | ✓ 自发 | ✓ **写不受限**（fs 通道独立于 pwsh 沙箱） |
| pwsh | ✓ 自发 | 读/执行 ✓（ConstrainedLanguage 下运行）；**写 ✗**（拒绝→升级→无审批服务） |
| todo_write | ✓ 自发 | ✓ |
| subagent | ✓（S3 点名触发） | ✓ 生命周期完整、结果回报 |

净增点 vs 咱们 8 工具：**subagent、todo_write、多模型（provider 参数）、上下文压缩（已装配未触发）**；search 类 dsh 有 fs-search 插件可选装配（本组合未装）。

## 三、沙箱与确认链实测（决策点 3 输入）

- read-only（sandbox-policy）**只约束 pwsh 执行通道**；fs 工具读写均不受限（实测 write + 磁盘确认）
- pwsh 写拒绝两种形态：① ConstrainedLanguage 禁止 .NET 类型创建（`CannotCreateTypeConstrainedLanguage`，包装脚本编码设置行）；② 文件写 UnauthorizedAccessException（DSH 文件策略只读）
- 升级路径：模型自发带 `sandbox_permissions: workspace-write` + `justification` 重试 → **`Error: sandbox escalation to "workspace-write" requires approval, but no approval service is composed`**
- **确认请求不进入 JSON-RPC 协议**（当前组合无审批服务）——待澄清 #5 的实测答案：客户端确认栏无对接面；若需 dsh 内确认链，须装配审批服务插件并摸清其桥接形态（loader 的桥接请求 respond 机制疑似预留位）
- windows-acl runner 链：首次真实执行成功（受限 token + ConstrainedLanguage），无 fail-closed

## 四、草案缺陷清单（dsh Web agent 产出的 draft-cordis-win64.yml）

| # | 缺陷 | 修正（已落 cordis-final.yml） |
|---|---|---|
| 1 | 缺 dsh-sandbox-local + dsh-sandbox-policy 装配件 | 已补 |
| 2 | mode/workspaceRoot 错挂在 pwsh-sandbox（无此字段） | 移到 sandbox-policy |
| 3 | pwsh-local 与 pwsh-sandbox 同注册 ctx.shell 服务 | 只挂 pwsh-sandbox |
| 4 | 缺 dsh-shell-env（tool-pwsh inject 依赖） | 已补 |
| 5 | pwsh-sandbox 不在 examples 链接场（bare 名解析不到） | 相对路径引用 |

另有部署级发现：**bare 包名从配置文件目录向上解析**（"configuration project" 语义）——部署时 cordis.yml 必须与 runtime 闭包（package.json + node_modules）同目录；spike 用 junction + 相对路径解法。

## 五、M1 素材清单

- `tools/spike/spike_driver.mjs`：五模块驱动（spawnAndEnv / lineReader / routeFrame / runTurn / normalizeFrames），逐函数标注 M1 对译目标
- `tools/spike/cordis-smoke.yml`（零表达式冒烟版）+ `cordis-final.yml`（!!js 定稿）
- `tools/spike/frames/raw/*.jsonl`（8 组原始帧）+ `frames/norm/*.jsonl`（归一化 fixture：{{rootSessionId}}/{{childSessionId-N}}/{{messageId-N}}/{{childMessageId-N}}/{{cwd}}/{{system}} + time/seq/createdAt 归零）——M1 单测回放断言只用 norm
- 关键 fixture：`s3-subagent.norm.jsonl`（父子会话隔离断言）、`s2-pwsh-write.jsonl`（沙箱拒绝/升级形态断言）、`s1-hello-final.jsonl`（receipt 门控 + reasoning 流断言）

## 六、风险更新

| 原风险 | 实测状态 |
|---|---|
| 草案 !!js 表达式加载失败 | 消解：零表达式冒烟版跑通 + !!js 定稿实测通过 |
| Windows 工具面受限 | 达预期：5 类工具可用，净增点明确 |
| 沙箱双确认链 | 实测清晰：fs 不受沙箱约束、pwsh 受约束、升级需审批服务（当前不可用）——职责切分有据可依 |
| 协议 breaking（preview） | 钉住版本实测全部落地；frames 素材即回归基线 |
