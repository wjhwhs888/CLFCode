# 设计-任务清单UI显示

> **状态**：设计定稿，待实施（含 2026-09-02 审查补丁，见 §八）
> **创建**：2026-08-25
> **前置**：S2-6 todo_write 工具（数据层已落地，UI 未显示）
> **核实基线**：2026-08-25 对照源码逐条验证，全部断言有代码依据（见 §二）
> **审查修订**：2026-09-02（CLFCode 方独立源码核实——断言级全部属实；3 项接线缺口 + 2 项边界缺陷定案，见 §八）

---

## 一、背景与目标

S2-6 已实现 `todo_write` 工具：agent 可创建/更新/查询/清除待办清单，数据存入会话状态（`CLFAgentLoop::m_todos`），随 `latest.json` 持久化、`/resume` 恢复。**但 UI 从未显示清单**——agent 建了任务，用户看不到"当前做到第几步、还剩哪些"。

本文档设计 todo 清单在终端 UI 的显示方案，对标 Claude Code 的任务清单（编号列表 + 完成状态标识）。

**核心目标**：

1. agent 创建 todo 后，用户能**实时看到**任务清单及每项状态
2. 任务推进（pending → in_progress → completed）时显示**即时更新**
3. 任务全部完成时**明确收尾**，不残留误导性状态
4. 未完成任务**本回合内保持可见**；**下一回合开始面板清空（UI）**，数据留 jsonl 历史（对齐 dsh 回合边界语义）
5. 会话恢复（`/resume`）后清单**按最后一轮快照重现**（续写起点）

**🔒 最高优先级原则（用户定，2026-08-25）**：**UI 显示只是显示，供用户查看，绝不能影响具体工作**。

- **显示优先级最低，随时可调整**——面板的显示规则、清空时机、图标、颜色、位置，全部是"可调整的装饰"，改它们**不触及任何数据层行为**
- **数据层是"工作"的根基，稳定**：`m_todos` 怎么存、`todo_write` 怎么工作、jsonl 快照怎么写——这些是 agent 工作的依据，**不受显示层任何决定影响**
- **单向依赖**：显示层 → 读 `m_todos`（只读）；数据层 ← 永不反向依赖显示层
- **显示层失败不影响工作**：面板渲染、T6 的 emit/置位等任何显示动作抛异常，**不得影响** `runTurn` 正常返回、`m_todos` 数据、jsonl 落盘（实现上 try/catch 隔离，见 §3.7 时序约束与 §6.4）
- **UI 不替 agent 决定**：显示层不推断、不迁移、不合并清单状态（§3.6、§6.2"模型怎么操作就怎么操作"是此原则的推论）

**🏗️ 生产/消费分离原则（用户定，2026-08-25）**：**UI 显示不涉及数据层具体业务逻辑，数据业务层也不参与 UI 显示的任何具体工作——一个负责生产，一个负责消费，功能上分离**。

- **数据层 = 生产者**：`m_todos` 状态、jsonl 持久化、`todo_write` 工具——通过公开接口（`getTodos()/setTodos()`、`ICLFOutput`、SessionManager API）对外提供，**不含任何渲染/显示代码**
- **UI 层 = 消费者**：面板、折叠块回显、收尾行——只通过接口读取数据并渲染，**不写数据、不触达数据层内部**
- **接口契约**：`ICLFOutput` 已是"Agent → UI"纯虚接口（零项目依赖、Mock 可替换 `CLFTerminal`，`ICLFOutput.hpp:1-9`）——todo 显示同样走此模式：数据层经 `getTodos()` 暴露清单，UI 层消费渲染
- **UI 可替换**：换一个 UI 界面（如 Web UI / 其他终端框架），只需实现 `ICLFOutput` + 消费 `getTodos()`/jsonl 接口，数据业务层**零改动**——"换 UI 也能按数据业务层接口实现相应显示"
- **T6 的边界**：写 complete 行（数据动作）在数据层；emit 收尾行 / 置 `m_todoPanelDone`（显示动作）在 UI 侧——通过接口衔接（§3.7 显示失败隔离），职责不混

**总原则（用户定，2026-08-25）**：**处理逻辑借鉴 Claude Code，显示用自有 UI 框架**。

- **借鉴 Claude Code 的部分**：触发时机、更新方式、完成处理、全部完成收尾——即"什么时候建清单、什么时候改状态、做完怎么办、全做完怎么办"这套**行为语义**（详见 §3.3-3.7 各节的"逻辑来源"标注）
- **不照抄的部分**：Claude Code 的 todo 是**内联对话流**渲染（每次更新显示一条消息、随滚动区滚走）；我们**不用**这种形态，改用自有常驻面板（§3.1-3.2）——因为我们的 UI 框架有独立的进度行/状态行区域，常驻面板是自然延伸，且"当前做到哪"内联的话滚走就丢
- **两者天然兼容**：处理逻辑是"数据何时怎么变"，显示是"数据怎么呈现"，互不耦合

---

## 二、现状核实（2026-08-25 对照源码）

### 2.1 数据结构与存储

| 事实 | 依据 |
|---|---|
| `CLFTodoItem { m_id, m_content, m_status }`，状态取值 `pending / in_progress / completed` | `CLFTypes.hpp:130-134` |
| 清单存于 `CLFAgentLoop::m_todos`（`std::vector<CLFTodoItem>`） | `CLFAgentLoop.hpp:125` |
| `getTodos()` 返回 `const&`，`setTodos()` 整体替换 | `CLFAgentLoop.hpp:78-79` |
| 随会话落盘：`saveSession` 写入 `latest.json` 的 `todos` 字段（**现状**；改造为 jsonl 追加见另案 `设计-会话追加式保存.jsonl.md`） | `CLFAgentLoop.cpp:430`、`CLFMessageCodec.cpp:28-35` |
| 恢复：旧会话文件无 `todos` 字段 → 空清单（向后兼容） | `CLFAgentLoop.cpp:449-451`、`CLFMessageCodec.cpp:124-130` |

### 2.2 工具侧

| 事实 | 依据 |
|---|---|
| `todo_write` 工具：`create`（**整表替换**）/ `update`（按 id）/ `list` / `clear` | `CLFBuiltinTools.cpp:194-275` |
| risk 为 `Read` 级 | `CLFBuiltinTools.cpp:518` |
| handler 捕获 `CLFAgentLoop&`（首个捕获状态的 handler），工具注册后 agent 不可拷贝/移动 | `CLFBuiltinTools.cpp:191-193,541-545` |

### 2.3 UI 渲染结构（`CLFRepl.cpp` 渲染循环）

当前 vbox 布局（`CLFRepl.cpp:410-419`）：

```
contentArea          ← 滚动区（flex，对话/工具输出，会滚走）
progressElements     ← 工具执行中单行进度（showProgress，动态出现）
statusLine           ← 状态点 + 文本（Running/Done/Warn/Error）
thinSep
input                ← 输入框
thinSep
modeLine             ← 模型名 │ 目录 │ 安全模式
confirmBar
```

### 2.4 刷新机制

| 事实 | 依据 |
|---|---|
| FTXUI 全帧驱动：`requestRefresh()` → `PostEvent(Custom)` 触发重绘 | `CLFTerminal.cpp:50-53` |
| turn 期间 `turnTimer` 以 **1Hz** 调用 `requestRefresh()`（F13：工具执行期界面不冻结的最低帧率） | `CLFAgentLoop.cpp:87-96` |
| `showProgress` **不主动刷新**，注释"由 emitContent 顺带刷新" | `CLFTerminal.cpp:287-293` |
| `emitContent` / `emitStyledLine` / `setStatus` 均调 `requestRefresh` | `CLFTerminal.cpp:60,72,81,157,252` |

### 2.5 ⚠️ 已发现的隐患：m_todos 跨线程无锁

- `todo_write` handler 在**工具执行线程**（asyncSubmit 工作线程）内修改 `m_todos`
- UI 渲染在**主线程**（`screen.Loop`）
- 若渲染直接读 `m_agent.getTodos()`，是**数据竞争（UB）**——`getTodos()` 返回 `const&` 无任何同步

> 该隐患在 S2-6 落地时已存在，只是当时 UI 不读清单所以未暴露。本次显示功能**必须先修锁，再接线渲染**。

---

## 三、设计决策

### 3.1 显示位置

**插在 `contentArea` 与 `progressElements` 之间**，作为独立"任务面板"区：

```
contentArea          ← 滚动区（历史：对话/工具输出）
────────────────────
📋 任务清单 2/3       ← 任务面板（本次新增，见 §3.2）
   ⏳ 分析项目情况
   ✓ 查看语言特性
   ○ 提交状态
────────────────────
progressElements     ← 工具执行中单行进度
statusLine           ← 状态点
...
```

**理由**：信息层级从"历史"到"当前计划"到"当前动作"到"当前状态"，自上而下时效递增、持久性递减。任务清单是**当前回合的计划**，既不属于会滚走的滚动区，也不属于瞬态的进度行——插在两者之间语义最顺。

### 3.2 面板形态

**默认展示完整列表**（不默认折叠），理由：

- 任务数通常 3-8 项，完整展示约 4-10 行，可接受
- 与 Claude Code"清单全量呈现、不藏不折叠"的展示语义一致（只是我们放在常驻面板，它放在对话流）
- 折叠态只省 2-3 行，却引入"要不要展开"的判断成本

**面板结构**（自上而下）：

```
📋 任务清单 2/3                    ← 标题行：图标 + 计数（完成/总数），完成数绿色
   ⏳ 分析项目情况                  ← in_progress：⏳ 图标 + 高亮（CyanLight）
   ✓ 查看语言特性                  ← completed：✓ + 绿色
   ○ 提交状态                      ← pending：○ + 灰（dim）
```

**状态图标与颜色**：

| 状态 | 图标 | 颜色 | 说明 |
|---|---|---|---|
| in_progress | `⏳` | CyanLight | 当前正在做的任务，唯一高亮项 |
| completed | `✓` | GreenLight | 已完成 |
| pending | `○` | 默认（dim） | 待办 |

**溢出处理**：任务数 > 10 时截断，末行 `… 还有 N 项`（dim）。截断只看条数不看宽度，每行内容本身超宽由滚动区既有换行逻辑处理（复用 `wrapW` 换行路径，`CLFRepl.cpp:205-215`）。

**无清单时**：整块不渲染（零占用），不显示任何占位。

### 3.3 触发显示条件

> **逻辑来源（Claude Code）**：agent 决定何时建清单、何时改状态——UI 不主动创建/销毁清单，只响应数据变化。

**✅ 清单的唯一入口 = `todo_write` 工具（用户确认 2026-08-25）**

任务清单的创建/更新/查询/清除**全部**通过 `todo_write` 一个工具完成——大模型想建清单、标完成、清空，都是调用 `todo_write`（`{"action":"create"/"update"/"list"/"clear"}`）；handler 写入 `m_todos`，面板只读 `m_todos` 呈现。**UI 不解析模型回复文本**，不自动推断，不读其他来源。

**与子 agent 的关系（未来项）**：子 agent（subagent）是**另一个工具**，负责"派活给另一个 agent 执行"，不碰 `m_todos`、不触发本面板。多步骤任务的清单（1 2 3 4 5）由 `todo_write` 创建并显示；子 agent 是独立的未来功能，落地后若有需要再评估是否并入面板（当前设计**不包含**子 agent 显示）。

**面板存在条件：`m_todos` 非空 且 `m_todoPanelDone` 为 false。**

具体触发场景：

| 场景 | 行为 | 逻辑来源 |
|---|---|---|
| agent 首次 `todo_write create` | 面板出现，显示新清单（大纲式：所有步骤平铺）；**清除 `m_todoPanelDone`** | Claude Code：TodoWrite 创建清单即显示 |
| turn 进行中，agent `update` 状态 | 面板**原地更新**对应行（✓/⏳/○），**不追加**。**跨轮场景**：新回合清空后，模型本轮 update → 清 `m_todoPanelDone` → **面板重现**（含更新后状态）——dsh projection 语义：任何 todo/write 事件重建投影（2026-09-02 实机验收修复，与 create 一致） | Claude Code：每次 TodoWrite 后重新渲染 |
| turn 结束（未全完成） | 面板**保留显示**（本回合内可见"做到哪了"） | Claude Code：清单不随回合结束消失 |
| **普通新回合开始（非 resume 的新输入）** | **面板清空（UI 不显示）**；数据留在 jsonl 历史（模型从会话历史知道未完成项，可重写/继续）。实现：`CLFRepl::submit` 在 runTurn 前判定——`m_agent.getResumedFrom()` 为空 → `m_agent.setTodoPanelDone(true)` | 对齐 dsh：`turn/start` → projection 清空（`tool-todo/src/index.ts:140`） |
| **resume 续写（第一条续写输入）** | **面板保留显示**（= 最后快照），**不清空**——resume 是"继续做"语义，模型在续写期间仍基于旧清单工作（用户问"2 3 怎么回事"时面板可见）；直到模型 create 新清单 / 全完成 / clear 才变化。实现：submit 判定 `m_agent.getResumedFrom()` 非空 → 跳过清空；续写文件创建后 `beginSessionFile` 清 `m_resumedFrom`，此后轮次恢复普通语义（§6.4 生命周期定案） | 用户定案（resume 续写 ≠ 普通新回合，§6.4） |
| 全部项 completed | 见 §3.7 收尾：追加最终清单到对话流 + 置 `m_todoPanelDone`（面板隐藏） | 用户定案（异于 Claude Code） |
| agent `todo_write clear` | 面板消失 | Claude Code：TodoWrite 清空清单即消失 |
| `/resume` 恢复会话 | **面板显示 = 恢复取值优先级的结果**（最后一条 todo_snapshot > 最后带 todos 的 turn 行，jsonl 文档 §3.4.2 步骤 4）：非全完成 → 面板重现（**清 `m_todoPanelDone`**）；全已完成 → 置 `m_todoPanelDone` 不显示（完成记录已在对话流历史） | 用户定案（resume 是"继续做"，非普通新回合，见 jsonl 文档 §3.4.2） |
| `/clear` | **清空 `m_todos` + 置 `m_todoPanelDone`**（新会话语义干净，旧清单不残留）——现状 `clearContext()` 只清 context 不清 m_todos（`CLFAgentLoop.cpp:382-385`），需补 | 新会话干净语义 |
| 程序启动、全新会话 | 无 todos → 不显示 | 一致 |

### 3.4 更新链路（如何更新显示）

> **逻辑来源（Claude Code）**：每次 TodoWrite 调用后，界面重新显示最新清单——**整表重绘**，不依赖增量 diff。我们沿用"数据变化 → 全量重绘"语义，渲染目标是**独立常驻面板**（滚动区外）。

**数据流**：`todo_write handler` 修改 `m_todos` → 工具循环结束 → 触发刷新 → 渲染读 `m_todos` → 面板**原地重绘**。

**关键约束（用户确认 2026-08-25）**：
- **面板不进对话流**：对话流（滚动区）是"当前在干啥、干了啥、回复"的工作流区；任务清单是**大纲式临时区**，两者分离
- **执行中不追加**：任务状态变化只改面板对应行，**绝不**向对话流追加新清单块、不触发对话流内容更新（唯一例外：**全部完成收尾时**追加一次最终记录，见 §3.7——那是收尾动作，不是执行中更新）
- **不重绘对话流**：todo 变化只重绘面板区域（FTXUI 全帧驱动下，面板是独立 Element，其数据变化不影响 `m_contentBuffer`）

**具体接线**：

1. **线程安全（前置修复）**：`m_todos` 加 `mutable std::mutex`；`getTodos()` 改返回**副本**（锁内拷贝），`setTodos()` 锁内整体替换。渲染每帧拷贝一次 vector，任务数小（<20），开销可忽略。
2. **刷新触发**：`CLFToolExecutor` 工具循环**每次迭代结束**统一调一次 `m_output->requestRefresh()`。
   - 现状：读类工具成功只走 `showProgress`，而 `showProgress` 不主动刷新 → 全靠 turnTimer 1Hz 兜底，**todo_write 更新后最长延迟 1 秒才重绘**
   - 统一刷新后：todo_write 返回即重绘，且工具动画帧率从 1Hz 提升到"每次工具结束"，顺带改善体验
   - 不特判工具名（todo_write 无需特殊身份），未来状态类工具自动受益
3. **渲染读取**：`CLFRepl` 渲染循环（`CLFRepl.cpp:412` vbox 构建处）调用 `m_agent.getTodos()`（现在返回副本，线程安全）→ 构建面板 Element 插入 vbox（contentArea 与 progressElements 之间）。
4. **todo_write 修改后即时快照**（跨文档，jsonl 文档 §3.2）：handler 在 create/update/clear 成功 `setTodos` 后立即调 `agent.appendTodoSnapshotNow()`（内部经 `m_activeSessionFile` 追加 `todo_snapshot` 行 + flush，失败 warn 不抛）与 `agent.markTodosDirty()`（list 不调）——防崩溃丢清单进度 + 置位轮末快照标志。

**刷新频率结论**：turn 进行中 = 每次工具结束即时刷新（+1Hz 兜底）；turn 空闲 = 面板静态保留。

### 3.5 任务完成后的处理（completed）

> **逻辑来源（Claude Code）**：单项完成 = 该项标记 `completed` 并**保留在清单中**，不清除、不折叠——直到 agent 显式清空或整表替换。

- 单项完成：`update status=completed` → 对应行图标变 `✓` + 绿色，标题计数 +1
- **不清除已完成项（未全完成时）**：完成项保留在清单中（`✓` 标识），直到 agent 明确 `clear` 或 `create` 整表替换
- **理由**：① 完成项是"本轮做了什么"的记录，用户回看需要 ② 对齐 Claude Code / dsh whole-list replacement 语义——清除/替换是 agent 显式动作，UI 不擅自清
- **全完成时的收尾例外**：见 §3.7——全部项 completed 后由系统统一"追加最终清单到对话流 + 置 `m_todoPanelDone` 隐藏面板"，不再等到 agent 显式 clear（用户定案）

### 3.6 未完成如何维持标识（pending / in_progress）

> **逻辑来源（Claude Code）**：pending / in_progress / completed 三态由 agent 通过 TodoWrite 显式声明；UI 只呈现状态，**不推断、不自动迁移**。

- `pending`：`○` 灰显，表示"还没轮到"
- `in_progress`：`⏳` 高亮，表示"当前在做"
- **回合内不清零**：清单原样保留，未完成项维持原状态（**本回合结束后面板保留显示**；**下一回合开始（非 resume 新输入）面板清空 UI**，数据留 jsonl 历史——见 §3.3 表格）
- **关键语义**：`in_progress` 是 agent 声明的状态，不是 UI 推断的——agent 说做完了才变 `✓`，agent 没说就停在原状。**UI 不自动把 in_progress 改回 pending**（那是 agent 的职责，UI 只呈现）
- 中断/错误后：清单保留最后一次 agent 写入的状态（本回合内可见）；jsonl 快照已落盘，可回看

### 3.7 全部完成后的收尾（用户定案 2026-08-25：追加 + 清空）

> **异于 Claude Code**：Claude Code 全 ✓ 后清单保持展示、等 agent 显式终结；我们改为——全完成后**追加最终清单到对话流（固化历史）+ 清空面板**。理由：面板是**临时运行态区**，任务执行完就应恢复零占用；最终状态通过"追加"落入对话流成为历史，用户可回看。

**场景**：清单所有项均为 `completed`，且该轮 turn 结束。

**行为**（顺序）：

1. **追加最终清单到对话流**：`emitContent` 一行简单明了的完成记录——
   ```
   📋 任务清单（全部完成）: ✓ 分析项目情况 · ✓ 查看语言特性 · ✓ 提交状态
   ```
   - **格式从简**（用户确认）：一行内联，✓ + 空格分隔，**不做复杂表格/多行渲染**
   - 追加进滚动区（`m_contentBuffer`），随对话流一起滚动、可回看
2. **清空面板显示**：面板区域恢复零占用（置 `m_todoPanelDone`），等待下一次 `todo_write create` 触发
3. **清单数据与"已归档"标志**：
   - `m_todos` **保留**（全 ✓）——jsonl 的 turn 行轮末快照与 todo_snapshot 行仍记录它（供 resume 回显）
   - **`m_todoPanelDone` 定案放 `CLFAgentLoop`**（`std::atomic<bool>`，2026-09-02 审查定案，§八 补丁 2）：置位后面板渲染跳过（§4.2 buildTodoPanel 入口检查），解决"m_todos 非空但面板应消失"的矛盾。放 AgentLoop 而非 CLFRepl 的原因：T6 检测在 AgentLoop 的 runTurn 完成分支（clf_core），**core 不依赖 UI 层**；Repl/命令层经公共接口 `setTodoPanelDone(bool)/isTodoPanelDone()` 访问
   - 下次 `todo_write create` 时清除该标志（新清单重新显示面板）

**判定**：`runTurn` **正常完成分支内、拼 worked 之前**（`CLFAgentLoop.cpp:331` 完成块、`:335` 前）检测 **`m_todos` 非空 && 全部 `completed` && `!m_todoPanelDone`（未收尾过）** → 触发上述追加+清空。

> ⚠️ **`!m_todoPanelDone` 是唯一防线，`m_todoDirty` 不参与 T6 判定**（2026-09-02 审查定案，§八 补丁 5）：
> - `!m_todoPanelDone`：防止已收尾的清单再次触发（收尾后置位；恢复全✓快照时置位）
> - ~~`m_todoDirty`~~ **已从 T6 判定移除**——原设计用它防"下一轮不碰 todo 重复追加"，但该场景已被 `!m_todoPanelDone` 单独防住；而它反而制造"中断残留全✓面板永不消失"缺陷（最后 update 后 Esc → 到不了完成分支 → 下一轮 `m_todoDirty=false` → 面板残留）。移除后：中断残留面板由**下一轮"新回合清空"（submit 置 done）解决**——面板消失符合新回合语义；收尾行不补发（用户主动中断，历史无收尾记录符合语义）。`m_todoDirty` 仅保留 jsonl 用途（决定 turn 行是否带 todos 快照字段，jsonl 文档 §3.2）

> ⚠️ **时序约束（关键，2026-08-25，详见 §6.3）**：追加触发点**必须在 `runTurn` 完成分支内、`emitContent(worked)` 之前**——此时总结文本已全部流式输出完毕（逐段 emitContent 已入 `m_contentBuffer`），追加的清单行会自然排在总结之后、worked 之前（顺序：总结 → 清单 → ✻ worked → 落盘）。**禁止**在工具迭代末 / 流式接收过程中触发——否则会出现"总结未显示完、清单已插入"的顺序错乱（最后一次 `update` 发生在总结生成**之前**，若在工具迭代末检测全完成，清单会插到总结前面）。

> ⚠️ **显示失败隔离（最高优先级原则的落地，§一）**：T6 追加（emitContent）、置 `m_todoPanelDone` 等**显示动作必须 try/catch 包裹**——显示层异常不得逸出到 `runTurn` 完成分支的外层 `catch`（`CLFAgentLoop.cpp:348`，它把异常当重试处理，会误触发重试/改变返回值）。**数据动作（写 complete 行）优先于显示动作**：顺序 = ① 写 complete 行（数据，先保证落盘；`CLFSessionManager::appendComplete(m_activeSessionFile, todos)`，`m_activeSessionFile` 为空则跳过，内部自兜底不抛）→ ② emit 对话流（显示）→ ③ 置 `m_todoPanelDone`（显示）；②③ 各自 try/catch，失败仅影响显示，不影响 ① 已完成的数据与 `runTurn` 正常返回。

**未全部完成时**：面板保留（未完成项可见"做到哪了"），不追加不清空。

### 3.8 与既有 UI 元素的交互

| 元素 | 交互 |
|---|---|
| 滚动区 | 任务面板**不进滚动区**（常驻），滚动只影响 contentArea |
| 进度行 progressElements | 面板在其上方；工具执行中的单行进度仍在原位 |
| 状态点 statusLine | 不变；`⏳` 图标与 running 状态点动画并存（状态点是"当前动作"，面板是"任务计划"，语义不冲突） |
| 折叠块/思考（Ctrl+T / Ctrl+R） | 互不影响 |
| 鼠标选区（copy-on-select） | 面板行**不参与选区行映射**（`m_lastRowMap` 只映射 contentArea 行，面板是独立 Element，天然隔离） |

### 3.9 线程安全汇总（本次引入的锁）

> **线程模型基准（2026-09-02 核实）**：`CLFAsyncSubmit` 单工作线程串行执行（launch 前 join，`CLFAsyncSubmit.cpp:12-27`）——submit/命令分发/runTurn/todo_write handler/轮末保存**全部同线程串行**；跨线程边界只有一条：**asyncSubmit 工作线程（写）↔ UI 主线程（渲染读）**。

| 位置 | 改动 |
|---|---|
| `CLFAgentLoop.hpp:125` | `m_todos` 旁加 `mutable std::mutex m_todosMutex;` |
| `CLFAgentLoop.hpp:78-79` | `getTodos()` 改为锁内拷贝返回 `std::vector<CLFTodoItem>`（值返回）；`setTodos()` 锁内 `m_todos = std::move(todos)` |
| `CLFAgentLoop.cpp:430` | `saveSession` 传参处改用局部副本（`auto todos = getTodos();`）避免重复加锁（jsonl 方案中该方法改造为 `appendTurnLine`，见 jsonl 文档 §3.9） |
| `CLFAgentLoop.cpp:449-451` | 恢复路径 `setTodos(std::move(todos))` 不变（内部已加锁） |
| `CLFAgentLoop`（新增） | `m_todoPanelDone`（**`std::atomic<bool>`**，定案放 AgentLoop，§八 补丁 2）：置位后面板隐藏；`create` 时清除、全完成收尾时置位、resume 恢复非全完成快照时清除、新回合（submit）时置位。跨线程（工作线程置位 ↔ 主线程渲染读），必须原子。接口 `setTodoPanelDone(bool)/isTodoPanelDone()` |
| `CLFAgentLoop`（新增） | `m_todoDirty`（**`std::atomic<bool>`**）：仅 create/update/clear 置位（handler 调 `markTodosDirty()`），list 不置；轮末 `appendTurnLine` 读取并清除（决定 turn 行是否带 todos 字段）。**不参与 T6 判定**（§八 补丁 5）。访问全在工作线程，atomic 为防御性 |
| `CLFAgentLoop`（新增） | `m_activeSessionFile`（`std::string` + `mutable std::mutex m_sessionCtxMutex`）：写=submit/`beginSessionFile`/cmdClear（工作线程）、读=handler/`appendTurnLine`/T6（工作线程）——当前全串行安全，mutex 为防御性（防未来并发调用方）。接口 `setActiveSessionFile(path)/getActiveSessionFile()`（getter 锁内拷贝） |
| `CLFAgentLoop`（新增） | `m_resumedFrom`（`std::string`，**无锁**）：写=restoreSession/`beginSessionFile`/cmdClear、读=submit 判定——全在工作线程串行，无需同步 |

> ⚠️ **agent 不可拷贝/移动约束**（S2-6 已 `= delete`）不受影响——`std::mutex` 成员恰好要求不可拷贝，与现有约束一致，无新风险。

---

## 四、实施拆分

### 4.1 改动清单

| # | 文件 | 改动 | 说明 |
|---|---|---|---|
| T1 | `CLFAgentLoop.hpp` | `m_todos` 加锁 + `getTodos/setTodos` 改线程安全 | 前置修复，§3.9 |
| T2 | `CLFAgentLoop.cpp` | `saveSession` 用副本；其余调用点核查 | 配合 T1 |
| T3 | `CLFToolExecutor.cpp` | 工具循环每次迭代末 `m_output->requestRefresh()`（注意 `:376`/`:387` 等 `continue` 提前退出分支也要覆盖——统一调用点放在循环体末尾且所有分支可达，或 RAII） | §3.4 刷新链 |
| T4 | `CLFRepl.cpp` | vbox 中 contentArea 与 progressElements 之间插入任务面板 Element | §3.1-3.2 渲染 |
| T5 | `CLFRepl.hpp` | （如需）任务面板构建辅助函数声明 | 纯函数，可单测 |
| T6 | `CLFAgentLoop.cpp`（完成分支检测 + complete 行写入）+ `CLFRepl.cpp`（submit 回合边界清空） | ① `runTurn` 完成分支内、拼 worked 前（`:331` 完成块、`:335` 前）检测"m_todos 非空 && 全 completed && !m_todoPanelDone"（**无 m_todoDirty**，§八 补丁 5）→ **顺序：① `CLFSessionManager::appendComplete(m_activeSessionFile, ...)`（数据，先保证落盘；无活动文件则跳过，内部自兜底不抛）→ ② emitContent 追加完成记录（显示，try/catch）→ ③ `m_todoPanelDone.store(true)`（显示）**；下次 `create` 时清除。② `CLFRepl::submit` 在 runTurn 前判定"普通新回合清空"：`m_agent.getResumedFrom()` 为空 → `m_agent.setTodoPanelDone(true)`（面板清空 UI，数据留 jsonl 历史，§3.3）；**resume 续写不清空**（`m_resumedFrom` 非空时跳过，§6.4-E；续写文件创建后清 `m_resumedFrom`，§八 补丁 5）。**禁止在工具迭代末/流式中触发**（时序约束见 §3.7、§6.3）；**显示动作异常隔离**（§3.7 显示失败隔离，不得逸出完成分支） | §3.7 收尾 + 回合边界 |
| T7 | `CLFBuiltinTools.cpp` | `todoTool.m_description`（`:515-517`）加引导："继续已有任务时用 update（按 id 改状态），不要用 create 重建；create 为整表替换，仅用于全新清单"——降低 create 误删风险（不做逻辑兜底，§6.2） | 一行小修 |

### 4.2 面板渲染实现要点（T4）

在 `CLFRepl.cpp` 渲染循环内新增一个构建函数（建议静态/匿名命名空间纯函数，便于单测）：

```
buildTodoPanel(const std::vector<CLFTodoItem>& todos, bool panelDone) -> ftxui::Element
```

逻辑：

1. **入口检查**：`todos.empty()` **或 `panelDone` 为 true** → 返回 `ftxui::emptyElement()`（后者：清单已全完成收尾，m_todos 仍保留但面板不显示；调用方从渲染处传 `m_agent.isTodoPanelDone()`，纯函数保持可单测）
2. 统计 completed 数 → 标题行：`📋 任务清单 n/total`（**执行中形态**；全完成收尾走 §3.7，面板随即清空，故面板自身不渲染"全完成标题"）
3. 逐项：按 status 映射图标（⏳/✓/○）+ 颜色 → 每行 `ftxui::hbox({text("   "), text(icon), text(" "), text(content)})` + 对应颜色
4. 溢出截断（>10 项）
5. 返回 `ftxui::vbox({title, rows...})`

### 4.3 测试计划

| 测试 | 覆盖 | 方式 |
|---|---|---|
| `qa_CLFTodoPanel`（新增） | 纯函数 `buildTodoPanel`：空清单→空 Element / 计数正确 / 三态图标映射 / 溢出截断 | boost::ut 单测，无 UI 依赖 |
| `qa_CLFAgentLoop` 扩展 | `getTodos` 线程安全：并发读写不崩（多线程交替 get/set） | 加锁后回归 |
| `qa_CLFToolExecutor` 扩展 | 工具迭代末触发 requestRefresh（MockOutput 计数） | Mock 计数断言 |
| `qa_CLFRepl` 扩展（如可行） | 全完成检测：全 completed 时触发追加+清空（MockOutput 捕获 emitContent 与面板状态） | 若 UI 逻辑可抽取则单测 |
| 现有全套 | `qa_CLFMessageCodec`（todos 序列化）不变应全绿 | 回归 |
| 人工验收 | 真实会话：create 多任务 → 逐项 update → 全完成追加+清空 → clear | 见 §五 |

### 4.4 构建

- 新增测试加入 `CLF_TEST_TARGETS` 列表（`src/CMakeLists.txt`，**必须**，否则 C++17 下 boost::ut 编译失败——A1 教训）
- MSVC Debug/Release 干净重建 + ctest

---

## 五、人工验收清单

1. 无 todo 时启动 → 界面无任务面板、无占位
2. 让 agent 用 `todo_write create` 建 3 项任务 → 面板立即出现，计数 `0/3`，全 `○`（大纲式平铺）
3. agent `update` 第 1 项为 `in_progress` → 该行变 `⏳` 高亮，计数不变（**原地更新，无追加**）
4. agent `update` 第 1 项为 `completed` → 该行变 `✓` 绿，计数 `1/3`（原地更新）
5. 执行全程观察对话流 → **无任何清单块追加**（对话流只显示工作流内容）
6. 依次完成全部 → 本轮结束自动：对话流追加一行 `📋 任务清单（全部完成）: ✓ A · ✓ B · ✓ C` → **面板清空消失**
7. 完成后再输入新对话（不建新任务）→ 面板保持消失，对话流里保留完成记录可回看
8. agent `todo_write clear` → 面板消失（未全完成时）
9. 有未完成任务时 `/exit` 再 `/resume` → 面板原样重现（含各状态）
10. 工具执行中（读类工具 showProgress 动画）→ 任务面板与进度行同屏正常
11. 任务面板行鼠标拖选 → 不影响 contentArea 选区的正常功能
12. 中断场景：清单未全完成被中断 → 面板保留未完成状态，不追加不清空；**无 ✻ worked**
13. 迭代上限：清单未完成触发 16 轮上限 → 显示 `[Error] Exceeded...` + Warn 状态点，**无 ✻ worked**，面板保留
14. 模型主动停但未标完：finish_reason=stop 且清单非全 ✓ → **有 ✻ worked**，面板保留（未完成可见）
15. 全完成收尾顺序：总结 → `📋 任务清单（全部完成）` → `✻ worked`（清单行紧跟总结、在 worked 前）
16. 继续任务：清单 1✓ 2⏳ 3○ 4○ 5○ 时用户说"继续做 2 3"→ 模型若 update 则 1 4 5 保留；若 create 只写 2 3 则整表替换（1 4 5 消失）——**UI 只呈现，不干预**（§6.2）
17. 工具描述引导：todo_write 的 help/schema 文案含"继续已有任务用 update"（T7 后人工确认模型行为改善）
18. **新回合清空**：清单未完成（如 1✓ 2⏳ 3○），回合结束面板保留 → 用户输入新对话（非 resume）→ **面板清空（UI 不显示）**，jsonl 历史里仍有该轮快照
19. **resume 面板 = 最后一轮快照**：resume 一个未完成会话（最后快照 1✓ 2⏳ 3○）→ 面板重现 1✓ 2⏳ 3○（续写起点）
20. **resume 全完成不显示**：resume 一个全完成会话（最后快照 3✓）→ 面板不显示（完成记录在折叠块历史里）
21. **resume 续写不清空**：resume 未完成会话（面板 1✓ 2⏳ 3○）→ 输入"继续做 2 3"→ 面板**保留**（不清空）→ 模型 list 确认 → 完成 2 3 → 全完成收尾
22. **/clear 清清单**：有未完成清单时 `/clear` → 面板消失 + 新会话无残留（m_todos 已清）
23. **list 不触发快照**：一轮内只 `todo_write list`（不改数据）→ 该轮 jsonl 无 todos 字段（m_todoDirty 未置位）
24. **多轮不重复追加**：全完成收尾后（m_todoPanelDone=true）→ 下一轮模型不碰 todo → 不重复追加清单行（消费条件含标志检查）
25. **中断残留（§6.4-H）**：清单在最后一个 update 后全✓，用户 Esc 中断 → 本轮面板残留全✓、无收尾行 → 用户输入新对话（新回合）→ **面板清空消失**、对话流无收尾行（符合"被打断"语义）→ 数据仍留 jsonl 历史

---

## 六、边界情况与风险

| 情况 | 处理 |
|---|---|
| 状态值非法（非三态） | 数据层已校验（`isValidTodoStatus`，`CLFBuiltinTools.cpp:231`），UI 遇未知值按 pending 渲染兜底 |
| `m_content` 为空 | 数据层 create 已跳过空内容项（`:227`）；UI 遇空内容显示 `(无内容)` dim 兜底 |
| 任务数巨大（如 100） | 截断到 10 项 + `… 还有 N 项`，不卡渲染（渲染只遍历截断后行数） |
| 渲染读清单与 handler 写清单并发 | T1 锁解决；副本拷贝在锁内，成本 <1μs（小 vector） |
| agent 崩溃/中断时清单状态 | 保留最后一次写入状态；`saveSession` 每轮落盘，`/resume` 恢复 |
| 面板挤压滚动区高度 | 面板行数 = 任务数（≤10 截断），滚动区 flex 自动收缩，可接受 |
| 旧会话文件无 todos 字段 | 已兼容（空清单 → 面板不显示），无回归 |

### 6.1 未完成路径与 ✻ worked 的触发事实（2026-08-25 核实）

**三种"清单未完成"路径，均不触发 T6 消费**（消费条件 = 全 completed && !m_todoPanelDone，天然排除）：

| 场景 | ✻ worked 是否显示 | 清单状态 | T6 消费 |
|---|---|---|---|
| 模型主动停（finish_reason=stop，未标完） | ✅ 显示（走完成分支 `CLFAgentLoop.cpp:331-339`） | 未完成 | ❌ 不消费（面板保留） |
| 用户中断（Esc/Ctrl+C） | ❌ 不显示（各 `[Interrupted]` return 在完成分支前） | 未完成 | ❌ 不触发（到不了完成分支） |
| 迭代上限（超过 `m_maxToolCallIterations`=16） | ❌ 不显示（`:366` 只拼 `finalContent` 无 `emitContent`；返回 `[Error] Exceeded...` + Warn 状态点） | 未完成 | ❌ 不触发 |

**结论**：T6 挂在完成分支 + 消费条件 = 全 completed，恰好把三种未完成情况都排除——只有真正"全部完成"才消费。

### 6.2 create 整表替换的模型行为风险（2026-08-25 定案：模型怎么操作就怎么操作）

**背景**：清单 1✓ 2⏳ 3○ 4○ 5○，用户说"计划中 2 3 还没做，请继续"。模型有两种合法选择：

| 模型行为 | 效果 | 风险 |
|---|---|---|
| `update`（按 id 改状态） | 原清单保留，只改 2 → 1✓ 2⏳ 3○ 4○ 5○ | 无 |
| `create`（整表替换） | **只提交 2 3 → 1 4 5 从 `m_todos` 消失** | 清单不完整 |

**⚠️ `m_todos` 是模型的工作记忆**（模型每轮 `list` 它做计划），不只是显示——create 只写部分项时：
- 1 4 5 从工作记忆消失 → 模型可能忘做 4 5 / 重做 1（但对话历史里有旧快照，风险程度取决于 context 深度与模型注意跨度）

**定案（用户 2026-08-25）**：
- **UI/数据层不做程序级兜底**——create 是 dsh 语义的整表替换（对齐 Claude Code），handler 不该偷偷合并，违反"agent 显式控制"原则
- **模型怎么操作就怎么操作**：模型调 update 就更新原清单，调 create 就整表替换（含误删），UI 只呈现 `m_todos` 结果，不干预
- **缓解**：工具描述引导（一行小修，`CLFBuiltinTools.cpp:515-517` `m_description`）——加"继续已有任务时用 update（按 id 改状态），不要用 create 重建；create 为整表替换，仅用于全新清单"。从源头降低 create 误删概率，但不做逻辑兜底

### 6.3 追加时机与流式输出的时序约束

**顺序**：模型总结（流式 emitContent 逐段）→ 📋 清单行（程序追加）→ ✻ worked（程序追加）。三者都进 `m_contentBuffer`，追加顺序 = 显示顺序。

**触发点**：`runTurn` 正常完成分支内、拼 worked 之前（`CLFAgentLoop.cpp:331` 完成块，`:335` 前）——此时总结已全部输出完毕，清单行插在总结与 worked 之间。

**禁止**：工具迭代末 / 流式接收过程中触发（否则清单会插到总结前面——最后一次 `update` 发生在总结生成之前，工具迭代末检测会提前追加）。

**✻ worked 的来源**（`CLFAgentLoop.cpp:335`）：`"\n \n✻ " + m_labels.worked + " for " + formatDurationSeconds(s)`——`m_labels.worked` 默认 `"Worked"`（`CLFTypes.hpp:180`）、`s` = `now - turnStart`（`:86` 记录）、`formatDurationSeconds` 格式化（`CLFTypes.hpp:184`）。**是程序收尾标记，非模型文本**；任务清单行同为程序追加——两者同级（都是模型总结之后的程序收尾行），清单在前、worked 在后。

### 6.4 审查补丁：8 个边界问题（A-G 2026-08-25 二轮审查；H 2026-09-02 三轮审查）

| # | 问题 | 修复 |
|---|---|---|
| A | `m_todoPanelDone` 跨线程数据竞争（T6 在 asyncSubmit 工作线程置位、主线程渲染读） | 改 `std::atomic<bool>` 且**定案放 AgentLoop**（§3.9、§八 补丁 2） |
| B | resume 后未完成快照面板重现时，`m_todoPanelDone` 未清 | resume 恢复非全完成快照 → 清 `m_todoPanelDone`（§3.3 resume 行） |
| C | `/clear` 只清 context 不清 `m_todos`（现状 `CLFAgentLoop.cpp:382-385`），面板残留旧清单 | `/clear` 同时清 `m_todos` + 置 `m_todoPanelDone`（§3.3 /clear 行，jsonl 文档 J5 同步） |
| D | `m_todoDirty` 若在 handler 统一置位，`list` 也会误置（list 不改数据，不该触发快照） | `m_todoDirty` **仅 create/update/clear 置位**，list 不置（§3.2 判定补充） |
| E | resume 后第一条续写输入若按"普通新回合"清空面板，用户刚看到清单立刻消失 | **resume 续写 = "继续做"语义**：第一条续写输入**不清空面板**（§3.3 resume 续写行）——用户问"2 3 怎么回事"时面板可见，模型基于 `m_todos`（= 未完成快照）检查并继续完成 |
| F | 全完成收尾后 `m_todos` 保留全✓、`m_todoPanelDone=true`——下一轮模型不碰 todo 时，完成分支检测"全 completed"仍 true → **重复追加清单行** | 判定加 `!m_todoPanelDone` 条件（§3.7 判定）——已收尾过 → 不追加。~~原写 m_todoDirty 一并加入~~（2026-09-02 审查移除，见新增 H 行） |
| G | **显示动作异常逸出到 runTurn 外层 catch**：完成分支的 `emitContent(worked)`（`CLFAgentLoop.cpp:338`）/`setStatusKind`（`:342`）现有代码无 try/catch，异常会被 `:348` 当重试处理——T6 的 emit/置位若不加隔离会误触发重试 | **T6 显示动作 try/catch 包裹**（最高优先级原则）：数据动作（写 complete 行）优先，显示动作（emit/置位）各自隔离失败；异常不得逸出完成分支（§3.7 显示失败隔离） |
| H | **中断残留全✓面板永不消失**（2026-09-02 审查发现）：模型在最后一个 `update` 后立即被 Esc（或迭代上限）→ 到不了完成分支 → 面板永久显示全✓清单；下一轮模型不碰 todo → `m_todoDirty=false` → 面板残留 | **T6 判定移除 `m_todoDirty`**——只留"全 completed && !m_todoPanelDone"。中断残留面板由下一轮"新回合清空"（submit 置 done）解决——面板消失符合新回合语义；**收尾行不补发**（下一轮 submit 已先置 done，完成分支不再触发；用户主动中断，历史无收尾记录符合语义，与"被打断"事实一致）。F 行担心的"重复追加"由 `!m_todoPanelDone` 单独防住（收尾后已置位）✅；恢复全✓快照时 restoreSession 置位 ✅。`m_todoDirty` 仅保留 jsonl 快照字段用途（jsonl §3.2） |

**resume 续写 vs 普通新回合的区分依据**：`m_resumedFrom` 非空（处于 resume 续写中）→ 面板保留；普通新对话（无 resume）→ 回合清空。`/clear` 清 `m_resumedFrom` 后回到普通语义。

**`m_resumedFrom` 生命周期（定案，2026-09-02）**：置位 = `restoreSession` 内部（恢复即进入续写态）；清除 = ① `/clear` ② **续写文件创建后**（`beginSessionFile` 内，§八 补丁 5）。即：resume 后续写会话的**第 2 轮及以后输入按普通语义**（新回合清面板）——清单数据仍留 jsonl 历史与 `m_todos`，模型可 `list` 查看，语义自洽。若不清除则续写会话永远不清面板，与"每回合清空"矛盾。

**模型行为预期（resume 续写场景）**：resume 后 `m_todos` = 最后清单状态（**最后一条 todo_snapshot** 优先，否则最后带 todos 的 turn 行——jsonl 文档 §3.4.2 步骤 4；如 1✓ 2⏳ 3○ 4○ 5○）——模型从 context/jsonl 历史看到完整清单；用户问"2 3 怎么回事"时，模型 `list` 确认状态 → 检查原因 → 按清单继续完成 2 3（update 标记）→ 面板实时更新 → 全完成触发收尾。面板全程正确反映（§3.3 resume 续写行）。

---

## 七、与后续计划的衔接

- 本功能是 **S3 之前可独立插入的小批**（约 0.5-1 天），也可并入 S3 排期
- **ask_user（N 选项确认栏）**未来落地时，本面板的"常驻区 Element 构建 + 独立于滚动区的定位"可直接复用为确认栏的视觉框架（同为"内容区外的常驻交互区"）
- 不涉及 dsh 对接决策，无 B 阶段前置依赖

---

## 八、审查补丁（2026-09-02，CLFCode 方定案，实施前必读）

> 来源：对两份文档的独立源码核实（40+ 断言全部属实，仅 2 处行号小偏差已就地修正）。本节约定 4 项接线缺口与 2 项边界缺陷，**实施时按本节为准**。

### 补丁 1：jsonl 写入的线程模型（对应 jsonl 文档 §3.8/§3.9）

- **核实结论**：`CLFAsyncSubmit` 单工作线程串行执行（`launch` 前 `join()`，`CLFAsyncSubmit.cpp:12-27`）——submit/命令分发/runTurn/todo_write handler/todo_snapshot 追加/turn 行追加**全部同线程串行，无写并发**。UI 主线程只读（渲染）。
- **定案**：`CLFSessionManager` 的 `append*` 方法仍加 **static `std::mutex`**（防御性）——照 CLFLogger 惯例（mutex + ios::app + flush 三件套），防未来并发调用方（如 S3 摘要自动触发若移到别的线程）。当前接线不依赖此锁的正确性。
- 真跨线程边界只有一条：**工作线程写 ↔ 主线程渲染读** → `m_todosMutex`（§3.9）与 `m_todoPanelDone` atomic 覆盖。

### 补丁 2：`m_todoPanelDone` 定案放 `CLFAgentLoop`

- 原文 §3.7 悬而未定（"CLFRepl 或 CLFAgentLoop"）、§3.9 定 CLFRepl——与 T6 检测点（runTurn 完成分支，clf_core）矛盾：**core 不依赖 UI 层**，放 Repl 则检测读不到。
- 定案：`CLFAgentLoop` 成员 `std::atomic<bool> m_todoPanelDone{false}` + 接口 `setTodoPanelDone(bool)/isTodoPanelDone()`。Repl 渲染经 `m_agent.isTodoPanelDone()` 读取；submit 新回合清空经 `m_agent.setTodoPanelDone(true)` 置位。（文档 §3.7/§3.9/T6/§4.2/§6.4-A 已同步）

### 补丁 3：会话文件上下文接口（对应 jsonl 文档 §3.9，两份文档共用）

AgentLoop 新增（handler 与完成分支经此写 jsonl，不依赖 Repl 传参）：

```cpp
// —— 会话文件上下文（jsonl 追加式保存）——
void        setActiveSessionFile(const std::string& jsonlPath); // 空串=无活动文件
std::string getActiveSessionFile() const;                      // 锁内拷贝
void        beginSessionFile(const std::string& firstInput);    // 懒创建：header+首轮；
                                                                // resumedFrom 非空则复制续写，清 m_resumedFrom
void        appendTodoSnapshotNow();  // todo_write 修改后 handler 调用：追加 todo_snapshot 行+flush
std::string appendTurnLine();         // 轮末 Repl 调用：追加 turn 行（本轮消息差集+todos 快照），清 m_todoDirty
void        markTodosDirty();         // create/update/clear 置位（list 不调）
void        setResumedFrom(const std::string& p);  // restoreSession 内部置位；/clear 与 beginSessionFile 清除
const std::string& getResumedFrom() const;
```

### 补丁 4：`m_resumedFrom` / `m_activeFile` 定案放 `CLFAgentLoop`

- 原 J4 定放 CLFRepl——但命令 handler 签名 `(cmdName, args, agent, historyDir, output)`（`CLFCommandDispatcher.hpp:22-27`）**没有 Repl 引用**，`/resume` 记录、`/clear` 关文件都接不上线。
- 定案：`m_resumedFrom`（=补丁 3 的 `m_resumedFrom`）与 `m_activeFile`（=补丁 3 的 `m_activeSessionFile`）都放 AgentLoop。`restoreSession` 内部置 `m_resumedFrom = filePath`（最自然）；cmdClear 经 agent 引用关闭文件；submit 经 `m_agent` 建文件。**命令层骨架零改动**。
- `m_resumedFrom` 生命周期（补丁 5 联动）：置位=restoreSession；清除=① `/clear` ② `beginSessionFile` 建续写文件后。续写会话第 2 轮起按普通语义（新回合清面板）。

### 补丁 5：T6 判定移除 `m_todoDirty`（修复中断残留缺陷）

- 缺陷：最后 update 后 Esc → 到不了完成分支 → 面板永久显示全✓；下一轮 `m_todoDirty=false` → 面板残留、收尾丢失。
- 定案：T6 判定 = **"m_todos 非空 && 全部 completed && !m_todoPanelDone"**。`!m_todoPanelDone` 单独防住 F 行"重复追加"（收尾后置位）与"恢复全✓"（restoreSession 置位）。
- **中断残留的实际结局（终审定案）**：下一轮 submit 的"新回合清空"先置 done → 面板消失（新回合语义 ✅）；完成分支检测到 done=true 不再补发收尾行——**收尾行在中断场景下不补发**（用户主动中断，历史无收尾记录与"被打断"事实一致，可接受）。原初稿"下一轮完成分支自动补收尾"表述有误（忽略了 submit 清空判定先于完成分支执行），已修正。
- `m_todoDirty` 仅保留 jsonl 用途（turn 行 todos 快照字段开关）。
- 验收 24 语义更新：全完成收尾后下一轮不碰 todo → 不重复追加（`!m_todoPanelDone` 已置位，非 m_todoDirty）。

### 补丁 6：list 的 [当前] 标记重定义（对应 jsonl 文档 §3.9/J5）

- 取消 latest.json 后：`m_isLatest` 失去来源（`CLFSessionManager.cpp:217-239` 依赖 latest.json 存在）。
- 定案（2026-09-02 终审修正）：`CLFSessionManager::list` 增加可选参数 `const std::string* activeFilePath`——**[当前] 标记重定义为"`m_path == *activeFilePath`"，不排除活跃文件**。cmdResume/cmdHistory 经 `agent.getActiveSessionFile()` 传入，UX 与现状完全一致。
- **resume 活跃文件 = 复制续写**（与 resume 任意归档同一行为）：restoreSession 置 `m_resumedFrom` → 用户输入 → `beginSessionFile` 创建"续"文件并切换 `m_activeSessionFile` → **原文件冻结在 resume 时点、不再被写**（写路径已切到续文件），不存在双写。与现状 resume `[当前]`（覆盖式继续）语义对齐——区别只是 jsonl 时代产生一个冻结快照 + 一个续文件，内容有重叠但无乱象。
