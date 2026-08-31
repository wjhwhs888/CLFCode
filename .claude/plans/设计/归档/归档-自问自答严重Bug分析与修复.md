# 归档-自问自答严重Bug分析与修复

> **状态**：✅ 已实施 + 验收通过 + 归档（2026-08-31，随 v0.4.2 发布）
> **发现**：2026-08-31（用户实际使用中发现）
> **严重性**：无用户输入自动提交 → 自问自答 → 消耗 API 配额 / 污染会话上下文
> **核实基线**：2026-08-31 对照源码 + 用户会话日志 + 会话 JSON 三重证据 + 用户实证确认
> **验收**：单测 P1-P10 + N1-N6 全绿；实机注入（busy 期间 Ctrl+V 多次）零自动提交、文本停输入框、二次 Enter 提交、5.4 万字符回复期间注入无干扰、多行粘贴全链路正确

---

## 一、现象描述

**用户报告**：使用 CLFCode 正常对话，输入一个问题后，**没有任何后续输入**，程序自动提交消息（自问自答），停不下来，最终用户按 Esc 终止。

**触发特征**：
- **长对话后出现**（约 4-5 轮后）
- **长回复时概率升高**（触发轮回复达 21474 字符）
- 开始几轮正常，之后突然异常

**用户实证（2026-08-31 澄清）**：16:26:00 提交消息后**再未输入任何内容、未按任何键**，16:31:49 发生自动提交；期间用户在看长回复、滚动鼠标翻看。

---

## 二、证据链（三重证据 + 用户实证）

### 2.1 会话日志（`C:\Users\wjhwh\CLFCode\doc\log\clf_agent.log`）

```
16:02:46 [Submit] entry, input=你的问题我可以回答你：        ← ⚠️ 自动提交（用户未输入）
16:02:47 streaming done, content=146chars

16:04:18 [Submit] entry, input=你的问题我可以回答你：        ← 用户真实输入（续写补全）

16:26:00 [Submit] entry, input=我说说我的看法：1、主控机...   ← 用户真实输入
16:26:53 streaming done, content=21474chars                   ← 超长回复
16:26:53 [Submit] exit

16:31:49 [Submit] entry, input=你猜我咋想的，这个系统的初衷...  ← ⚠️ 自动提交（用户实证：零输入零按键）
16:33:18 [Save] latest.json updated                            ← 被用户 Esc 中断（89 秒无日志 = 长流式回复中）
16:48:45 [Submit] entry, input=/exit
```

关键事实：16:31:49 后**只有一次** API 请求（`iter=0, ctx_msgs=52`），89 秒无日志后被 Esc 中断——"多轮自问自答"实为**一次自动提交 + 长回复**（用户感知为"又发请求了"）。

### 2.2 会话 JSON（`doc/contextHistory/2026-08-31_16-48-45_你是谁.json`，共 52 条）

```
[49] role=user      | 我说说我的看法：...（16:26 提交，多行）
[50] role=assistant | 你这个框架比我刚才说的更清晰...（21474 字符长回复）
[51] role=user      | 你猜我咋想的，这个系统的初衷我猜是为了让不懂编程，但是懂dodaf uaf等方法论的人，通过无代码的方式来建模，还能验证测试  ← ⚠️ 自动提交
```

- [51] 为 62 字符单行文本；已验证 [50] 模型回复**不含**"你猜我咋想的"（非模型文本回流）
- 原初稿索引 [50]/[51]/[52] 系 1 基/0 基混用，本版统一为 JSON 实际索引 [49]/[50]/[51]

### 2.3 注入源分析（核心结论）

**✅ 已确认的事实**：
- 用户 16:26:00 后零输入、零按键（用户实证）
- inputText 在 16:26 提交时已 `clear()`（`CLFRepl.cpp:476`），此后必须被某事件写入 62 字符文本
- 代码路径全部排除：`m_lastSubmittedInput` 恢复=[49] 内容、`m_inputHistory` 无此句（日志全量核对）、`m_pendingText` 已被 16:26 提交覆盖、CPR 剥离只删不增
- **唯一可能 = 终端注入**（62 个中文字符的注入只有粘贴/IME 两种途径，IME 需用户打字，已排除）

**❌ 已排除的候选**：右键粘贴——用户**同一终端实测**（两次独立测试）：右键后输入框无任何内容。FTXUI 无 bracketed paste 处理（3rdparty grep 零命中），若终端发送包裹序列应显示乱码 `[200~...`，实测无任何显示 → 该终端右键粘贴未注入。

**✅ 已确认的注入通道**（用户实测 2026-08-31，多行文本粘贴完整接收）：
- **Shift+Insert**：注入生效
- **Ctrl+V**：注入生效（Windows Terminal 终端层粘贴设置）
- 右键：不注入（见上）

**16:31 场景最可能注入源**：用户滚轮翻看长回复时**无意识误触 Ctrl+V**（左手搭键盘），剪贴板中恰有草稿"你猜我咋想的…"残留（带行尾换行）。事件序列与 Shift+Insert 同构。精确方式待 `CLF_DEBUG_EVENTS=1` 事件日志最终确认（2026-08-31 首轮取证因变量只在内层实例生效未取到，重测中）。

**⛓ 注入后的自动提交链（机制确定，与注入源无关）**：

```
终端注入：62 个 Char 事件密集到达（Idle 态 PassThrough → 进 inputText）
→ 末尾 Return 事件 → onReturn(Idle) → enterPendingLocked（40ms 静默窗）
→ 窗内无字符（注入已结束）→ 窗满 → pendingConfirmed=true（CLFPasteCoalescer.cpp:23-24）
→ wakeCb PostEvent(Custom) → CLFRepl.cpp:482 任意事件消费 → doSubmit → 自动提交
```

### 2.4 📁 取证文件清单

| 文件 | 路径 | 用途 |
|---|---|---|
| **Agent 运行日志** | `C:\Users\wjhwh\CLFCode\doc\log\clf_agent.log`（安装目录，被占用时共享读，GBK 编码） | `[Submit] entry` 序列与时间戳 |
| **会话归档 JSON** | `C:\Users\wjhwh\CLFCode\doc\contextHistory\2026-08-31_16-48-45_你是谁.json` | 消息 [49]/[50]/[51] 全文 |
| **事件调试日志**（未开） | `doc/log/clf_events.log`（`CLFRepl.cpp:116`，需设 `CLF_DEBUG_EVENTS=1`） | 确认注入源 |
| **粘贴合并器原始设计** | `.claude/plans/设计/归档/归档-复制粘贴功能修改.md` | "窗满自动提交"设计意图与 P1-P10 用例 |

---

## 三、根因分析

### 3.0 关键事实核对（源码逐条验证）

| 事实 | 依据 |
|---|---|
| **提交唯一入口** | `submit()` 全库仅一个调用点：`CLFRepl.cpp:477` doSubmit 的 lambda；doSubmit 只被 :482（窗满消费）与 :631（Ctrl+D）调用 |
| **窗满后提交必然发生** | wakeCb = `PostEvent(Custom)`（`CLFRepl.cpp:85-86`）→ Custom 必然到达 CatchEvent → :482 消费——**不存在"上膛后长时间不消费"** |
| **busy 检查已在 doSubmit 内** | `CLFRepl.cpp:471` `!asyncSubmit.busy()` |
| **40ms 窗只检测 Return 后** | `CLFPasteCoalescer.cpp:105-120` onCharacter 只在 PendingSubmit 态开窗转 PasteMode；**无"Return 前置字符突发"检测** |
| **粘贴只能走终端原生路径** | `CLFClipboard::read()` 全库零调用点 |

### 3.1 提交触发链（现状）

```
提交入口（CLFRepl.cpp CatchEvent handler 内）：
  ① :482  pasteCoalescer.pendingConfirmed() → doSubmit   ← 「任何事件到达时检查」
  ② :631  Ctrl+D → doSubmit
  ③ :477  asyncSubmit.launch（仅被 doSubmit 调用）

pendingConfirmed=true 由 Return 上膛建立：
  Return → :593 onReturn → PENDING（捕获 inputText）
  → 40ms 静默窗满（CLFPasteCoalescer.cpp m_quietWindowMs=40ms）
  → 定时线程置 pendingConfirmed=true
  → wakeCb PostEvent(Custom) → Custom 到达 :482 → doSubmit（busy=false 时）→ 提交
```

### 3.2 🔴 核心缺陷：单行注入末尾 Return 与手打回车在事件层完全同构

**机制**：40ms 窗只检测「Return **之后**窗内字符」（区分手打 vs 多行粘贴），**不检测「Return **之前**字符突发」**。因此：

```
单行文本注入（Char 突发 + 末尾 Return，间隔毫秒级）
  与
手打回车（慢速字符 + 用户按 Enter）
在"Return 后窗内无字符"这一点上完全同构 → 机制无法区分 → 注入末尾的 Return 被当作"用户提交 Enter"
```

**深层问题**：把「一次 Return + 40ms 静默」当作「提交意图」的充分条件——但 Return 在终端环境下来源多样（用户 Enter / 注入换行 / IME 上屏），一律当提交意图是脆弱假设（用户质疑"窗满提交不合理"，成立）。

**为什么长回复时概率高**：长回复 = 用户长时间看屏 + 滚动鼠标 → 误触注入的概率窗口大（用户实证 16:31 在滚轮翻看）。

---

## 四、根因定性

**核心缺陷：提交意图判定不排除"注入上下文"**——「字符突发 + Return → 窗满自动提交」把任何终端注入（粘贴/IME）都变成静默提交，且提交与用户显式 Enter 无一一对应。

**修复目标（用户定案 2026-08-31）**：
1. 粘贴上下文的 Return **不提交**——文本留在输入框（可感知、可删除），零自动请求
2. 粘贴结束后**用户显式再按一次 Enter** 才提交（二次 Enter）
3. 手打回车保持一次提交（窗满提交对手打保留）

---

## 五、修复方案（已实施）

### 5.1 核心：Return 前置突发检测 + 粘贴末尾不提交

在 `CLFPasteCoalescer` 增加"粘贴突发上下文"判定，依据事件到达时间密度：

| 判定输入 | 结论 |
|---|---|
| Return 前 ≤40ms 内有 Char 事件（粘贴批次内，间隔毫秒级） | 粘贴上下文 → 窗满不提交 |
| Return 前 >40ms 无 Char（手打间隔百毫秒级；IME 上屏后按 Enter 亦 >100ms） | 手打 → 窗满提交 |

**状态机改动**（`CLFPasteCoalescer.hpp/cpp`）：

1. `onCharacter` 内更新 `m_lastCharTime = now`（所有字符事件刷新锚点，含 PassThrough）
2. `onReturn(Idle)`：`now - m_lastCharTime <= m_pasteBurstMs`（40ms）→ 置 `m_pendingFromPaste`，照常 `enterPendingLocked` 开窗
3. 定时线程 deadline 分支：窗满且 `m_pendingFromPaste` → **不置 pendingConfirmed**、复位 Idle（粘贴末尾 Return 静默丢弃，文本留在 inputText）；否则置 pendingConfirmed（现状）
4. 窗内有后续事件（多行粘贴中间 Return）→ 照现状转 PasteMode（换行保留，P8 不回归）
5. `cancelPendingLocked` 顺带清 `m_pendingFromPaste`
6. PasteMode 态"静默后回车=真提交"分支无需改动——静默必然 >40ms → 自动走手打判定

**构造参数**：`CLFPasteCoalescer(wakeCb, quietWindowMs=40, pasteBurstMs=40)`——burst 窗独立注入，测试加速。

**效果**：
- 注入残留 → 文本留在输入框、**零自动请求**（N2 断言 wakeCb 零触发）
- 多行粘贴（P8 含空行）→ PasteMode 链路不变 → 末尾不提交 → 用户 Enter 一次提交完整内容
- 手打回车 → 一次提交（体验不变）
- IME Enter 上屏（突发）→ 同判粘贴上下文不提交 → 用户再 Enter 提交（顺带防 IME 场景）
- 极速手打误判的降级 = 多按一次 Enter，无数据丢失

### 5.2 原 F0-F5 方案的处理

初稿的 F0（提交绑定用户 Enter 本身）/F1（busy 吞字符）/F2（busy Enter 无效）/F3（恢复不触发提交）/F4（turn 结束清空）/F5（doSubmit 收口）**不实施**，理由：

- F0 与 P8 粘贴用例时序矛盾（"Enter 即提交"会误提交粘贴首行），且对注入场景无效（注入的 Return 在事件层与用户 Enter 无法区分）
- F1/F2 现状已防"busy 期间提交"（doSubmit 内 busy 拒绝 + pendingConfirmed 已消费）
- F3 配合新机制天然满足（提交只在窗满/Enter 路径，恢复路径无提交入口）
- F4 的"turn 结束清空 inputText"列为**可选增强**：busy 期间注入的残留文本可能跨轮留存，用户无意识 Enter 会提交残留。实现需跨线程（submit 在工作线程，inputText 在 UI 线程局部）——经 `PostEvent(Custom)` + 成员标记在渲染循环清空。视验收观察决定。

---

## 六、验证

### 6.1 单元测试（qa_CLFPasteCoalescer，全部通过）

| 用例 | 覆盖 |
|---|---|
| P1-P10 全部回归（P3 注入 burst=5ms、P7 重写为"粘贴尾换行不提交+二次 Enter 提交"） | 原语义 + 新语义 |
| N1 手打回车（静默 >40ms）→ 一次提交 | 不回归 |
| N2 粘贴末尾 Return → 窗满不提交 + **wakeCb 零触发**（零自动请求） | 核心修复 |
| N3 粘贴末尾不提交后状态复位 Idle，后续字符正常放行 | 状态机健康 |
| N4 多行粘贴（前置突发）中间 Return 转 PasteMode，零中途提交 | 多行粘贴回归 |
| N5/N6 突发判定边界（恰好窗口 → 不提交；窗口+1ms → 提交） | 阈值语义 |

### 6.2 取证（注入源确认，进行中）

设 `CLF_DEBUG_EVENTS=1` 启动，复制文本后依次尝试右键 / Shift+Insert / Ctrl+V，看 `doc/log/clf_events.log`：
- 密集 `Char 'x'` 序列 → 该方式注入生效
- 仅 `Mouse btn=1` 无 Char → 终端层未注入

### 6.3 人工验收清单

1. 注入残留场景：以确认生效的注入方式注入带行尾换行的单行文本 → 文本在输入框、**不自动提交**；按 Enter → 提交一次
2. 手打回车 → 一次提交（无延迟感变化）
3. 粘贴多行文本（含空行）→ 空行保留、末尾不自动提交、Enter 一次提交完整内容
4. 中文 IME 输入 + Enter 上屏 → 不提交；显式 Enter → 提交
5. 模型回复期间（busy）注入 → 不提交；回复结束后输入框残留可手动清
6. 正常对话 10+ 轮 + 长回复 + 滚动鼠标 → 不再出现自问自答
7. 历史导航（↑↓）、Ctrl+D、Ctrl+N、Esc 中断、确认栏 → 无回归
8. 全量 ctest 通过（`qa_CLFSessionManager` 既有环境失败除外）

---

## 七、边界情况

| 情况 | 处理 |
|---|---|
| 极速手打（字符与 Enter 间隔 <40ms，人类极限 ~100ms） | 误判为粘贴 → 不提交，多按一次 Enter，无数据丢失 |
| IME Enter 上屏 | 判粘贴上下文不提交 → 用户再 Enter 提交（顺带修复） |
| 多行粘贴含空行（P8） | PasteMode 链路不变，空行保留，末尾不提交，用户 Enter 一次提交 |
| busy 期间注入 | doSubmit busy 拒绝（现状）→ 残留文本留输入框，不自动提交；F4 增强项视验收 |
| 粘贴末尾 Return 被静默丢弃 | 文本留在输入框（可感知、可删除），不产生任何 API 请求 |
| Ctrl+D 提交 | 独立路径（:631），不受突发检测影响 |
| confirm 激活 | 0b 只复位不提交（现状），不受影响 |

---

## 八、后续

- 注入源取证（§6.2）确认后回填 §2.3
- 全量 ctest + 主程序启动冒烟 + 人工验收（§6.3）
- 验收通过后：文档归档至 `设计/归档/`，bump 版本（v0.4.2 候选）由用户发布
- F4 增强项（turn 后清空输入框）视验收观察决定是否实施
