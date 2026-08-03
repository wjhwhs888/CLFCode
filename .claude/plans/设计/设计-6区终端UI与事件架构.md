# CLFCode 6 区终端 UI + 事件架构设计

## 一、设计目标

将当前"过程调用"模式重构为"事件驱动 + 区域独立渲染"模式：
- 6 个独立渲染区域，各自响应专属 dirty flag
- 统一事件通道，触发源与渲染解耦
- 帧末批量渲染，避免重复绘制
- 终端兼容性分析 + 线程安全保证

---

## 二、区域规格

### 布局总览（自底向上）

```
 行号
┌──────────────────────────────────────────────────────┐
│ ⑦ ContentRegion  滚动区   DECSTBM(1, cb)              │
│    纯显示器：只做 ANSI 格式调整 + 宽度重排              │
│    dirty: DIRTY_CONTENT                               │
├──────────────────────────────────────────────────────┤
│ ⑥ StatusRegion   状态区   上分隔线上方                 │
│    1行基线: "· Thinking… (3s)"                        │
│    树形展开: "ph1> 🆕 xxx" 最多 10 行                  │
│    dirty: DIRTY_STATUS                                │
├──────────────────────────── 上分隔线 ────────────────┤
│ ⑤ InputRegion    输入区   下边界固定，上边界随换行上移  │
│    行数: 1 + count(\n)                                │
│    dirty: DIRTY_INPUT                                 │
├──────────────────────────── 下分隔线 ────────────────┤
│ ④ ModeLine       模式区   1 行                         │
│    "  auto mode on · shift+tab to cycle · /help"      │
│    dirty: DIRTY_MODE                                  │
├──────────────────────────────────────────────────────┤
│ ③ ConfirmRegion  确认区   1 行                         │
│    空 / "[●]确认  [ ]取消"                             │
│    dirty: DIRTY_CONFIRM                               │
└──────────────────────────────────────────────────────┘

② LayoutEngine: 横跨所有区域，计算边界
① EventDispatcher: 事件 → dirty flag → 帧末渲染
```

### 各区尺寸公式

```
H = 终端总行数
W = 终端总列数
N = InputRegion 行数 = 1 + count(\n in input text)
M = StatusRegion 行数 = 1 + statusTree.size()  (max 10)

固定区行数 = 2(分隔线) + 1(ModeLine) + 1(ConfirmRegion) + N + M
           = 4 + N + M

ContentBottom = H - (4 + N + M)   ← DECSTBM 滚动区下边界
if ContentBottom < 3: ContentBottom = 3  (最小 3 行滚动区)

行位置:
  contentRow(1)  = 1
  contentRow(cb) = ContentBottom
  
  statusRow(0)       = ContentBottom + 1        (状态区第1行)
  statusRow(M-1)     = ContentBottom + M
  上分隔线            = ContentBottom + M + 1
  inputRow(0)        = ContentBottom + M + 2    (输入区第1行)
  inputRow(N-1)      = ContentBottom + M + 1 + N
  下分隔线            = ContentBottom + M + N + 2 = H - 2
  modeRow             = H - 1
  confirmRow          = H
```

---

## 三、事件系统设计

### Event 定义

```cpp
// CLFEvent.hpp
enum class EventType {
    None = 0,
    
    // 输入
    KeyPress,          // { char, CLFKey, shift/ctrl/alt }
    
    // 内容 (来自 API / 工具)
    ContentAppend,     // { text }  → DIRTY_CONTENT
    ContentNewline,    // { }       → DIRTY_CONTENT (换行)
    ContentThought,    // { seconds } → DIRTY_CONTENT
    
    // 状态 (来自 Thinking / 工具执行)
    StatusThinking,    // { seconds } → DIRTY_STATUS
    StatusClear,       // { } → DIRTY_STATUS
    StatusTask,        // { title, elapsed, phase_list } → DIRTY_STATUS
    
    // 布局
    InputChanged,      // { } → DIRTY_INPUT + 可能 DIRTY_LAYOUT
    LayoutResize,      // { width, height } → DIRTY_LAYOUT
    ModeChanged,       // { mode_name } → DIRTY_MODE
    
    // 确认
    ConfirmShow,       // { prompt, options } → DIRTY_CONFIRM
    ConfirmHide,       // { } → DIRTY_CONFIRM
    
    // 系统
    AppExit,           // { }
};

struct Event {
    EventType type = EventType::None;
    std::string text;
    std::string arg1, arg2;
    int i1 = 0, i2 = 0;
    std::vector<std::string> tree;  // StatusTask 的树形行
};
```

### 事件队列

```cpp
class EventQueue {
    static constexpr int kMaxEvents = 256;
    std::array<Event, kMaxEvents> m_ring;
    std::atomic<int> m_write{0};
    int m_read = 0;  // 仅主线程读
    
public:
    bool push(const Event& e);   // 线程安全 (Thinking / HTTP 回调线程调用)
    bool empty() const;
    Event pop();                 // 仅主线程调用
    
    // 合并优化: 连续 ContentAppend 合并为一个
    void coalesce();
};
```

### dirty 映射

```cpp
int dirtyFlagsFor(const Event& e) {
    switch (e.type) {
        case KeyPress:         return DIRTY_INPUT;  // (+ LAYOUT if Enter/Esc)
        case ContentAppend:
        case ContentNewline:
        case ContentThought:   return DIRTY_CONTENT;
        case StatusThinking:
        case StatusTask:       return DIRTY_STATUS;
        case StatusClear:      return DIRTY_STATUS;
        case InputChanged:     return DIRTY_INPUT | (lineCountChanged ? DIRTY_LAYOUT : 0);
        case LayoutResize:     return DIRTY_LAYOUT;
        case ModeChanged:      return DIRTY_MODE;
        case ConfirmShow:      return DIRTY_CONFIRM;
        case ConfirmHide:      return DIRTY_CONFIRM;
        case AppExit:          return 0;
    }
}
```

---

## 四、主循环

```cpp
int CLFRepl::run() {
    init();
    while (true) {
        // ── Phase 1: 收集输入 ──
        if (!m_confirmActive) {
            auto key = CLFConsole::readKey();
            processKey(key);  // → 修改 m_input/m_cursor → push InputChanged
        } else {
            auto key = CLFConsole::readKey();
            processConfirmKey(key);  // → push ConfirmHide / 执行/拒绝
        }
        
        // ── Phase 2: 检查缩放 ──
        int h = CLFTerminal::getTerminalHeight();
        if (h != m_lastHeight) push(LayoutResize{h, ...});
        m_lastHeight = h;
        
        // ── Phase 3: 消费事件队列 ──
        int dirty = 0;
        while (!m_eventQueue.empty()) {
            Event e = m_eventQueue.pop();
            dirty |= dirtyFlagsFor(e);
            // 需要状态变更的事件在此处理
            handleEvent(e);
        }
        
        // ── Phase 4: 渲染 ──
        if (dirty) CLFTerminal::renderDirty(dirty);
        
        if (m_exit) break;
    }
}
```

---

## 五、终端兼容性分析

### 5.1 DECSTBM 动态调整

```
风险: 输入换行或状态区展开时, ContentRegion 边界变化, DECSTBM 需重新设置

解决方案:
  - 每次 DIRTY_LAYOUT 时先 resetScrollRegion()
  - 计算新 ContentBottom
  - 重新 setScrollRegion(1, ContentBottom)
  - 重绘整个固定区

风险级别: 低 (DECSTBM 动态重置在 xterm/Windows Terminal/ConPTY 上均支持)
```

### 5.2 多线程写 stdout

```
风险: ThinkingIndicator(后台线程) 和 scrollPrint(HTTP 回调线程) 并发写 stdout

当前状态: ThinkingIndicator 直接写 cout, scrollPrint 也写 cout — 已有竞争!
事件化后: ThinkingIndicator → push Event, HTTP 回调 → push Event
          主线程统一渲染 → 只有主线程写 stdout

收益: 消除已有数据竞争 ✅
```

### 5.3 Windows 控制台 VT 支持

```
风险: Windows 10 以下或传统 conhost 不支持 DECSTBM

当前措施: CLFAnsi::enable() 检测 ENABLE_VIRTUAL_TERMINAL_PROCESSING
         不支持时走简化渲染路径 (无 ANSI 模式)

新布局兼容: 简化模式下各区顺序输出, 无固定区概念
           CTRL+C 退出, Enter 提交, 功能完整
```

### 5.4 光标闪烁

```
风险: 快速渲染时 (批量 scrollPrint) 光标在屏幕上跳动

解决方案: 渲染前 \033[?25l (隐藏光标), 渲染后 \033[?25h (显示)
          可选: 仅在 DIRTY_INPUT 时显示光标, 其他区渲染时隐藏
```

### 5.5 确认区输入焦点

```
风险: 确认区激活时, 键盘输入应操作确认区 (↑↓ Enter Esc)
      而非 InputRegion (输入文字)

解决方案:
  - m_confirmActive flag 控制主循环走 processConfirmKey 分支
  - ConfirmRegion 激活期间 InputRegion 只读显示 (不响应用户输入)
  - 确认完成 (Enter/Esc/CtrlC) 自动清除 flag

风险级别: 低 (已实现 confirmDialog 独立按键循环)
```

### 5.6 事件队列溢出

```
风险: API 流式数据产生速度 > 主循环消费速度

解决方案:
  - 环形队列, 队列满时丢弃最旧事件 (或合并 ContentAppend)
  - coalesce(): 连续多个 ContentAppend 合并为一个 (拼接 text)
  - 256 容量远超实际需求 (一帧最多 ~50 事件)
```

---

## 六、线程模型

```
Thread 1 (Main ─ CLFRepl::run):
  - readKey (阻塞)
  - processKey → push KeyPress event
  - pop events → handleEvent → renderDirty
  - 唯一写 stdout 的线程 ✅

Thread 2 (HTTP callback ─ httplib):
  - 接收 SSE chunk
  - push ContentAppend event  (thread-safe via EventQueue mutex)
  - 不写 stdout, 不调终端 API

Thread 3 (Thinking ─ CLFThinkingIndicator):
  - 每秒 push StatusThinking event  (thread-safe)
  - 检测 ESC → push interrupt event
  - 不写 stdout (移除 std::cout 直接输出)

锁:
  EventQueue::push 内部 lock_guard
  CLFScrollBuffer::append 内部 lock_guard (不变)
  CLFAnsi / CLFTerminal 静态状态 → 仅主线程访问, 无需锁
```

---

## 七、实施步骤

### Step 1: 终端 6 区布局 (~4h)

```
文件: CLFTerminal.hpp, CLFTerminal.cpp

变更:
  1.1 新增成员:
      s_statusText, s_statusTree   (⑥ StatusRegion)
      s_confirmOptions, s_confirmSelected (③ ConfirmRegion)
  
  1.2 新增方法:
      drawStatusLine(text)         → ⑥ 单行状态
      drawStatusTree(lines)        → ⑥ 树形展开
      clearStatusLine()            → ⑥ 清除
      drawConfirmBar(opts, sel)    → ③ 确认栏
      clearConfirmBar()            → ③ 清除
      drawModeLine(mode)           → ④ 模式行 (替代旧 drawModeArea)
  
  1.3 内部重写:
      computeLayout()              → 根据 N,M,H,W 计算所有行位置
      renderFixedArea()           → 一次性绘制③④⑤⑥+分隔线
      renderDirty(dirtyFlags)     → 只绘制 dirty 区
  
  1.4 旧 API 兼容:
      drawStatusArea → 转为调用 drawStatusLine
      drawModeArea   → 转为调用 drawModeLine
      drawConfirmArea→ 转为调用 drawConfirmBar
      toContentArea  → 清除 InputRegion + 重置状态

存量调用适配:
  CLFRepl.cpp: confirmDialog → drawConfirmBar/clearConfirmBar
  CLFAgentLoop.cpp: drawStatusArea → drawStatusLine
  CLFThinkingIndicator: std::cout → 暂不变 (Step 2 改)
  
验证:
  cmake --build build -j6
  启动 → 对话 → 工具调用 → Ctrl+C 退出
  所有原有功能正常
```

### Step 2: 事件系统 (~3h)

```
新增文件: CLFEvent.hpp, CLFEventQueue.hpp/cpp

变更:
  2.1 CLFEvent.hpp: EventType 枚举 + Event 结构体
  2.2 CLFEventQueue: 线程安全环形队列 + coalesce
  2.3 CLFRepl::run():
      主循环改为 Phase 1-4 结构
      processKey → push KeyPress → handleEvent
      confirmDialog → 内联到 run() (通过 m_confirmActive flag)
  2.4 CLFAgentLoop::runTurn():
      scrollPrint → push ContentAppend
      scrollPrint("\n") → push ContentNewline
      thoughtMark → push ContentThought
  2.5 CLFThinkingIndicator:
      std::cout << "· Thinking…" → push StatusThinking
      stop() → push StatusClear
  2.6 CLFToolExecutor:
      drawStatusArea → push StatusTask

CMDList 更新: 加 CLFEventQueue.cpp

验证:
  编译 → 对话 → 流式输出 → ESC 中断 → 工具调用
  确认帧末渲染: 多个连续 ContentAppend 只触发一次 DIRTY_CONTENT
```

### Step 3: 清理 + 测试 (~2h)

```
3.1 删除旧 API (CLFTerminal):
    toContentArea, drawStatusArea, drawModeArea, drawConfirmArea,
    clearConfirmArea, scrollAppend, setScrollCollapsed, isScrollCollapsed
    
3.2 删除旧成员:
    s_statusTitle, s_statusContent, s_scrollCollapsed ← 已无用
    
3.3 运行全部测试:
    ctest --test-dir build --output-on-failure
    
3.4 手动认证:
    启动横幅 → 对话 → 工具调用 → ESC 中断
    Shift+Tab 切模式 → /mode /clear /history /resume /config
    缩放终端 → 输入换行 → 确认区 → 树形状态
```

---

## 八、验收标准

| 测试场景 | 预期 |
|---------|------|
| 启动 | 横幅 + 5 skills 加载, 6 区正常显示 |
| 输入单行 | ❯ 正常显示, Enter 提交 |
| Shift+Enter 换行 | 输入区多行展开, 上分隔线上移 |
| 流式响应 | 内容逐字显示在滚动区, · Thinking 显示在状态区 |
| 思考超过 5s | 状态区更新 "· Thinking… (5s)", 不闪烁 |
| 工具调用 | 工具名显示在滚动区, 状态区显示 "working…" |
| 高风险确认 | 底部确认区出现, ↑↓选择, Enter确认 |
| Shift+Tab | 模式行切换, 其他区不变 |
| 终端缩放 | 各区域重新计算位置, 内容正常显示 |
| Ctrl+C 退出 | 会话保存, 再见提示, 返回 0 |
| Release 编译 | 零警告 |

---

## 九、文件清单

| 文件 | 操作 | 行数 |
|------|------|------|
| **新增** `CLFEvent.hpp` | EventType + Event | ~50 |
| **新增** `CLFEventQueue.hpp` | 线程安全环形队列 | ~40 |
| **新增** `CLFEventQueue.cpp` | coalesce 实现 | ~60 |
| `CLFTerminal.hpp` | 6 区 API | +30 / -20 |
| `CLFTerminal.cpp` | 布局重写 | ~400 重写 |
| `CLFRepl.hpp/cpp` | 事件主循环 | +30 / -120 |
| `CLFAgentLoop.cpp` | scrollPrint→Event | ~15 |
| `CLFThinkingIndicator.cpp` | cout→Event | ~10 |
| `CLFToolExecutor.cpp` | status→Event | ~5 |
| `CMakeLists.txt` | 加 EventQueue | +3 |

**总计**: 3 新文件 ~150 行, 8 文件改动 ~620 行 (含 旧代码删除 ~150 行)
