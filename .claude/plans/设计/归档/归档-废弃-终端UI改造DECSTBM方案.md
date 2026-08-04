# 终端 UI 改造设计

> 状态：Phase 1/3/4 完成 + 架构重构完成 | 最后更新：2026-08-01
>
> 目标：对齐 Claude Code 交互体验
>
> **本次实现摘要**：
> - Phase 1 思考标记 ✅ | Phase 3 状态栏改造 ✅ | Phase 4 轮尾标记 ✅
> - 🆕 DECSTBM 滚动区重构（替代纯顺序模型）
> - 🆕 CLFConsole `_getch()` 重写（Windows 方向键 + 中文输入）
> - 🆕 `Shift+Tab` 模式切换 + ESC 流式打断
> - 🆕 `finish_reason: 'length'` 支持

---

## 1. 当前问题诊断

### 1.1 架构层面

```
当前模型：纯顺序流（Pure Sequential Output）

  scrollPrint() ──→ std::cout << text   ← 一行行追加
  drawInputArea() ──→ 临时绘制固定区      ← 每次交互重绘 5 行（\n\n + 上线 + ❯ + 下线 + 模式）
  submit() ──→ toContentArea()          ← 清掉固定区 3 行，内容顶上
  redrawAll() ──→ 从缓冲重建              ← 缩放后重绘

问题：
  a) 固定区每次"绘制 → 清除 → 重绘"循环，终端闪烁
  b) 流式输出期间状态区更新直接塞入内容流，视觉层次混乱
  c) 缓冲区无限增长，无裁剪策略
  d) 静态类 + 全局状态，无法单测
```

### 1.2 交互层面（vs Claude Code）

| 场景 | Claude Code | CLFCode 当前 | 差距 |
|------|-------------|-------------|------|
| AI 思考中 | `Thought for 3s (ctrl+o to expand)` | 无 | 用户不知道 Agent 在干什么 |
| 工具调用 | `● Write(文件) ⎿ Wrote N lines` + diff + 行号 | `● Write(文件)` 纯文本 | 看不到具体改动 |
| 工具执行中 | 状态区显示当前操作 | 无 | 不知道工具执行进度 |
| 轮间等待 | `✻ Baked for 8s` | 无 | 长间隔显得卡顿 |
| 状态栏 | `⏸ manual mode on · ? for shortcuts · ← for agents` | `▍ 模式: edit Ctrl+N 切换 \| /help` | 信息密度低 |
| 错误提示 | `✘ Auto-update failed · Run claude doctor` | 无 | 错误不可见 |

### 1.3 代码层面

```
CLFTerminal 职责过载：
  ├── ANSI 颜色封装（green/cyan/yellow/...）    ← 合理
  ├── 树状输出（item/sub/ok/fail/...）          ← 合理
  ├── 5 区布局（initLayout/drawXxx/...）        ← 复杂，与上两层耦合
  ├── 滚动缓冲（s_scrollBuffer）               ← 内存管理缺失
  ├── 光标定位（moveCursor/clearLine）          ← 与布局耦合
  └── 窗口缩放重绘（redrawAll）                 ← 与缓冲耦合

问题：
  - 静态类所有状态全局共享（s_xxx），无法实例化
  - scrollPrint 同时做缓冲+输出，职责不清
  - drawInputArea 首次绘制/更新两套逻辑，容易出 bug
  - 固定区行号计算（H-8, H-6, ...）在顺序模型中已失效但未清理
```

---

## 2. 目标状态

### 2.1 视觉目标

```
┌─ 滚动内容区 ───────────────────────────────────────────────────┐
│                                                                 │
│  ❯ 帮我生成一个html文件，命名为五子棋，内容为空                  │  ← 用户输入回显
│                                                                 │
│    Thought for 3s (ctrl+o to expand)                            │  ← 思考过程（可展开）
│                                                                 │
│  ● Write(五子棋.html)                                           │  ← 工具调用头
│    ⎿  Wrote 10 lines to 五子棋.html                              │  ← 结果摘要
│        1 <!DOCTYPE html>                                        │  ← 上下文（灰色）
│        2 <html lang="zh-CN">                                    │
│        ...                                                      │
│                                                                 │
│  ● 已生成 五子棋.html，内容为空                                  │  ← Agent 回复
│                                                                 │
│  ✻ Baked for 8s                                                 │  ← 轮尾标记
│                                                                 │
│  ❯ 写一个文件开头进去                                            │  ← 下一轮输入
│    Thought for 7s, read 1 file (ctrl+o to expand)               │
│                                                                 │
│  ● "文件开头"具体指什么内容？                                    │  ← Agent 追问
│                                                                 │
│  ❯ 页面标题                                                     │  ← 用户回复
│    Thought for 1s                                                │
│                                                                 │
│  ● Update(五子棋.html)                                          │  ← 增量编辑
│    ⎿  Added 1 line                                              │
│        6      <title>五子棋</title>                             │  ← 上下文（无标记）
│        7  </head>                                               │
│        8  <body>                                                │
│        9 +    <h1>五子棋</h1>                                   │  ← 新增行（绿 + 加号）
│       10  </body>                                               │
│                                                                 │
│  ● 已添加，<body> 中现在有一个 <h1>五子棋</h1> 页面标题。        │
│                                                                 │
│  ✻ Sautéed for 5s                                               │
│                                                                 │
│  ─────────────────────────────────────────────────────────────  │  ← 上分隔线
│  ❯                                                              │  ← 输入行
│  ─────────────────────────────────────────────────────────────  │  ← 下分隔线
│    ⏸ manual mode on · ? for shortcuts · ← for agents           │  ← 状态栏（2空格缩进）
│    ✘ Auto-update failed · Run claude doctor                     │  ← 错误提示
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 新增的视觉元素

| 元素 | 格式 | 触发时机 | ANSI 色 |
|------|------|----------|---------|
| 思考过程 | `  Thought for {N}s (ctrl+o to expand)` | 每轮 API 调用前/后 | 灰色 `\033[90m` |
| 思考过程（含文件数） | `  Thought for {N}s, read {M} files (ctrl+o to expand)` | 工具执行后 | 灰色 |
| 轮尾标记 | `✻ {verb}ed for {N}s` | 每轮完成后 | 灰色 |
| 工具调用（写） | `● Write(文件名)` | 写文件工具 | 默认色 |
| 工具调用（读） | `● Read(文件名)` | 读文件工具 | 默认色 |
| 工具调用（编辑） | `● Update(文件名)` | 增量编辑 | 默认色 |
| 工具调用（命令） | `● Bash(命令摘要)` | 命令执行 | 默认色 |
| 结果摘要 | `  ⎿  Wrote {N} lines to 文件名` | 工具返回后 | 灰色 |
| diff 新增行 | `{行号} +    {内容}` | 增量编辑 | 绿色 `\033[32m` |
| diff 上下文行 | `{行号}      {内容}` | 增量编辑 | 灰色 `\033[90m` |
| 状态栏 | `  ⏸ {模式} on · ? for shortcuts · ← for agents` | 始终 | 灰色 |
| 错误提示 | `  ✘ {错误信息}` | 异常时 | 红色 `\033[31m` |

### 2.3 动态动词表（轮尾标记）

```cpp
// 按操作类型选择动词，形如 "✻ Baked for 8s"
const char* verbForAction(ToolAction action) {
    switch (action) {
        case Write:  return "Baked";
        case Read:   return "Crumbed";
        case Edit:   return "Sautéed";
        case Shell:  return "Braised";
        case Search: return "Churned";
        default:     return "Thought";
    }
}
```

---

## 3. 实现方案

### 3.1 阶段划分

```
Phase 1 ── 思考过程标记 "Thought for Xs"           ← 最小改动，最大收益
Phase 2 ── 工具调用显示升级（diff + 行号）          ← 核心体验对齐
Phase 3 ── 状态栏改造（缩进 + 多行 + 错误位置）      ← 视觉打磨
Phase 4 ── 轮尾标记 "✻ Baked for Xs"              ← 锦上添花
Phase 5 ── 架构重构（CLFTerminal 拆分）             ← 为长期可维护性
```

### 3.2 Phase 1：思考过程标记

**目标**：流式输出开始前显示 `Thought for {N}s`，结束后显示耗时。

**改动点**：

```
CLFTerminal 新增方法：
  static void thoughtStart();           // 输出 "  Thought..." 并启动计时
  static void thoughtEnd(int filesRead = 0); // 计算耗时并完成行
  static void roundEnd(const std::string& verb); // 输出 "✻ {verb}ed for {N}s"

CLFRepl::submit() 注入：
  submit() {
      toContentArea();
      echoInput();
      CLFTerminal::thoughtStart();      // ← 新增
      response = m_agent.runTurn(input);
      CLFTerminal::thoughtEnd(...);     // ← 新增
      CLFTerminal::roundEnd(...);       // ← 新增
  }
```

**状态**：不影响滚动缓冲（不进 scrollBuffer），纯显示层。

### 3.3 Phase 2：工具调用 diff 展示

**目标**：工具执行结果展示改动内容和行号。

**当前**：
```
CLFAgentLoop::executeTools() → m_statusCallback(title, content)
  → CLFTerminal::drawStatusArea(title, content)
    → 纯文本输出，无结构化
```

**改为**：
```
新增回调：setToolResultCallback(toolName, result)
  → CLFTerminal::drawToolResult(name, result)
    → 按工具类型分发：
      - read_file  → 显示读取字节数
      - write_file → 显示写入行数 + 内容预览
      - edit_file  → 显示 diff (+/- 行号、上下文)
      - execute_command → 显示 stdout/stderr
```

**diff 渲染逻辑**（edit_file 专用）：
```
输入：原始文件内容 + 新文件内容
输出：
  ● Update(文件名)
    ⎿  Added N lines / Removed M lines
        {行号}      {上下文行}
        {行号} +    {新增行}       ← 绿色
        {行号} -    {删除行}       ← 红色
        {行号}      {上下文行}
```

**注意**：CLFCode 目前没有 `edit_file` 工具（只有 `write_file` 全量覆写），diff 展示先用于 `write_file` vs 空文件的对比。后续增加增量编辑工具后再接真实 diff。

### 3.4 Phase 3：状态栏改造

**当前**：
```
▍ 模式: edit                    Ctrl+N 切换 | /help
```

**目标**：
```
  ⏸ edit mode on · ? for shortcuts · ← for agents
  ✘ {错误信息}                                     ← 有错误时显示
```

**改动**：
```
CLFTerminal::drawStatusBar(mode, errors)
  → 2 空格缩进
  → 左侧：⏸ {mode} mode on（灰色）
  → 中间：· ? for shortcuts · ← for agents（灰色）
  → 右侧：错误提示（红色，有则显示）

drawInputArea 调整：
  首次绘制流程（从上到下）：
    \n\n                        ← 让位
    ───...───                    ← 上分隔线（浅蓝）
    ❯ {text}                    ← 输入行
    ───...───                    ← 下分隔线（浅蓝）
      ⏸ edit mode on · ...      ← 状态栏（2空格缩进，灰色）
```

### 3.5 Phase 4：轮尾标记

**目标**：每轮完成时显示 `✻ Baked for 8s`。

**实现**：
```
CLFTerminal::roundEnd(verb, seconds)
  → scrollPrint(gray("✻ " + verb + "ed for " + std::to_string(seconds) + "s") + "\n");
```

**动词选择**：根据本轮是否涉及工具调用、工具类型自动选择。无工具调用的纯对话轮次用 "Thought"。

### 3.6 Phase 5：架构重构（远期）

**目标**：将 CLFTerminal 从静态全局类拆分为可实例化的组件。

```
CLFTerminal (静态工具类，颜色/控制码)     ← 保留，不变
  ├── CLFDisplay (实例，管理缓冲+输出)    ← 新增，替代 scrollBuffer 相关
  ├── CLFInputArea (实例，管理输入区绘制)  ← 新增，替代 drawInputArea
  └── CLFStatusBar (实例，管理状态栏)     ← 新增，替代 drawStatusArea/drawModeArea
```

这个阶段不急于做，等前 4 个 phase 稳定后，确认了正确的抽象边界再动手。

---

## 4. 回调体系调整

当前 CLFAgentLoop 有两个回调：
```cpp
setConfirmCallback(...)   // 高风险确认
setStatusCallback(...)    // 工具执行状态
```

需要新增：
```cpp
setToolResultCallback(toolName, result)     // Phase 2：工具执行结果（含 diff）
setThinkingCallback(phase, ...)             // Phase 1：思考阶段变更
```

回调触发点和数据流：

```
CLFAgentLoop::runTurn()
  │
  ├─→ thinkingCallback("start")           ← Phase 1：准备发 API 请求
  ├─→ API 请求...
  ├─→ thinkingCallback("streaming")        ← Phase 1：收到首个 token
  │
  ├─→ [如有 tool_calls]
  │   ├─→ statusCallback(tool, args)      ← Phase 2：开始执行工具
  │   ├─→ 工具执行...
  │   └─→ toolResultCallback(name, result) ← Phase 2：展示结果
  │
  └─→ thinkingCallback("end", elapsed)     ← Phase 1：完成
```

---

## 5. 滚动缓冲区裁剪

当前问题：`s_scrollBuffer` 无限增长。

**方案**：限制最大行数（默认 10000 行），超出时从头部丢弃。

```cpp
constexpr int kMaxBufferLines = 10000;

void CLFTerminal::scrollPrint(const std::string& text) {
    // ... 追加到 s_scrollBuffer ...
    while (s_scrollBuffer.size() > kMaxBufferLines) {
        s_scrollBuffer.erase(s_scrollBuffer.begin());
    }
    // ... 输出到终端 ...
}
```

---

## 6. 风险与约束

| 风险 | 影响 | 应对 |
|------|------|------|
| ANSI 序列与终端兼容性 | Ctrl+O 折叠功能不可用 | 折叠使用纯文本缩进替代 ANSI 光标移动 |
| diff 计算成本 | 大文件比较卡顿 | 限制 diff 上下文行数（前后各 3 行） |
| 思考标记与流式输出竞态 | Timing 不准 | 计时在主线程做，不受流式 chunk 到达时间影响 |
| 中文终端宽度计算 | 某些字符宽度判断失准 | 保持当前 `textWidth()` 逻辑，后续引入 ICU/utf8proc |
| 缓冲区裁剪丢历史 | `redrawAll` 看不到早期内容 | 可接受：终端本来就不能无限回滚 |

---

## 7. 实施计划

| Phase | 内容 | 预估改动量 | 依赖 |
|-------|------|:---:|------|
| 1 | 思考过程标记 | ~30 行 | 无 |
| 2 | 工具调用 diff | ~80 行 | Phase 1 |
| 3 | 状态栏改造 | ~50 行 | 无（可并行） |
| 4 | 轮尾标记 | ~20 行 | Phase 1 |
| 5 | 架构重构 | ~200 行 | Phase 1-4 全部完成 |

建议顺序：**Phase 1 → Phase 2 → Phase 3 → Phase 4**（Phase 5 视稳定情况而定）

---

## 8. 实施记录（2026-08-01）

### 8.1 已完成项

#### Phase 1：思考过程标记 ✅
- `CLFTerminal::thoughtMark(int seconds)` — 输出 `Thought for {N}s`（灰色）
- `CLFRepl::submit()` 中 `runTurn` 前后 `steady_clock` 计时
- 前导 `\n` 防止与流式输出末行粘连

#### Phase 3：状态栏改造 ✅
- `Ctrl+N` → `Shift+Tab` 模式切换（`CLFKey::ShiftTab`）
- 状态栏格式：`  edit mode on · shift+tab to cycle · esc to interrupt · ? for help`
- `CLFConsole::readKey()` Windows 端 `_getch()` 重写（Tab + `GetKeyState(VK_SHIFT)` 检测）
- `drawModeArea` 用 `\0337`/`\0338`（DECSC/DECRC）保存/恢复光标，确认过程中切换模式也能正确归位

#### Phase 4：思考耗时标记 ✅
- 与 Phase 1 合并实现：`thoughtMark` 即为轮尾标记

#### 🆕 ESC 流式打断 ✅
- `CLFConsole::checkEscape()` — 非阻塞 `_kbhit()` + `_getch()` 检测 ESC
- `CLFAgentLoop::runTurn()` 流式回调中每 SSE 行前检查 → 检测到 ESC 立即 `markDone` + 输出 `⏹ 已中断`
- `CLFRepl::submit()` 识别 `[Interrupted]` 返回值，不重复打印

#### 🆕 DECSTBM 滚动区重构（替代纯顺序模型）✅
- 滚动区：`\033[1;{H-5}r`（顶部内容区独立滚动）
- 固定区：底部 5 行（blank + 上分隔线 + 输入行 + 下分隔线 + 状态栏）
- `scrollPrint` 输出自动约束在滚动区内，固定区始终可见
- `drawInputArea` 用 CUP 绝对定位更新输入行，不碰滚动区
- `toContentArea` 不再清除输入区，只移动光标到内容区

#### 🆕 CLFConsole Windows 键盘输入重写 ✅
- `_getch()` 替代 `std::cin.get()`（无 C++ 流缓冲、无 VT 翻译干扰）
- 方向键：`0xE0`/`0x00` 前缀 + 扫描码识别
- 中文 UTF-8：逐字节读取 + `readUtf8Char()` 拼装
- Enter：`\r`（0x0D）→ `CLFKey::Enter`

### 8.2 遇到的问题与解决

#### 问题 1：方向键确认对话框中不响应
- **现象**：`_kbhit()` + `std::cin.get()` 在不同层面工作——`_kbhit` 查 INPUT_RECORD 队列，`std::cin` 读字符缓冲，不同步
- **尝试**：`ENABLE_VIRTUAL_TERMINAL_INPUT` → 干扰 Enter 和普通字符；`ReadConsoleInputW` → `uChar=0` 导致字符丢失；`WaitForSingleObject` → 同样同步问题
- **最终方案**：`_getch()` 直接读控制台——方向键走 0xE0 前缀协议，普通键返回 ASCII，无需任何 VT 标志

#### 问题 2：流式输出时输入区消失
- **现象**：原顺序模型下 `toContentArea()` 清除输入区，流式期间输入区不可见
- **方案**：DECSTBM 滚动区——内容区独立滚动，底部固定区从不清除
- **细节**：`drawInputArea` 不再 `resetScrollRegion/setScrollRegion`，仅用 CUP 绝对定位；`drawModeArea` 用 DECSC/DECRC 保存恢复光标

#### 问题 3：确认过程中 Shift+Tab 切换模式后界面混乱
- **现象**：`drawModeArea` 硬编码"回到输入行"，但确认时光标在滚动区，回错位置导致后续输出覆盖输入区
- **方案**：`\0337`（DECSC）保存光标 → 更新模式行 → `\0338`（DECRC）恢复光标，不假设光标位置

#### 问题 4：光标始终在终端顶部
- **现象**：`initLayout` 后光标在 `\033[H`（内容区顶部），`drawInputArea` 不移动
- **排查**：`resetScrollRegion/setScrollRegion` 配对在某些终端上有副作用
- **方案**：删除 `drawInputArea` 更新路径中的滚动区重置/恢复，纯 CUP 定位

#### 问题 5：`finish_reason: 'length'` 被当错误
- **方案**：`isValidFinish` 添加 `"length"` 为有效结束原因
- **附带**：`max_tokens` 8192→16384（生成代码场景容易截断）

### 8.3 待完成

| Phase | 内容 | 状态 |
|-------|------|:---:|
| 2 | 工具调用 diff 展示（行号 + 新增/删除标记） | 待做 |
| 5 | CLFTerminal 架构拆分（不再紧迫，当前结构已稳定） | 远期 |

### 8.4 技术笔记

#### DECSTBM 滚动区布局
```
┌─ 滚动区 1 ～ H-5 ────────┐  ← scrollPrint 输出
│  内容独立滚动             │
├─ 固定区 H-4 ～ H ────────┤
│  (空白间隔)               │
│  ─── 上分隔线 ───        │  ← lightBlue
│  ❯ 输入行                │
│  ─── 下分隔线 ───        │  ← lightBlue
│    mode on · hints        │  ← gray, 2空格缩进
└───────────────────────────┘
```
- 滚动区外画固定区前需 `\033[r`（resetScrollRegion）临时禁用
- 画完恢复 `\033[1;H-5r`
- CUP（`\033[row;colH`）不受滚动区约束，可直接定位到任意行

#### Windows _getch() 按键映射
| 输入 | `_getch()` 返回值 | CLFKey |
|------|-------------------|--------|
| ↑ | 0xE0, 0x48 | Up |
| ↓ | 0xE0, 0x50 | Down |
| ← | 0xE0, 0x4B | Left |
| → | 0xE0, 0x4D | Right |
| Enter | \r (0x0D) | Enter |
| Backspace | \b (0x08) | Backspace |
| Esc | \x1B | Esc |
| Tab+Shift | \t (0x09) + GetKeyState(VK_SHIFT) | ShiftTab |
| Ctrl+C | 0x03 | CtrlC |
| 中文 | UTF-8 lead byte → readUtf8Char 拼装 | Char |
