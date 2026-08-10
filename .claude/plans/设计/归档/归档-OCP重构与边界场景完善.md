# 设计：OCP 重构 + 边界场景完善（v2 — 基于代码审计修正）

> 状态：待审批 | 日期：2026-08-06 | 版本：v2

---

## 变更记录

| 版本 | 变更 |
|------|------|
| v1 (初版) | 仅覆盖 progress.md 三项遗留问题 |
| v2 (当前) | 基于全量代码审计结果，新增 Phase 0 清理 + 4 个组件提取 + ICLFOutput 瘦身 |

---

## 一、审计发现汇总

全量代码审计（3 个 Agent 并行扫描 `src/` 全部文件）发现：

### 🔴 Bug

| # | 问题 | 位置 | 影响 |
|---|------|------|------|
| B1 | **CLFToolExecutor 输出参数为 nullptr** | `CLFAgentLoop.cpp:210` 构造时未传第 5 参数 `output` | 所有工具执行反馈**静默丢失**，用户看不到工具调用 |
| B2 | **数据竞争** | `CLFRepl.cpp` Renderer lambda 不加锁读 `m_contentBuffer`/`m_pendingLine`/`m_statusText`，后台线程持锁写入 | 未定义行为 |

### ⚫ 死代码（确认可删除）

| # | 文件 | 原因 |
|---|------|------|
| D1 | `CLFUI/CLFConsole.hpp/.cpp` | FTXUI 之前的原始控制台输入，零调用方 |
| D2 | `CLFUI/CLFScrollBuffer.hpp/.cpp` | 被 CLFTerminal 内部 `m_contentBuffer` 取代 |
| D3 | `CLFTypes/CLFEvent.hpp` + `CLFEventQueue.hpp/.cpp` | **只写不读**：事件 push 但从未 pop/消费 |
| D4 | `CLFTypes/CLFErrorCodes.hpp` | 无任何文件 include |
| D5 | `test/MockOutput.hpp` | 无任何测试使用 |
| D6 | `CLFTerminal::m_repl` / `setRepl()` | 写入但从未读取（`main.cpp:69` 设置后无消费） |
| D7 | `CLFAnsi::lightBlue` / `textWidth` / `wrappedLines` | 零调用方 |
| D8 | `CLFTerminal::green` / `yellow`（静态委托） | 零调用方（转调 `CLFAnsi` 同名函数，对方也无调用） |
| D9 | `CLFAgentLoop::getSecurityMode()` | 零调用方（仅 `getSecurityModeName()` 被使用） |
| D10 | `CLFAgentLoop::setStatusCallback` / `m_statusCallback` | 被 CLFRepl 接线但从**未被调用** |
| D11 | `CLFRetryPolicy::isRetryableError()` | 零调用方（仅 `isFatalHttpError()` 被使用） |
| D12 | `CLFSkillLoader::clear()` | 零调用方 |
| D13 | `CLFConfigLoader::getProjectRoot()` | 零调用方 |
| D14 | `CLFAgentLoop.cpp` `emitNewline()` 匿名函数 | 零调用方 |
| D15 | `ICLFOutput` 中 7 个从未调用的纯虚方法 | 见 B3 |
| D16 | `CMakeLists.txt:100` 过期注释 + 测试 C++20 不一致 | — |

### 🟡 架构问题

| # | 问题 | 详情 |
|---|------|------|
| A1 | ICLFOutput 接口虚胖 | 13 个纯虚方法中 7 个从未调用：`emitRaw`、`onToolCall`、`onToolResult`、`askSelect`、`askInput`、`requestShutdown`、`InterruptError` |
| A2 | Confirm 跨三处分裂 | 状态在 CLFTerminal、渲染在 CLFRepl Renderer、按键在 CatchEvent |
| A3 | Mode 三处硬编码 | `kModeCycle[]`(CLFRepl:69) + `CLFSecurityPolicy`(权威) + `CLFCommandDispatcher::m_modeName`(副本) |
| A4 | CLFRepl::run() 244 行 | 含两个巨型 lambda（Renderer 96 + CatchEvent 93），可提取 4 个独立组件 |
| A5 | CLFRepl 绕过 ICLFOutput 抽象 | 直接 `dynamic_cast<CLFTerminal*>` + 访问 public 成员 |
| A6 | CLFRepl.cpp 依赖 `<windows.h>` | 仅因两个 clipboard 函数，应移至独立文件 |
| A7 | CLFTerminal 15+ public 成员 | 注释写明「Renderer lambdas 需要访问」——应改为 snapshot accessor |

---

## 二、修正后的设计方案

### 设计原则（新增）

1. **先清理再建造**：Phase 0 清掉所有死代码和已知 bug，提供一个干净的基线
2. **组件化优先**：超出 30 行的独立逻辑提取为新类/文件，遵循 SOLID 中的 SRP
3. **接口最小化**：ICLFOutput 做到「刚好满足需求」，不过度设计
4. **保持可编译**：每个 Phase 结束时项目必须编译通过 + 测试全绿

### 整体 Phase 规划

```
Phase 0: 清理与修复（基础）
  ├── 0.1 删除死文件
  ├── 0.2 删除事件系统
  ├── 0.3 ICLFOutput 瘦身
  ├── 0.4 修复 CLFToolExecutor nullptr bug
  ├── 0.5 修复数据竞争（CLFTerminal snapshot）
  ├── 0.6 删除死方法/死指针
  ├── 0.7 Mode 去重
  └── 0.8 CMakeLists.txt 清理

Phase 1: OCP 命令注册表（核心重构）
  ├── 1.1 CLFCommand 结构体 + 注册 API
  ├── 1.2 CLFCommands.cpp — 11 个 handler
  └── 1.3 handle() 查表路由

Phase 2: CLFRepl 组件提取（解耦）
  ├── 2.1 CLFClipboard（独立工具文件）
  ├── 2.2 CLFAsyncSubmit（线程管理）
  ├── 2.3 CLFScrollView（viewport + scroll keys）
  └── 2.4 CLFConfirmBar（confirm render + key handling）

Phase 3: CJK 光标缓解
  ├── 3.1 sanitizeUtf8 提升为公开函数
  ├── 3.2 Ctrl+V 粘贴 UTF-8 验证
  └── 3.3 问题复现 + 终端兼容性评估
```

---

## 三、Phase 0：清理与修复（基础）

### 3.0.1 删除死文件（D1-D5）

| 操作 | 文件 | CMakeLists 变更 |
|------|------|----------------|
| 删除 | `src/CLFUI/CLFConsole.hpp` + `.cpp` | 从 `clf_ui` 移除 |
| 删除 | `src/CLFUI/CLFScrollBuffer.hpp` + `.cpp` | 从 `clf_ui` 移除 |
| 删除 | `src/CLFTypes/CLFEvent.hpp` | —（header-only） |
| 删除 | `src/CLFTypes/CLFEventQueue.hpp` + `.cpp` | 从 `clf_types` 移除 |
| 删除 | `src/CLFTypes/CLFErrorCodes.hpp` | —（header-only） |
| 删除 | `src/test/MockOutput.hpp` | —（未被编译） |

### 3.0.2 删除事件系统残留（D3）

影响文件：
- `CLFAgentLoop.hpp` — 删除 `#include` 前置声明、`setEventQueue()` 方法、`m_eventQueue` 成员
- `CLFAgentLoop.cpp` — 删除 `emitContent(CLFEventQueue*, ...)` 中的 queue push 逻辑，简化为直接调 `out->emitContent()`；删除 `emitNewline()`（本身也无调用）
- `CLFRepl.hpp` — 删除 `m_eventQueue` 成员 + 前置声明
- `CLFRepl.cpp` — 删除 `m_eventQueue` 构造和 `m_agent.setEventQueue()`

### 3.0.3 ICLFOutput 瘦身（A1）

从 13 个纯虚方法 → **6 个**：

```cpp
class ICLFOutput {
public:
    virtual ~ICLFOutput() = default;

    // ① 内容输出
    virtual void emitContent(const std::string& text) = 0;
    virtual void emitRaw(const std::string& data) = 0;    // [保留] 子进程透传钩子

    // ② 瞬时状态
    virtual void setStatus(const std::string& title,
                           int current = -1, int total = -1) = 0;

    // ③ 交互
    virtual bool confirm(const std::string& prompt) = 0;

    // ④ 中断
    virtual void onInterrupt(std::function<void()> callback) = 0;

    // ⑤ 错误
    virtual void emitError(const std::string& message) = 0;
};
```

**删除的方法 + 理由**：

| 方法 | 删除理由 |
|------|----------|
| `onToolCall` / `onToolResult` | CLFToolExecutor 直接用 `emitContent` 输出，未走此接口 |
| `askSelect` / `askInput` | 零调用方；即使需要也可走 `confirm` 的多选变体或未来的 Input 组件 |
| `requestShutdown` | 零调用方；致命错误走 `emitError` |
| `InterruptError` 异常类 | 定义但从未抛出；中断走 `onInterrupt` 回调 |

**同步变更**：`CLFTerminal` 删除对应 override；`MockOutput` 文件已删除。

### 3.0.4 修复 CLFToolExecutor nullptr bug（B1）

**根因**：`CLFAgentLoop.cpp:210-211`
```cpp
// 当前：漏传 output
CLFToolExecutor executor(m_tools, m_securityPolicy, m_confirmCallback, m_lastToolStats);
// 修正：传入 output
CLFToolExecutor executor(m_tools, m_securityPolicy, m_confirmCallback, m_lastToolStats, m_output);
```

**风险**：修复后工具反馈会**首次显示**给用户（之前一直被静默丢弃），UI 可能有小变化——但这才是正确行为。

### 3.0.5 修复数据竞争（B2）

**新增 `CLFTerminal` snapshot accessor**：

```cpp
// CLFTerminal.hpp 新增（替代 Renderer 直接读 public 成员）
struct ContentSnapshot {
    std::vector<std::string> lines;
    std::string pendingLine;
    std::string statusText;
    bool confirmActive;
    std::string confirmPrompt;
    std::vector<std::string> confirmOpts;
    int  confirmSel;
};
ContentSnapshot contentSnapshot() const {
    std::lock_guard lock(m_mutex);
    return {m_contentBuffer, m_pendingLine, m_statusText,
            m_confirmActive, m_confirmPrompt, m_confirmOpts, m_confirmSel};
}
```

**Renderer 侧**：从 `terminal->m_contentBuffer` 改为 `auto snap = terminal->contentSnapshot();`，后续全部引用 `snap.xxx`。一次加锁、一次拷贝、渲染全帧。

**注意**：`m_confirmActive` 等字段目前未被 `m_mutex` 保护——需一并纳入 `m_mutex` 保护范围。

### 3.0.6 删除死方法/死指针（D6-D14）

| 文件 | 操作 |
|------|------|
| `CLFTerminal.hpp/.cpp` | 删除 `setRepl()` / `m_repl`（写入从未读取） |
| `CLFTerminal.hpp/.cpp` | 删除 `green()` / `yellow()` 静态委托（零调用） |
| `CLFTerminal.hpp/.cpp` | 删除 `isShutdownRequested()` / `m_shutdownRequested` |
| `CLFTerminal.cpp` | 删除 `stripAnsi()`（仅 emitRaw 内 flush 调用，而 emitRaw 无调用方；后续 emitRaw 启用时重新评估） |
| `CLFAnsi.hpp/.cpp` | 删除 `lightBlue()` / `textWidth()` / `wrappedLines()` |
| `CLFAgentLoop.hpp/.cpp` | 删除 `getSecurityMode()` / `setStatusCallback` / `m_statusCallback` |
| `CLFAgentLoop.cpp` | 删除 `emitNewline()` |
| `CLFRetryPolicy.hpp/.cpp` | 删除 `isRetryableError()` |
| `CLFSkillLoader.hpp/.cpp` | 删除 `clear()` |
| `CLFConfigLoader.hpp/.cpp` | 删除 `getProjectRoot()` |
| `CLFSessionManager.hpp/.cpp` | 审查 `remove()` / `promote()`（仅在测试中使用→移到测试文件或保留） |

**原则**：每个删除前 grep 确认零引用。

### 3.0.7 Mode 去重（A3）

**问题**：模式名在 3 处独立维护：
1. `CLFSecurityPolicy`（权威来源，`getModeName`/`modeFromString`）
2. `CLFRepl.cpp:69` — `kModeCycle[] = {"auto","analyze","edit","manual"}`
3. `CLFCommandDispatcher::m_modeName` — 平行副本

**修正**：

```cpp
// CLFSecurityPolicy.hpp 新增
static constexpr const char* kModeNames[] = {"auto", "analyze", "edit", "manual"};
static constexpr int kModeCount = 4;
static CLFSecurityMode nextMode(CLFSecurityMode current);  // 循环下一个
```

```cpp
// CLFRepl::cycleMode() 简化为：
void CLFRepl::cycleMode() {
    auto next = CLFSecurityPolicy::nextMode(m_agent.getSecurityMode());
    m_agent.setSecurityMode(next);
    // m_dispatcher 不再存储独立的 m_modeName，改为直接从 agent 读取
}
```

```cpp
// CLFCommandDispatcher — modeName() 改为代理 agent
const std::string& CLFCommandDispatcher::modeName() const {
    return m_agent.getSecurityModeName();
}
// 删除 m_modeName 成员和 setModeName()；cycleMode() 通过 agent.setSecurityMode() 同步
```

### 3.0.8 CMakeLists.txt 清理

- 删除 `CLFConsole.cpp`、`CLFScrollBuffer.cpp` 编译项
- 删除 `CLFEventQueue.cpp` 编译项
- 删除 `# Step 0: FTXUI ANSI 透传验证 (临时, 验证后删除)` 过期注释
- 测试 target 的 `CXX_STANDARD 20` → `CXX_STANDARD 17`（与项目一致）

### Phase 0 变更总览

| 类别 | 文件数 | 删除行数（估算） |
|------|--------|-----------------|
| 删除文件 | 11 | ~800 行 |
| 修改文件 | ~12 | ~200 行 |
| ICLFOutput 瘦身 | 2 | ~60 行 |
| Bug 修复 | 2 | ~5 行 |

---

## 四、Phase 1：OCP 命令注册表

### 4.1 核心类型

```cpp
// CLFCommandDispatcher.hpp（基于 Phase 0 清理后的版本）

namespace CLF::CLFUI {

using CLFCommandHandler = std::function<bool(
    const std::string& cmdName,       // "/exit"
    const std::string& args,          // 命令名后的参数字符串（已 trim）
    CLF::CLFCore::CLFAgentLoop& agent,
    const std::string& historyDir,
    CLF::CLFTypes::ICLFOutput* output)>;

struct CLFCommand {
    std::string       m_name;         // "/exit"（含前缀）
    std::string       m_description;  // "退出并保存会话"
    CLFCommandHandler m_handler;
};

class CLFCommandDispatcher {
public:
    CLFCommandDispatcher(CLF::CLFCore::CLFAgentLoop& agent,
                         const std::string& historyDir,
                         CLF::CLFTypes::ICLFOutput* output,
                         std::function<void()> onExit);

    void registerCommand(CLFCommand cmd);
    bool handle(const std::string& input);

    // modeName 改为 agent 代理（Phase 0.7 已处理）
    std::string modeName() const;

    void setOnExit(std::function<void()> cb) { m_onExit = std::move(cb); }
    const std::vector<CLFCommand>& commands() const { return m_commands; }

private:
    CLF::CLFCore::CLFAgentLoop& m_agent;
    std::string m_historyDir;
    CLF::CLFTypes::ICLFOutput* m_output;
    std::function<void()> m_onExit;
    std::vector<CLFCommand> m_commands;
};
```

### 4.2 handle() 路由

```cpp
bool CLFCommandDispatcher::handle(const std::string& input) {
    if (input.empty() || input[0] != '/') return false;

    auto spacePos = input.find(' ');
    std::string cmdName = (spacePos == std::string::npos) ? input : input.substr(0, spacePos);
    std::string args;
    if (spacePos != std::string::npos) {
        args = input.substr(spacePos + 1);
        // trim leading spaces
        size_t start = args.find_first_not_of(' ');
        args = (start == std::string::npos) ? "" : args.substr(start);
    }

    for (auto& cmd : m_commands) {
        if (cmd.m_name == cmdName) {
            bool handled = cmd.m_handler(cmdName, args, m_agent, m_historyDir, m_output);
            // /exit 特殊处理：handler 返回后调用 onExit
            if (handled && cmdName == "/exit" && m_onExit) {
                m_onExit();
            }
            return handled;
        }
    }
    return false;
}
```

### 4.3 内置命令拆分

新建 `src/CLFUI/CLFCommands.cpp`，按功能分 5 组：

```
/src/CLFUI/CLFCommands.cpp
  namespace {  // 匿名命名空间
    // === 会话管理 ===
    cmdExit()     — agent.saveSession + onExit 由 handle() 调用
    cmdClear()    — agent.saveSession + clearContext

    // === 信息查询 ===
    cmdHelp()     — 静态帮助文本
    cmdModel()    — agent.getConfig()
    cmdConfig()   — agent.getConfig()
    cmdContext()  — agent.getContext() + agent.getConfig()

    // === 模式 ===
    cmdMode()     — CLFSecurityPolicy::modeFromString() + agent.setSecurityMode()

    // === 知识库 ===
    cmdSkill()    — CLFSkillLoader 静态方法 + agent.getLoadedSkills()/injectSkillToContext()

    // === 会话恢复 ===
    cmdHistory()  — CLFSessionManager::list()
    cmdResume()   — CLFSessionManager::list() + agent.restoreSession()
  }

  void registerBuiltinCommands(CLFCommandDispatcher& dispatcher) {
      dispatcher.registerCommand({"/exit",    "退出并保存会话", cmdExit});
      dispatcher.registerCommand({"/help",    "显示帮助信息",   cmdHelp});
      // ... 共 11 条
  }
```

### 4.4 变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `CLFCommandDispatcher.hpp` | **修改** | 新增 `CLFCommand` 结构体、`registerCommand()`、`commands()`；modeName() 改为代理；删除 m_modeName |
| `CLFCommandDispatcher.cpp` | **重写** | 构造时调用 `registerBuiltinCommands()`；`handle()` 查表路由 |
| `CLFCommands.cpp` | **新增** | 11 个 handler + `registerBuiltinCommands()` |
| `CLFRepl.cpp` | **轻微修改** | 删除 `#include` 已删文件；cycleMode() 简化 |
| `CMakeLists.txt` | **修改** | 添加 `CLFCommands.cpp` |

### 4.5 验证方式

写一个简单的编译期测试：构造 `CLFCommandDispatcher`，对每个已知命令调 `handle()`，验证返回 true + 输出非空。

---

## 五、Phase 2：CLFRepl 组件提取

### 5.1 CLFClipboard（独立工具）

从 `CLFRepl.cpp:33-66` 提取 `readClipboard()` / `writeClipboard()` 到新文件：

```
src/CLFUI/CLFClipboard.hpp   — readClipboard() / writeClipboard() 声明
src/CLFUI/CLFClipboard.cpp   — Win32 实现（#include <windows.h> 移至此）
```

**收益**：CLFRepl.cpp 不再需要 `<windows.h>`；剪贴板逻辑可独立测试。

### 5.2 CLFAsyncSubmit（线程管理）

从 `CLFRepl::run()` 提取 submit 线程管理逻辑：

```cpp
// src/CLFUI/CLFAsyncSubmit.hpp
class CLFAsyncSubmit {
public:
    // 启动异步任务（如果上一个还在运行则 join 等待）
    void launch(std::function<void()> task);
    // 等待当前任务完成
    void join();
    bool busy() const { return m_submitting; }
private:
    std::thread       m_thread;
    std::atomic<bool> m_submitting{false};
};
```

**收益**：线程 join-before-respawn 逻辑集中管理；CLFRepl::run() 减少 ~10 行。

### 5.3 CLFScrollView（viewport + scroll keys）

从 Renderer + CatchEvent 提取 viewport 逻辑：

```cpp
// src/CLFUI/CLFScrollView.hpp
class CLFScrollView {
public:
    // 每帧调用：根据 totalLines 更新内部状态
    void update(int totalLines, int termHeight, int reservedLines = 6);

    // 从 allLines 中截取可见窗口，返回带 hint 的 Elements
    ftxui::Elements renderWindow(const ftxui::Elements& allLines);

    // 处理滚动相关事件，返回 true 表示已消费
    bool handleEvent(ftxui::Event e);

    // 重置（/clear 时）
    void reset();

private:
    int  m_scrollOffset   = 0;
    bool m_autoScroll     = true;
    int  m_lastTotalLines = 0;
    int  m_viewH          = 0;
    int  m_maxOff         = 0;
};
```

**提取来源**：
- 状态变量：`CLFRepl.cpp:131-133`（scrollOffset, autoScroll, lastTotalLines）
- Viewport 算法：`CLFRepl.cpp:156-180`（viewH, maxOff, clamp, slice, hints）
- Scroll 事件：`CLFRepl.cpp:239-262`（wheel, PageUp/Down, Home/End）

**收益**：~55 行逻辑 + 状态集中到一个可单测类；Renderer 和 CatchEvent 各自减少 ~30 行。

### 5.4 CLFConfirmBar（confirm render + key handling）

将 confirm 的**渲染**和**按键处理**合并到一个组件：

```cpp
// src/CLFUI/CLFConfirmBar.hpp
class CLFConfirmBar {
public:
    // 绑定到 CLFTerminal 的 confirm 状态（snapshot 方式）
    void update(const CLFTerminal::ContentSnapshot& snap);

    // 渲染确认栏
    ftxui::Element render();

    // 处理 confirm 相关按键（Return/Esc/ArrowLeft/Right）
    // 返回 true 表示已消费
    bool handleEvent(ftxui::Event e, CLFTerminal& terminal);

    bool active() const { return m_active; }

private:
    bool                m_active = false;
    std::string         m_prompt;
    std::vector<std::string> m_opts;
    int                 m_sel = 0;
};
```

**注意**：`handleEvent` 需要写 `terminal.m_confirmResult`/`m_confirmActive`/`m_confirmCv`——这些交互字段保留在 CLFTerminal 中（因为 `CLFTerminal::confirm()` 的 CV 等待需要它们），但由 CLFConfirmBar 通过 `ContentSnapshot` 读取、通过直接操作 terminal 写入。

**收益**：Renderer 减少 ~22 行，CatchEvent 减少 ~19 行，confirm 协议有一个明确的 owner。

### 5.5 CLFRepl::run() 最终形态（预期 ~80 行）

```
run():
  cleanup temp files          → 移至 CLFSessionManager::cleanupTempFiles()
  init FTXUI screen
  print banner
  create input component
  create CLFScrollView
  create CLFConfirmBar
  create CLFAsyncSubmit
  Renderer = [&] {
      auto snap = terminal->contentSnapshot();    // 修复数据竞争
      scrollView.update(snap.lines.size() + ...);
      auto window = scrollView.renderWindow(allLines);
      auto confirmBar = confirmBar.render();
      return vbox({window, status, separator, input, separator, mode, confirmBar});
  }
  CatchEvent = [&](Event e) {
      if (confirmBar.handleEvent(e, *terminal))  return true;
      if (scrollView.handleEvent(e))             return true;
      if (e == CtrlD)  asyncSubmit.launch(...);
      if (e == Escape) terminal->m_interruptCb();
      ...
  }
  screen.Loop(CatchEvent(Renderer(...)))
  asyncSubmit.join()
```

### 5.6 其他小修正

- **Temp 文件清理**（`CLFRepl.cpp:101-107`）→ 移至 `CLFSessionManager::cleanupTempFiles()`
- **Keybinding legend 字符串** → 提取为 `CLFRepl.cpp` 文件级常量，Renderer 和 CatchEvent 共用，防止 drift
- **CLFTerminal 中 m_mutex 保护范围** → 扩大到 confirm 字段 + m_statusText

### Phase 2 变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `CLFUI/CLFClipboard.hpp/.cpp` | **新增** | 剪贴板工具 |
| `CLFUI/CLFAsyncSubmit.hpp/.cpp` | **新增** | 异步提交线程管理 |
| `CLFUI/CLFScrollView.hpp/.cpp` | **新增** | 滚动视口组件 |
| `CLFUI/CLFConfirmBar.hpp/.cpp` | **新增** | 确认栏组件 |
| `CLFUI/CLFRepl.hpp` | **修改** | 成员变量减少 |
| `CLFUI/CLFRepl.cpp` | **重写 run()** | ~240→~80 行 |
| `CLFUI/CLFTerminal.hpp` | **修改** | 新增 ContentSnapshot + accessor |
| `CMakeLists.txt` | **修改** | 添加 4 个新文件 |

---

## 六、Phase 3：CJK 光标缓解

### 6.1 sanitizeUtf8 提升

当前 `sanitizeUtf8` 位于 `CLFContext.cpp` 匿名命名空间，提升为 `CLFCore` 的公开函数：

```cpp
// CLFCore/CLFContext.hpp 新增声明
namespace CLF::CLFCore {
    std::string sanitizeUtf8(const std::string& input);
}
```

### 6.2 Ctrl+V 粘贴 UTF-8 验证

在 Phase 2 提取的 `CLFClipboard` 中增加验证：

```cpp
// CLFClipboard.cpp
std::string CLFClipboard::read() {
    std::string raw = readClipboardRaw();  // 原 Win32 实现
    return CLFCore::sanitizeUtf8(raw);     // UTF-8 净化
}
```

### 6.3 CJK 半字移动复现

在 Windows Terminal 中尝试复现：

1. 输入中文 + 英文混合文本
2. 用方向键逐字移动光标
3. 用 Ctrl+V 粘贴包含截断 UTF-8 的文本
4. 用鼠标点击中文文本中间位置

**预期结果**：FTXUI v7 的 GlyphPrevious/GlyphNext 应正确处理全宽字符。如果无法复现，将 progress.md 该项标记为「无法复现，关闭」；如果能复现，录制现象并评估是否需要 FTXUI 补丁。

### Phase 3 变更清单

| 文件 | 操作 |
|------|------|
| `CLFCore/CLFContext.hpp` | 新增 `sanitizeUtf8` 声明 |
| `CLFCore/CLFContext.cpp` | 将 `sanitizeUtf8` 移出匿名命名空间 |
| `CLFUI/CLFClipboard.cpp` | `read()` 增加 sanitizeUtf8 调用 |
| `progress.md` | 更新 CJK 问题状态 |

---

## 七、风险与缓解

| 风险 | 严重程度 | 缓解 |
|------|----------|------|
| Phase 0 删除过多导致编译失败 | 中 | 每个子步骤独立编译验证，逐步删除 |
| CLFToolExecutor 修复后 UI 变化 | 低 | 这是**恢复正确行为**，工具反馈本应显示 |
| Snapshot 拷贝开销 | 低 | 每帧一次 string vector 拷贝（通常 <1000 行），远小于渲染开销 |
| CLFScrollView 引入滚动行为回归 | 中 | 先写单测覆盖 clamp/slice/hint 边界，再替换 |
| CLFConfirmBar 破坏 confirm CV 协议 | 中 | confirm 字段保留在 CLFTerminal（不改位置），ConfirmBar 通过引用写入 |
| CJK 半字移动无法复现 | 低 | 最多花费 30min 排查，无法复现则记录关闭 |

---

## 八、附录

### A. 死代码删除清单（完整）

```
删除文件 (11):
  src/CLFUI/CLFConsole.hpp
  src/CLFUI/CLFConsole.cpp
  src/CLFUI/CLFScrollBuffer.hpp
  src/CLFUI/CLFScrollBuffer.cpp
  src/CLFTypes/CLFEvent.hpp
  src/CLFTypes/CLFEventQueue.hpp
  src/CLFTypes/CLFEventQueue.cpp
  src/CLFTypes/CLFErrorCodes.hpp
  src/test/MockOutput.hpp
  (CLFCommands.cpp 新增 — 非删除)

删除方法/成员 (跨文件):
  CLFAnsi::lightBlue, textWidth, wrappedLines
  CLFTerminal::green, yellow, setRepl, m_repl, isShutdownRequested, m_shutdownRequested
  CLFTerminal::askSelect, askInput, onToolCall, onToolResult, requestShutdown (override)
  CLFAgentLoop::getSecurityMode, setStatusCallback, m_statusCallback
  CLFAgentLoop::emitNewline (匿名函数)
  CLFAgentLoop::setEventQueue, m_eventQueue
  CLFRepl::m_eventQueue
  CLFRetryPolicy::isRetryableError
  CLFSkillLoader::clear
  CLFConfigLoader::getProjectRoot
  CLFCommandDispatcher::m_modeName (改为代理 agent)
  ICLFOutput::onToolCall, onToolResult, askSelect, askInput, requestShutdown
  ICLFOutput::InterruptError 异常类

总计估算: ~1100 行净删除
```

### B. ICLFOutput 瘦身对比

```
v1 (当前) 13 方法:
  emitContent  emitRaw  setStatus  onToolCall  onToolResult
  confirm  askSelect  askInput  onInterrupt  emitError
  requestShutdown  InterruptError + ~ICLFOutput

v2 (修正后) 6 方法:
  emitContent  emitRaw  setStatus  confirm  onInterrupt  emitError
```

### C. 新文件一览

| Phase | 文件 | 说明 |
|-------|------|------|
| P1 | `CLFUI/CLFCommands.cpp` | 11 个内置命令 handler |
| P2 | `CLFUI/CLFClipboard.hpp/.cpp` | 剪贴板工具 |
| P2 | `CLFUI/CLFAsyncSubmit.hpp/.cpp` | 异步提交线程 |
| P2 | `CLFUI/CLFScrollView.hpp/.cpp` | 滚动视口 |
| P2 | `CLFUI/CLFConfirmBar.hpp/.cpp` | 确认栏组件 |
