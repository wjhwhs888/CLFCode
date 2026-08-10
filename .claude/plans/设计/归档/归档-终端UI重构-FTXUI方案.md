# 设计-终端UI重构-FTXUI方案

> 状态：**已完成** | 创建：2026-08-04 | 实施：2026-08-05 | 审查日期：2026-08-07 | 版本：FTXUI v7.0.0
>
> **⚠️ 审查标记**：本文档是实施记录。"实施差异说明"表基本准确，以下为额外不一致：
> - `askSelect/askInput` **已从接口删除**（非"stub 未变"）
> - `CLFConsole` **已彻底删除**（非"简化"），`CLFScrollBuffer` 由 `CLFScrollView` 取代
> - 确认栏为**两选项确认/返回**（非 Button 双按钮），Esc/CtrlC = 拒绝+中断 Agent
> - `confirm` 为**CV 同步等待**（非嵌套 screen.Loop），未切组件树隐藏状态行
> - 以 `> **DEPRECATED:**` 标记的段落均与当前代码不符

## 实施差异说明

| 设计 | 实际实现 | 原因 |
|------|----------|------|
| 嵌套 `screen.Loop()` (confirm) | CV 同步等待 + 底部确认栏 | v7 App::Loop 嵌套不稳定 |
| Modal 弹窗遮罩 | 底部固定栏 (设计 §3.6 原样) | 避免双 UI 渲染撕裂 |
| `m_refreshPending` 防抖 | 已实现 | 一致 |
| `vscroll_indicator \| frame` | 手动滚动偏移 + 鼠标滚轮 + 键盘翻页 | frame 不跟踪新内容，需要 focus 配合 |
| FTXUI v6.1.9 | FTXUI v7.0.0 | v7 修复嵌套 Renderer 光标 + 输入光标定位 |
| CLFAnsi 可删除 | 保留 (颜色辅助方法) | emitContent 自动过滤 ANSI，颜色方法无副作用 |
| 静态兼容层逐步迁移 | 一次性全部删除 | 代码清洁度优先 |
| `emitRaw` 第一期透传 | 实现独立 emitRaw (保留 ANSI) | 提前完成 |
| askSelect/askInput stub | 未变 | 第二期

## 1. 为什么选 FTXUI

### 废弃方案回顾

手写 ANSI + DECSTBM 方案经过 5+ 轮尝试，核心矛盾无法解决：
- `\033[2J` 在 DECSTBM 下不可靠
- 固定区坐标体系与内容区不可调和
- `showThinking` 清除范围覆盖固定区
- 旧固定区残留无法彻底清除

根因：**试图用部分更新（DECSTBM+增量重绘）模拟全帧渲染，但终端不是浏览器 DOM。**

### FTXUI 解决什么

| 痛点 | 手写 ANSI | FTXUI |
|:---|:---|:---|
| 清屏 | `\033[2J`（行为不确定） | 双缓冲自动全帧渲染 |
| 布局 | 手动坐标计算 | `vbox`/`hbox` 声明式 |
| 固定区 | DECSTBM 保护 | 组件树的固定位置 |
| 滚动 | 终端原生（无法控制） | `frame` + `vscroll_indicator` |
| 输入 | `_getch()` 手写 | `Input` 组件原生支持 |
| 线程刷新 | 无保护，直接 `cout` | `screen.Post(Event::Custom)` 线程安全 |
| 依赖 | 无 | FTXUI（MIT，零外部依赖） |

## 2. 架构：FTXUI 嵌入 Harness

```
┌─ ICLFOutput (CLFTypes) ──────────────────┐
│  接口不变: emitContent / setStatus / ...  │
└──────────────────┬───────────────────────┘
                   │ 实现
┌──────────────────┴───────────────────────┐
│  CLFTerminal (CLFUI)                     │
│  ┌─────────────────────────────────────┐ │
│  │  FTXUI 组件树                        │ │
│  │  ┌─ vbox ────────────────────────┐  │ │
│  │  │  scroll_content   (内容+工具)  │  │ │  ← onToolCall/Result 追加到此
│  │  │  separator                    │  │ │
│  │  │  status_line     (瞬时状态)   │  │ │
│  │  │  separator                    │  │ │
│  │  │  input_area      (输入区)     │  │ │
│  │  │  separator                    │  │ │
│  │  │  mode_line       (模式行)     │  │ │
│  │  │  confirm_bar     (确认区)     │  │ │
│  │  └───────────────────────────────┘  │ │
│  └─────────────────────────────────────┘ │
│  ScreenInteractive::Fullscreen()         │
└──────────────────────────────────────────┘
```

**接口不变，只换实现。** ICLFOutput 的 11 个方法全部映射到 FTXUI 组件状态更新。

## 3. 组件设计

### 3.1 内容区（ScrollContent）

```
┌──────────────────────────────────────┐
│ ● CLFCode — CLI Agent Framework...   │  ← banner
│ > 你是谁                              │  ← user query
│ ● CLFCode: 我是 CLFCode，一个...      │  ← AI response
│ ...                                   │
│   Thought for 2s                      │  ← thought mark
└──────────────────────────────────────┘
         ↑ frame | vscroll_indicator
```

FTXUI 实现：
```cpp
auto content = Renderer([&] {
    Elements lines;
    // 传全部行, FTXUI 的 frame 组件自动裁剪可见部分 (无需手动计算视口)
    for (auto& line : m_contentBuffer)
        lines.push_back(text(line));
    return vbox(lines) | vscroll_indicator | frame | flex;
});
```

**ICLFOutput 映射**：
```cpp
void CLFTerminal::emitContent(const std::string& text) {
    { std::lock_guard lock(m_mutex); m_contentBuffer.push_back(text); }
    if (!m_refreshPending.exchange(true))  // 同帧合并, 首次Post后续不重复
        screen.Post(Event::Custom);
}
```
- `emitRaw(data)` → 同上透传（注：FTXUI 默认转义 ANSI码，需验证透传方案）

### 3.2 状态行（StatusLine）

```
· Thinking… (3s)                         ← showThinking
第 2/5 步: 运行测试...                    ← setStatus with cur/total
```

```cpp
auto status = Renderer([&] {
    if (status_text.empty()) return emptyElement();
    return text("  " + status_text) | dim;
});
```

**ICLFOutput 映射**：
- `setStatus(title, cur, total)` → 更新 `status_text` 变量

### 3.3 工具反馈（ToolFeedback）

工具输出（`● tool_name`、`⎿ ✓/✗`）直接追加到 `scroll_content` 的内容 buffer，**不设独立固定组件**。防止工具调用多时独立组件膨胀、把输入区挤出屏幕（布局漂移）。

**ICLFOutput 映射**：
- `onToolCall(name, params)` → `content_buffer.push("● " + name + "(" + params + ")")`
- `onToolResult(name, result, ok)` → `content_buffer.push("  ⎿ " + (ok ? "✓" : "✗") + " " + result)`

### 3.4 输入区（InputArea）

布局约束: 默认 1 行 + 水平滚动; Shift+Enter 换行扩展（与当前行为一致），Enter 提交。

```
> 用户输入的内容█                          ← Input 组件
```

```cpp
InputOption opt;
opt.multiline = true;  // Shift+Enter 换行
auto input = Input(&input_text, "> ", opt);
```

### 3.5 模式行（ModeLine）

```
  edit mode on    shift+tab to cycle · esc to interrupt · /help for help
```

```cpp
auto mode = Renderer([&] {
    return hbox({
        text("  " + mode_name + " mode on"),
        filler(),
        text("shift+tab to cycle · esc to interrupt · /help for help"),
    }) | dim;
});
```

### 3.6 确认区（ConfirmBar）

```
[●] 确认    [ ] 取消                       ← Toggle 组件
```

```cpp
auto confirm = Container::Horizontal({
    Button("确认", [&] { result = true; screen.ExitLoopClosure()(); }),
    Button("取消", [&] { result = false; screen.ExitLoopClosure()(); }),
});
```

## 4. ICLFOutput → FTXUI 映射表

| ICLFOutput 方法 | FTXUI 实现 |
|:---|:---|
| `emitContent(text)` | `content_buffer.push(text)` → `screen.Post(Event::Custom)` |
| `emitRaw(data)` | 同上透传 (注: FTXUI 默认转义 ANSI码; 如需原始透传需验证 `raw_text` 或退到 `std::cout`) |
| `setStatus(title, cur, total)` | `status_text = format(title, cur, total)` → `Post` |
| `onToolCall(name, params)` | `content_buffer.push("● "+name+"("+params+")")` → `Post` (追加到滚动内容区) |
| `onToolResult(name, result, ok)` | `content_buffer.push(format(result))` → `Post` (同上, 不独立组件, 防布局漂移) |
| `confirm(prompt)` | 阻塞: 嵌套 `screen.Loop(confirm_ui)` → `ExitLoopClosure()` 返回, 不栈溢出 |
| `askSelect(opts, prompt)` | 渲染 Menu 组件 → `screen.Loop(menu_ui)` → 返回索引 |
| `askInput(prompt, def)` | 渲染 Input 组件 → `screen.Loop(input_ui)` → 返回文本 |
| `onInterrupt(cb)` | `screen.Post(Event::Special({ESC}))` 触发回调 |
| `emitError(msg)` | `error_log.push(msg)` → `Post`; 渲染: `text(msg) \| color(Color::Red)` |
| `requestShutdown(reason)` | `shutdown_requested = true` + cerr 输出 |

## 5. 渲染循环

### 5.1 刷新策略：事件驱动 + 节流（非固定帧率）

不用后台线程轮询。`emitContent` 被调用时按需 Post，合并同一帧内的多次调用：

```cpp
void CLFTerminal::emitContent(const std::string& text) {
    {
        std::lock_guard lock(m_mutex);
        m_contentBuffer.push_back(text);
    }
    // 按需刷新, 不空转
    if (!m_refreshPending.exchange(true))
        screen.Post(Event::Custom);
}
```

FTXUI 事件循环处理 Custom 事件时：渲染一帧，然后 `m_refreshPending = false`。如果流式爆发（1ms 内 10 个 chunk），`exchange(true)` 只第一次返回 false，后续调用不重复 Post——自动合并到同一帧。

> **为什么不用后台线程 sleep(16ms)？** 流式输出不均匀：有时 300ms 无内容（空转 18 帧），有时 1ms 来 10 个 chunk（挤在同一帧）。事件驱动没有空转，爆发时自动合并。

### 5.2 主循环

```cpp
auto screen = ScreenInteractive::Fullscreen();

auto component = Renderer(main_container, [&] {
    return vbox({
        scroll_content(),
        separator(),
        status_line(),
        separator(),
        input_area(),
        separator(),
        mode_line(),
        confirm_bar(),
    });
});

screen.Loop(component);  // 阻塞, FTXUI 全权接管输入+渲染
```

## 6. CLFRepl 改造：方案A — FTXUI Loop 驱动一切

FTXUI 的 `screen.Loop(component)` 是阻塞事件循环。CLFRepl 原有主循环（`while(!m_exit) { readKey; process; draw; }`）需要迁移到 FTXUI 事件回调中。

**改造方式：**

```cpp
// CLFRepl::run() — 改造后
int CLFRepl::run() {
    CLFTerminal::initLayout(m_dispatcher->modeName());
    printBanner();

    // 构建 FTXUI 组件树 (输入区+内容区+状态区)
    auto root = buildComponentTree();

    // FTXUI 接管: 输入由 Input 组件处理, 渲染自动
    auto screen = ScreenInteractive::Fullscreen();
    screen.Loop(root);  // 阻塞, 直到 ExitLoopClosure
    return 0;
}
```

**用户输入处理**：FTXUI 的 `Input` 组件捕获按键。Enter 提交时触发回调 → 调用现有 `submit(input)` 逻辑 → Agent 流式回调 → `emitContent` Post 重渲染。

**模式切换**：Shift+Tab 绑定到 Input 组件的 `on_key` 事件 → 调用现有 `cycleMode()` → 更新 mode_line 状态变量 → 自动重渲染。

**退出**：Ctrl+C → `screen.ExitLoopClosure()()` → `screen.Loop` 返回 → `restoreScrollRegion()` → 正常退出。

**斜杠命令迁移**：现有 `CLFCommandDispatcher` 的 if-else 链（`/help /history /exit /clear /model /mode /config /skill`）直接在 Input 提交回调中拦截：
```cpp
if (input_text[0] == '/') {
    if (input_text == "/exit") { screen.ExitLoopClosure()(); return; }
    if (m_dispatcher->handle(input_text)) return;  // 复用现有调度
}
submit(input_text);  // 非命令 → Agent
```
`CLFCommandDispatcher` 本身无需修改（只做调度，输出通过已有的 `ICLFOutput` 接口）。

**setRepl 弃用**：FTXUI 下 `confirm` 在 CLFTerminal 内部开嵌套 Loop，不再需要反向引用 `CLFRepl*`。`setRepl()` 改为空操作，`m_repl` 成员可移除。

**ScreenInteractive 注入**（D1）：`CLFRepl::run()` 创建 `ScreenInteractive` 对象，通过 `CLFTerminal::setScreen(&screen)` 注入。`confirm`/`askSelect`/`askInput` 的嵌套 Loop 需要访问同一个 screen 实例。

**confirm 期间隐藏状态行**（D3）：嵌套 Loop 的组件树只包含 `confirm_bar`，不包含 `status_line`/`mode_line`——切换组件树自然实现"确认时隐藏状态"。

**confirm 嵌套事件循环**（R3）：`CLFTerminal::confirm()` 内部：
```cpp
bool CLFTerminal::confirm(const std::string& prompt) {
    auto confirm_ui = buildConfirmUI(prompt);  // Button("确认") + Button("取消")
    bool result = false;
    auto inner = Renderer(confirm_ui, [&] { ... });
    screen.Loop(inner);           // 嵌套 Loop, 阻塞等待选择
    // 用户点击 Button → 回调设置 result + screen.ExitLoopClosure()()
    return result;                // 返回主 Loop
}
```
FTXUI 支持嵌套 Loop。`ExitLoopClosure()` 退出内层 Loop 返回主循环，不会栈溢出。

## 7. 与现有代码的关系

| 模块 | 影响 |
|:---|:---|
| `CLFTypes/ICLFOutput.hpp` | **不动** |
| `CLFCore/` | **不动** |
| `CLFNetwork/` | **不动** |
| `CLFTools/` | **不动** |
| `CLFUI/CLFTerminal.hpp/.cpp` | **重写**（FTXUI 组件替代手写 ANSI） |
| `CLFUI/CLFAnsi` | **可删除**（FTXUI 内置颜色） |
| `CLFUI/CLFScrollBuffer` | **保留**（内容缓冲逻辑不变） |
| `CLFUI/CLFConsole` | **简化**（输入由 FTXUI Input 处理） |
| `CLFUI/CLFRepl` | **小改**（主循环改为 FTXUI Loop） |
| `CMakeLists.txt` | 加 `ftxui::screen ftxui::dom ftxui::component` |

## 7. 实施步骤

**Step 0（实施前验证）**：emitRaw ANSI 透传测试（10分钟）——独立程序验证 `text("\x1b[32mHello\x1b[0m")` 是否显示绿色、`std::cout << ANSI` 与 FTXUI 全屏是否冲突。决定 emitRaw 的最终实现方案。

| # | 内容 | 预计改动 |
|:---|:---|:---|
| 1 | CMake 集成 FTXUI（FetchContent） | +3 行 |
| 2 | 写 `CLFTerminalFTXUI` 类（实现 ICLFOutput） | ~200 行 |
| 3 | 组件树搭建（6 区布局） | ~150 行 |
| 4 | ICLFOutput 方法→组件状态映射 | ~100 行 |
| 5 | CLFRepl 主循环改为 FTXUI Loop | ~20 行 |
| 6 | 删除 CLFAnsi、简化 CLFConsole | -100 行 |
| 7 | 构建 + 手动测试 | — |

## 8. 风险

| 风险 | 缓解 |
|:---|:---|
| FTXUI 学习曲线 | API 简洁，示例丰富 |
| 流式输出性能 | `Post(Event::Custom)` + 60fps 节流 |
| 中文/CJK 宽度 | FTXUI v5+ 支持全角字符 |
| Windows Terminal 兼容 | FTXUI 官方支持 Windows |
| 构建时间增加 | FTXUI header-only 为主，增量编译影响小 |

## 9. 验证

1. 构建通过
2. 启动：banner + 固定区，无空白
3. 流式输出实时刷新
4. 输入区正常编辑（中文、多行、光标移动）
5. Shift+Tab 模式切换
6. 确认对话框正常
7. 历史滚动（终端原生 scrollback 保留）
