# 设计-P2-UI展示完善

> 依据：《归档/归档-UI信息展示借鉴.md》P2 清单（恢复回显折叠 / 审批卡强化 / 消息级时间戳 / ContextMeter）
> 跨文档联动：P2-4 的 token 统计 =《设计-整体功能审查与修复.md》P1-3 的实现载体（usage 打通一并设计）
> 原则不变：ICLFOutput 合同最小演进（默认实现零破坏）、每项独立可测可回滚、不新增定时器线程。

## 0. 现状核实（2026-08-14）

| 项 | 现状 | 依据 |
|---|---|---|
| 恢复回显 | `restoreSession` 全量 emitContent 回显 + 分隔线，无折叠 | `CLFAgentLoop.cpp:424-441` |
| 审批卡 | 黄色段落渲染 "⚠ prompt" + 选项行；prompt 已含 headline+参数但无视觉分层；模态阻塞已天然 one-shot | `CLFConfirmBar.cpp:33-38`、`CLFToolExecutor.cpp:417-419/448-449` |
| 时间戳 | 全部消息输出无时间；本地时间 helper 缺双平台实现（整体审查 P2-8 同源） | 全库无 localTimeStamp |
| token | 上下文估计值有（`CLFContext::estimateTokens()`，/context 用）；**API response.usage 从未解析**（全库无 usage 引用）；累计统计无 | `CLFContext.cpp:181` |
| 折叠交互先例 | thinking 通道（Ctrl+T + 独立缓冲 + 折叠行）可复用同模式 | `CLFTerminal` / `CLFRepl.cpp:204-213` |

## 1. 变更项设计

### P2-1 恢复回显折叠（白噪声里用户最有感的一项）

- **合同演进**：ICLFOutput 增 `virtual void showFoldedBlock(const std::string& summary, const std::vector<std::string>& lines) { }` 默认空实现（零破坏）
- **CLFTerminal**：三成员 `m_foldedSummary/m_foldedLines/m_foldedExpanded` + snapshot 扩展；Renderer 滚动区渲染折叠行 `▸ ● 会话已恢复 · N 条消息（ctrl+r 展开）`（dim），展开时在折叠行后渲染全部行
- **AgentLoop::restoreSession**：回显改走 showFoldedBlock（不再直灌 emitContent）；system 恢复/技能重注入逻辑不变
- **数据流决策（R1）**：展开内容**全量传入 CLFTerminal**——messages 本就全部驻留 m_context（restoreSession 已加载），回显行只是其显示投影（二次拷贝）；现状已跳过 tool/system 行，文本量级 ≤ 几百 KB，内存有界可接受。**不做懒加载**（展开时重读会话文件会引入"文件已被后续 turn 更新"的语义漂移，得不偿失）
- **CLFRepl**：Ctrl+R 切换 m_foldedExpanded（复用 Ctrl+T 交互模式）
- **滚动模型（R5 实现注意）**：展开后总行数变化（scrollView.update 参与行数计算，CLFRepl.cpp:215）——toggle 时记录折叠行行号，展开后调 CLFScrollView 保持折叠行可见（按实际 API 定，防顶出视口）；"展开后折叠行仍可见"入**人工验收清单**（渲染层无单测）
- 规格：折叠行常显 dim；展开/收起不残留历史

### P2-2 审批卡强化

- **CLFConfirmBar::render**：prompt 按首个 `\n` 拆分——首行 headline 琥珀加粗（Yellow + bold），其余行 dim 段落（参数全文保留可见但降噪）
- **ToolExecutor**：prompt 构造已含 headline/参数两层（:417-419/:448-449），仅渲染分层，发射端不动
- **防残影**：`CLFTerminal::confirm` 退出时清 m_confirmPrompt
- 规格：headline 琥珀、参数 dim、one-shot 锁（模态）语义不变

### P2-3 消息级时间戳

- **helper**：CLFTypes 增内联 `localTimeStamp()`——`system_clock::now()` → `localtime_s`（Windows）/ `localtime_r`（POSIX），输出 `HH:mm`；**顺带修复整体审查 P2-8（get_current_time 仅 Windows）**，`CLFTools` 的 get_current_time 改用此 helper
- **发射点（R2 修订：仅用户消息行）**：`CLFRepl::submit` 用户回显行尾追加 dim 时间戳。**不加 ✻ 行**——✻ 行由 AgentLoop 发射且进入模型上下文，跨层时间戳不成立；✻ 行已有时长信息
- **跨日状态（R2）**：`m_lastTsDate`（"YYYY-MM-DD"）放 **CLFRepl**（发射点唯一，状态与发射点同居）；每次发射比较，日期变化时输出 `MM-DD HH:mm` 并更新，同日输出 `HH:mm`
- 规格：时间 dim 灰色，不打断阅读流

### P2-4 token 统计 + ContextMeter（= 整体审查 P1-3 落地）

- **usage 打通（R3 修订：落定时机）**：
  - `CLFProtocolAdapter`/`CLFStreamAccumulator`：解析 response `usage`（prompt_tokens/completion_tokens/total_tokens）
  - **流式细节**：DeepSeek 流式 usage 是 finish 前最后一个 chunk，且其 **choices 为空数组**——现有 `!delta["choices"].empty()` 判断会跳过，需在 delta 层单独提取 `delta["usage"]`；accumulator 在 `markDone()` 后保留最后一次 usage 值
  - **落定规则**：只统计已落定的 usage——**中断/错误 return 路径不累计**（turn 未正常完成，符合"不估猜"原则）；正常完成 turn 的 usage 累加进 `m_totalTokensUsed`
  - `CLFAgentLoop` 维护 `m_totalTokensUsed`（每 turn 累加）；`ToolStats` 增 `totalTokens` 字段（**R4 实现注意：`int totalTokens = 0;` 默认成员初始化零破坏**；T11 覆盖"无 usage → 字段省略"）
- **summary 行追加**（接 M2 已落地的 P1-2 格式）：`● thought for Xs，N 工具 (read A · search B · edited C · 12.3k tok)`——usage 未返回时该字段整体省略（"无数据分组消失"）
- **/context 显示累计**：`本次会话累计: X tokens`
- **ContextMeter 可视化**（最后一步）：/context 输出总量百分比条形 `[████░░░░] 78%`（分段按消息角色 system/tools/messages，CLFContext 已有角色数据；若分类数据不足先做总量）
- 规格：所有 token 字段 usage 缺失时不显示，不估猜

## 2. 测试方案（测试驱动）

| # | 用例 | 断言 |
|---|---|---|
| T7 | 折叠块 | MockOutput 记录 showFoldedBlock；restoreSession 不 emitContent 历史行；摘要行含消息计数 |
| T8 | 审批卡分层 | prompt 含 `\n` → 快照/渲染函数断言 headline 与参数分层（无 screen 渲染函数单测）；confirm 退出后 prompt 清空 |
| T9 | 时间戳 | localTimeStamp() 输出匹配 `HH:mm` 格式；get_current_time 双平台编译路径 |
| T10 | usage 打通 | Mock 响应含 usage → m_totalTokensUsed 累加正确；流式 usage chunk（choices 空数组）解析；无 usage 响应不崩溃；**中断 turn 缺失 usage → 不累计**（R3） |
| T11 | summary token 字段 | stats 含 usage → summary 追加 `Nk tok`；无 usage → 字段省略 |

回归：全量 ctest（8 套件 + 新增）。

## 3. 里程碑

| 阶段 | 内容 | 估时 |
|---|---|---|
| M1 | P2-1 折叠 + P2-3 时间戳 + T7/T9 | 1 天 |
| M2 | P2-2 审批卡 + T8 | 0.5 天 |
| M3 | P2-4 usage 打通 + summary/context 显示 + T10/T11；ContextMeter 条形 | 1-1.5 天 |

> M3 是唯一有 API 依赖的阶段（外部审查建议采纳）：**T10/T11 测试先行**，mock 打桩 usage 场景写全再动解析代码。

## 附录：外部审查裁决记录（2026-08-14）

| # | 意见 | 裁决 |
|---|---|---|
| R1 | P2-1 展开数据源未明确 | 采纳：全量传入 CLFTerminal（messages 已驻留 m_context，回显为投影；量级 ≤ 几百 KB 有界）；不做懒加载（避免会话文件语义漂移） |
| R2 | P2-3 跨日状态落点未明确 | 采纳并修正设计：时间戳仅用户消息行（✻ 行进上下文，跨层状态不成立）；m_lastTsDate 落 CLFRepl |
| R3 | P2-4 流式 usage 落定时机未明确 | 采纳：只统计已落定 usage，中断/错误路径不累计；补流式 usage chunk choices 为空数组的实施细节；T10 补中断用例 |
| R4 | ToolStats totalTokens 构造/拷贝破坏（第二轮） | 采纳：默认成员初始化 `int totalTokens = 0;` 零破坏 |
| R5 | 折叠/展开滚动模型（第二轮） | 采纳关切、修正测试层级：视口保持入实现注意 + 人工验收清单（T7 为 MockOutput 单测，无渲染层） |

## 4. 涉及文件

- `src/CLFTypes/ICLFOutput.hpp`（showFoldedBlock 默认空实现）+ `CLFTypes.hpp`（localTimeStamp）
- `src/CLFUI/CLFTerminal.{hpp,cpp}`（折叠块状态/渲染、confirm 清 prompt）
- `src/CLFUI/CLFRepl.{hpp,cpp}`（Ctrl+R、用户行时间戳、渲染折叠行）
- `src/CLFUI/CLFConfirmBar.cpp`（headline/参数分层）
- `src/CLFCore/CLFAgentLoop.{hpp,cpp}`（restoreSession 改折叠、usage 累计、✻ 行时间戳）
- `src/CLFCore/CLFProtocolAdapter.{hpp,cpp}` + `CLFStreamAccumulator.{hpp,cpp}`（usage 解析）
- `src/CLFCore/CLFToolExecutor.cpp`（summary token 字段）
- `src/CLFTools/`（get_current_time 双平台）
- `src/CLFUI/CLFCommands.cpp`（/context 累计显示、ContextMeter 条形）
- `src/test/`（T7-T11）
