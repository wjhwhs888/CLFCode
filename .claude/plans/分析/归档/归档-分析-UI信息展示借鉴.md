# 问题-借鉴 dsh Web UI 的信息展示设计

## 背景与原则

- **自闭环不破**：自研 agent 始终是完整功能体；本次借鉴仅限**设计语言**，零运行时依赖、不引 dsh 代码
- 咱们终端显示存在白噪声（信息过剩与信息不足并存）；dsh 是 DS 团队的展示设计沉淀，值得系统对照
- 排序：UI 借鉴先行于后端接入（零风险、立竿见影）

## dsh 展示设计全景（2026-08-14 已核实，证据见前端代码）

1. **消息流**：用户右对齐气泡+尾部元信息行（`HH:mm · Ran for 15s · TTFT 1.2s · 34 tok/s`，同日仅时间）；助手为无气泡 markdown 流；无"折叠旧内容"按钮，靠分页 + 压缩标记行（默认折叠，显示 shadowed items/tokens 计数）。证据：`packages/client/ui-conversation/src/client/chat/MessageItem.tsx`、`CompactionItem.tsx`
2. **工具调用**：统一单行 `[icon] Title · summary`，**默认折叠、整行点击展开**（IN/OUT 卡片）；**执行中保持同一行，仅加 running sweep 扫光动画**；错误行折叠摘要=错误首行。摘要=工具分类名+精选字段首行（相对 cwd 路径）。证据：`ui-tool/src/client/tool/components/ToolRow.tsx`
3. **思考**：默认折叠"Think"行+chevron；折叠摘要 running 时取末行、完成取首行。`ReasoningRow.tsx`
4. **状态指示**：StateDot 四态（done 绿 / warning 琥珀 / error 红 / ongoing 蓝像素追逐动画）；运行中 turn 顶部计时（≥15s 才显示）。`ui-primitives/src/StateDot.tsx`
5. **统计条**：管道分隔 StatsLine（turns/steps | llm/tool 耗时 | TTFT/tok/s | cache hit% | 输入/输出 tokens），分组无数据整体消失；ContextMeter 环形 token 占用仪表（system/tools/messages 分段）。`chat/StatsLine.tsx`、`skeleton/ContextMeter.tsx`
6. **错误与截断**：turn 级错误=红点+title+message+code 行；长输出统一 **head/tail 各 16 行**+"… 其余 N 行"展开按钮（TerminalBlock/ReadBlock/DiffBlock 共用）。`ui-primitives/src/head-tail-cap.ts`
7. **diff**：每文件加粗 path 头，多 hunk `⋯` 分隔；`- ` 红 / `+ ` 绿，符号前缀保证无色可读；footer `└ +A -R · N file(s)`；不软换行。`ui-primitives/src/DiffBlock.tsx`
8. **确认**：琥珀 strip "Waiting for approval"+理由 headline+命令 muted 代码+右对齐 refuse/allow（one-shot 锁防重复）；计划审批卡 / 风险确认 modal 两变体
9. **密度控制通用模式**：DisclosureRow 单行折叠原语（icon→chevron hover 预览，展开保留摘要）；所有卡片默认折叠；时钟 15s 后才出现；错误首行即摘要
10. **设计色语义**：success=green-500 `#22c55e`、error=red-600 `#ec1313`、warn=amber-500 `#f59e0b`、**进行中/业务蓝=deepseek-500 `#4176e6`**；文字三级 label-primary/secondary/tertiary；diff 复用状态色；动效 sweep 2.6s / 折叠 150ms

## 咱们现状对照（2026-08-14 盘点）

- 显示合同 ICLFOutput 六通道（内容/状态/确认/中断/进度块/思考分离）结构合理，不需动
- 已有降噪：工具完成折叠 ✓ 单行、emitError 标红、diff +绿/-红/灰、Ctrl+T 思考独立通道、双计时器、安全模式分色
- **白噪声源已定位**：① 工具执行中显示细节（dsh 执行中仅一行+动画）② "⏹ 已中断"在 CLFAgentLoop 12 处内联 ③ 会话恢复全量回显+分隔线无折叠 ④ 缺时间戳/统计（信息不足也是噪声）

## 可迁移清单（按优先级）

### P0 小改立见效

| 借鉴项 | FTXUI 落地 |
|---|---|
| 错误首行即折叠摘要（错误色常显） | ✗ 行摘要截断逻辑补"取首行"，色一致用 error 红 |
| head/tail 截断 | search_content 500 行截断改为头尾各 N 行+"… 其余 M 行"，diff 超限同策略 |
| diff footer 计数 + 多 hunk `⋯` 分隔 | diff 渲染补 `└ +A -R · N file(s)` 汇总行 |
| 工具执行中最小化 | 执行中进度块只显示"工具名+目标"一行+行尾动画字符（⠋⠙⠹…），细节挪到完成后展开 |

### P1 改造级

| 借鉴项 | FTXUI 落地 |
|---|---|
| 四态状态点 | 状态栏 Working→Cooked 升级为着色点语义：绿 done / 琥珀 warn / 红 error / **蓝 running**（点+动画字符）；≥15s 才显示时钟 |
| 状态色语义统一 | **冲突要解**：dsh 蓝=进行中，咱们蓝=analyze 模式。建议：模式色保留灰=manual/橙=edit，进行中蓝收编给 running，analyze 改色（紫/青） |
| StatsLine 统计条 | turn 结束时输出一行：`N turns · M steps · 耗时 Xs · TTFT Xs · 输入/输出 token`；无数据分组整体省略 |
| 思考单行折叠 | Ctrl+T 整块折叠升级为"Think 单行+展开"，折叠摘要=首行/末行 |
| 审批卡强化 | Zone 5 确认栏补：理由 headline + 命令内容 muted + 确认后锁定防重复提交 |

### P2 长期

| 借鉴项 | 说明 |
|---|---|
| 恢复回显降噪 | /resume 恢复时不全量回显，改为"已恢复 N 条历史消息"折叠行，展开查看 |
| ContextMeter | token 占用仪表（咱们有 30% 预算保护，缺可视化；环形→终端条形分段） |
| 消息级时间戳 | 每条消息尾部 `HH:mm`（同日省略日期，跨日补） |
| 压缩标记行 | 会话压缩后显示 shadowed items/tokens 计数的折叠行（接 CLFSessionSummarizer） |

## 咱们已有优势（不动的部分）

五区固定布局、Ctrl+T 思考独立通道、diff 着色（LCS+超限保护）、安全模式分色、渐进式进度块+双计时器——这些不比 dsh 差，借鉴是**补细节**不是推翻。

## 与 dsh 接入分析的关系

- 本文与《分析-dsh终端客户端接入》相互独立：UI 借鉴不依赖接入，先行实施
- 若将来 dsh 后端接入，本文的展示设计（单行折叠/状态点/统计条）即为 `session.event` 事件→CLFUI 的渲染映射蓝本（dsh 事件流与本文观察的 UI 是同一套词汇）

## 涉及文件（若立项，为设计阶段范围）

- `src/CLFUI/CLFTerminal.cpp`（截断/统计条/状态点/时间戳）
- `src/CLFCore/CLFAgentLoop.cpp`（中断标记收敛、恢复回显折叠）
- `src/CLFCore/CLFToolExecutor.cpp`（工具摘要/错误首行/执行中最小化）
- `src/CLFUI/CLFConfirmBar.cpp`（审批卡强化）
