# 设计-Harness 架构重构

> 状态：已完成 | 创建：2026-08-04 | 审查日期：2026-08-07
> 
> **⚠️ 审查标记**：本文档是开发过程文档。以下内容与当前代码**不一致**，阅读时请注意：
> - `ICLFOutput` 接口实际为 **6 方法**（非文档描述的 11 方法），`onToolCall/onToolResult/askSelect/askInput/requestShutdown` 未实现，`InterruptError` 不存在
> - `confirm` 机制改为 **CLFTerminal 内部 CV 同步 + 底部确认栏**（非嵌套 Loop，非双向注入 setRepl）
> - 中断改为**标志检查 + 返回字符串**（非 throw InterruptError）
> - `CLFConsole` 已删除（非保留为私有辅助），`CLFScrollBuffer` 由 `CLFScrollView` 取代
> - `MockOutput` 测试未实施
> - 以 `> **DEPRECATED:**` 标记的段落均与当前代码不符

## 1. 背景

### 什么是 Harness

**Harness（编排框架）** = Model 之外的一切。Model 提供原始智能（LLM），Harness 提供工具调度、权限执行、上下文管理、状态持久化、UI 渲染。公式：

```
Agent = Model + Harness
```

Claude Code 整个产品就是一个 Harness。Anthropic 的定义："Claude Code provides the tools, context management, and execution environment that turn a language model into a capable coding agent."

### Claude Code Harness 参考分层

```
┌─ Access Layer ───────────────────────────┐
│  Ink/React TUI + /command 系统            │
│  协议适配、请求路由、响应格式化             │
├─ Orchestration Layer ─────────────────────┤
│  Agent Loop (nO) + 动态 Workflow + 子Agent│
│  TodoWrite 计划 → dispatch_agent 派发     │
├─ Permission Layer ────────────────────────┤
│  deny→ask→allow 管道 (独立于推理的代码路径)│
│  Auto-mode 后台分类器（不读 Agent 输出）    │
├─ Tool Layer ──────────────────────────────┤
│  19 个权限门控工具 + MCP 扩展              │
├─ Context Layer ───────────────────────────┤
│  上下文窗口管理 + Compaction + CLAUDE.md   │
└─ Quality Layer ───────────────────────────┘
  Default-FAIL 合约 + 独立评估子Agent + Hook
```

### 核心设计原则（来自 Claude Code）

1. **Permission 独立于推理** — 模型决定"做什么"，另一套代码决定"能不能做"
2. **Agent Loop 纯调度** — 不含渲染逻辑，输出结构化数据由 Access 层呈现
3. **输出通道抽象** — Agent 不知道输出目标是 TUI 还是 API
4. **Default-FAIL 合约** — 不能声称成功除非有证据
5. **Fix Once in the Kernel** — 统一调度层的修改惠及所有工具

## 2. CLFCode 当前问题

```
❌ 当前:  CLFCore ──直接调用──→ CLFUI (scrollPrint/showThinking...)
          CLFToolExecutor 直接渲染终端
          CLFCommandDispatcher 直接调 scrollPrint
          CLFRepl 做文件清理（CLFTools 的职责）
          
结果:  clf_core 必须链接 clf_ui → 循环依赖 → 模块边界模糊
```

### 职责泄漏清单

| 位置 | 泄漏内容 | 应归属 |
|:---|:---|:---|
| `CLFAgentLoop::emitContent` | 直接调 `CLFTerminal::scrollPrint` | ICLFOutput |
| `CLFAgentLoop` 错误处理 | 直接调 `CLFTerminal::red/yellow/gray` | ICLFOutput |
| `CLFToolExecutor::execute` | 直接调 `CLFTerminal::scrollPrint` 渲染结果 | ICLFOutput |
| `CLFCommandDispatcher` | 每个命令直接调 `scrollPrint` | ICLFOutput |
| `CLFThinkingIndicator` | 直接调 `CLFTerminal::showThinking/clearStatus` | ICLFOutput |
| `CLFAgentLoop` 流式回调 | 直接调 `CLFConsole::checkEscape` | ICLFOutput |
| `CLFRepl::run` 启动清理 | 删除 `clf_cmd_stdout_*` 临时文件 | CLFTools |

## 3. 目标架构

```
              ┌─────────────┐
              │  CLFTypes   │  ← ICLFOutput 接口 (零依赖)
              └──────┬──────┘
         ┌───────────┼───────────┐
         ▼           ▼           ▼
   ┌──────────┐ ┌──────────┐ ┌──────────┐
   │CLFNetwork│ │ CLFCore  │ │ CLFUI    │  ← CLFUI 实现 ICLFOutput
   │ HTTP传输 │ │Agent逻辑 │ │ 终端渲染 │
   └────┬─────┘ └──┬───┬───┘ └────┬─────┘
        │          │   │          │
        │     ┌────┘   └────┐     │
        ▼     ▼            ▼     ▼
   ┌─────────────────┐  ┌─────────────────┐
   │   CLFTools      │  │     main.cpp    │  ← 组装注入
   │   工具实现       │  │   入口 + 装配    │
   └─────────────────┘  └─────────────────┘

依赖规则:
  CLFTypes     ← 零项目依赖
  CLFNetwork   ← CLFTypes
  CLFCore      ← CLFTypes + CLFNetwork (❌ 不依赖 CLFUI!)
  CLFTools     ← CLFCore
  CLFUI        ← CLFCore + CLFNetwork + CLFTypes (实现 ICLFOutput)
  main         ← 组装全部
```

## 4. ICLFOutput 接口 V4（CLI 降维·定稿）

### CLI 三大残酷现实

| # | 现实 | 含义 |
|:---|:---|:---|
| 1 | **输出分层** | 滚动日志（emitContent）和瞬时状态（showWorking）必须物理隔离——终端没有 DOM 节点，混在一起 = 疯狂刷屏 |
| 2 | **交互阻塞** | `confirm`/`askSelect` 和 `showWorking` 必须互斥——CLI 没有异步弹窗，状态刷新会"吃掉"用户输入字符 |
| 3 | **中断靠信号** | 轮询 `isInterrupted()` 在主线程阻塞（等子进程）时完全失效——必须用信号驱动 |

### 接口定义

> **DEPRECATED: 下文接口为设计初稿，实际 ICLFOutput 仅 6 个方法（emitContent/emitRaw/setStatus/confirm/onInterrupt/emitError），onToolCall/onToolResult/askSelect/askInput/requestShutdown/InterruptError 均未实现。**

```cpp
// CLFTypes/ICLFOutput.hpp — Agent → UI 输出抽象, CLI 约束版
namespace CLF::CLFTypes {

// 中断异常 — 冒泡到主循环捕获
class InterruptError : public std::runtime_error {
public: InterruptError() : std::runtime_error("User interrupted") {}
};

class ICLFOutput {
public:
    virtual ~ICLFOutput() = default;

    // ========== ① 内容输出 ==========

    // 永久内容 (进滚动区): AI 回复文本、代码块、最终答案
    virtual void emitContent(const std::string& text) = 0;

    // 子进程原始透传: 不加前缀, 不破坏 ANSI 进度条.
    // 实现方应自动清状态进入原始模式(调用方无需关心互斥细节),
    // emitRaw 结束后恢复状态刷新.
    virtual void emitRaw(const std::string& data) = 0;

    // ========== ② 瞬时状态 (统一状态槽, 覆盖式) ==========
    // 防止 showWorking 和 beginStep 同时调导致 UI 冲突
    // current=-1 表示无步骤进度 → 行为等价于旧 showWorking
    // current>=0 表示带进度 → 行为等价于旧 beginStep
    virtual void setStatus(const std::string& title,
                           int current = -1, int total = -1) = 0;

    // ========== ③ 工具反馈 ==========

    // 工具开始执行 (显示 "● tool_name(params)")
    virtual void onToolCall(const std::string& name, const std::string& params) = 0;
    // 工具执行结果 (显示 "  ⎿ ✓ ok" 或 "  ⎿ ✗ error")
    virtual void onToolResult(const std::string& name, const std::string& result, bool ok) = 0;

    // ========== ④ 交互 (调用前必须 setStatus("", -1) 清状态!) ==========

    // 高风险确认 — 阻塞, 期间禁止状态刷新
    virtual bool confirm(const std::string& prompt) = 0;
    // 多选 — CLI 用数字索引 [1]A [2]B, 返回 -1 表示取消/ESC
    virtual int askSelect(const std::vector<std::string>& options,
                          const std::string& prompt) = 0;
    // 自由文本输入 — nullopt 表示取消, "" 表示用户真输入了空串
    virtual std::optional<std::string> askInput(
        const std::string& prompt,
        const std::string& placeholder = "") = 0;

    // ========== ⑤ 中断机制 (信号驱动, 非轮询) ==========

    // 注册唯一中断回调 (后注册覆盖前注册).
    // 回调在 UI 线程触发 → 实现方必须保证线程安全 (atomic + mutex).
    // AgentLoop 应在 setOutput 时注册一次, 不重复调用.
    virtual void onInterrupt(std::function<void()> callback) = 0;

    // ========== ⑥ 异常通道 ==========

    // 错误输出 — UI 标红, 不影响正常输出流, Agent 继续运行
    virtual void emitError(const std::string& message) = 0;
    // 请求关闭 — 通知 UI 层准备退出, 由 AgentLoop 决定是否真退出 (可测试)
    virtual void requestShutdown(const std::string& reason) = 0;
};

} // namespace CLF::CLFTypes
```

### V4 最终版 vs 之前版本

| 变化 | 旧 | 新 (V4) | 原因 |
|:---|:---|:---|:---|
| 状态槽 | `showWorking` + `beginStep` 互斥未定义 | `setStatus(title, cur, total)` 统一 | 两个"瞬时状态"语义重叠 → 合并为覆盖式单槽 |
| 中断 | `isInterrupted()` 轮询 | `onInterrupt(cb)` 信号+线程安全文档 | 主线程阻塞时轮询失效; 补充回调覆盖+线程安全说明 |
| 交互取消 | `askSelect` 未定义取消 | `askSelect`→-1 取消, `askInput`→""取消 | CLI 下 ESC 退出选择不应抛全局异常 |
| 致命错误 | `onFatal` UI 直接 exit | `requestShutdown(reason)` 由 AgentLoop 决策 | 测试性更好, UI 层不越权 |
| 输出互斥 | `emitRaw`+`emitContent` 交替无约束 | emitRaw 前自动清状态, 结束时恢复 | 防止 \r 破坏换行内容 |
| 工具通道 | 工具元信息和业务数据混在一起 | 建议 `onToolCall/Result` 用不同颜色/前缀 | 用户需要区分操作记录 vs 真实输出 |

## 5. 关键改造

### CLFAgentLoop

```cpp
// 旧: static 直接调用
CLFTerminal::scrollPrint(text);
CLFTerminal::showThinking(seconds);
CLFConsole::checkEscape();

// 新: 注入接口 + 信号中断
class CLFAgentLoop {
    ICLFOutput* m_output = nullptr;
    std::atomic<bool> m_interrupted{false};
public:
    void setOutput(ICLFOutput* out) {
        m_output = out;
        // 注册中断回调: UI 层捕获 ESC → Kill 子进程 + 设标志
        m_output->onInterrupt([this]() {
            m_interrupted = true;
            if (m_httpClient) m_httpClient->abort();
        });
    }
    // 流式回调中:
    //   if (m_interrupted) throw InterruptError();
    //   m_output->emitContent(chunk);
};
```

### CLFTerminal

```cpp
class CLFTerminal : public CLF::CLFTypes::ICLFOutput {
    // ① 内容
    void emitContent(const std::string& t) override { scrollPrint(t); }
    // 第一期: 暂透传至 scrollPrint, 不处理 ANSI 进度条互斥
    void emitRaw(const std::string& d) override { scrollPrint(d); }

    // ② 状态 (统一槽: cur=-1 等价旧 showWorking, cur>=0 等价旧 beginStep)
    void setStatus(const std::string& title, int cur, int total) override {
        // 唯一的状态行刷新逻辑; cur=-1 时不显示进度
    }
    // 兼容旧代码的过渡方法 (内部均调 setStatus):
    void showThinking(int seconds) { setStatus("· Thinking… ("+std::to_string(seconds)+"s)"); }
    void showWorking(const std::string& t) { setStatus(t); }
    void clearStatus() { setStatus(""); }

    // ③ 工具 (🆕 第一期空实现, 第二期激活)
    void onToolCall(const std::string& n, const std::string& p) override {}
    void onToolResult(const std::string& n, const std::string& r, bool ok) override {}

    // ④ 交互 (需要 CLFRepl* 引用 — 见实施提醒#2)
    bool confirm(const std::string& p) override;
    int  askSelect(const std::vector<std::string>& opts, const std::string& p) override { return -1; }
    std::optional<std::string> askInput(const std::string& p,
                                        const std::string& def) override { return std::nullopt; }

    // ⑤ 中断
    void onInterrupt(std::function<void()> cb) override { m_interruptCb = std::move(cb); }

    // ⑥ 异常
    void emitError(const std::string& m) override { scrollPrint(red(m) + "\n"); }
    void requestShutdown(const std::string& r) override { /* 🆕 空实现 */ }

    void setRepl(CLFRepl* repl) { m_repl = repl; }  // 用于 confirm 委托
private:
    std::function<void()> m_interruptCb;
    CLFRepl* m_repl = nullptr;
};
```

### main.cpp 组装

```cpp
CLF::CLFUI::CLFTerminal terminal;             // UI 实现 ICLFOutput
CLF::CLFCore::CLFAgentLoop agent(config);
agent.setOutput(&terminal);                   // 注入输出通道
CLF::CLFTools::registerBuiltinTools(agent);
CLF::CLFUI::CLFRepl repl(agent, historyDir);  // REPL 也持有 terminal 引用
```

## 6. 实施策略：分两期

### 第一期（当前）：基础输出通道解耦

目标：切断 `clf_core → clf_ui` 链接依赖。

只实现 V4 接口中 **CLFCode 当前已用到的方法**，其余给空实现：

| 接口方法 | 替换当前调用 | 实现方式 |
|:---|:---|:---|
| `emitContent(text)` | `CLFTerminal::scrollPrint(text)` | 直接委托 |
| `emitRaw(data)` | （新增）透传子进程输出 | 暂委托 scrollPrint (第一期) |
| `setStatus(title,cur,total)` | 合并 `showWorking` + `showThinking` + `clearStatus` + `beginStep` | cur=-1: 等效旧 showWorking; cur>=0: 等效 beginStep; title="": 等效 clearStatus |
| `confirm(prompt)` | `CLFRepl::confirmDialog(prompt)` | 委托 REPL (需注入 CLFRepl*) |
| `onInterrupt(cb)` | 替代 `CLFConsole::checkEscape()` | 注册回调，ESC 时触发 |
| `emitError(msg)` | （新增）错误通道 | 委托 scrollPrint + red() |
| `onToolCall/onToolResult` | 暂无调用方 | 🆕 空实现 |
| `askSelect/askInput` | 暂无调用方 | 🆕 空实现 (askInput→nullopt, askSelect→-1) |
| `requestShutdown` | 暂无调用方 | 🆕 空实现 |

### 第二期（后续）：功能增强

逐步激活新增接口方法：
- `setStatus(cur, total)` 的 cur/total 参数 → 多步骤任务进度 (替代旧 beginStep)
- `onToolCall/onToolResult` → 替换 `CLFToolExecutor` 中硬编码的输出
- `emitError` → 统一错误输出通道
- `askSelect` → 丰富交互（CLI 数字索引）
- `emitRaw` → 长构建子进程 stdout 透传 (实现互斥 + ANSI 进度条保护)
- `requestShutdown` → 收到致命错误时的优雅退出

### 实施步骤（第一期）

| # | 内容 |
|:---|:---|
| 1 | 创建 `CLFTypes/ICLFOutput.hpp`（V4 完整接口 + InterruptError 类） |
| 2 | `CLFAgentLoop` 添加 `setOutput(ICLFOutput*)` + `m_interrupted` + 中断回调注册 |
| 3 | `CLFToolExecutor` 接受 `ICLFOutput*`，emitContent 替代 scrollPrint |
| 4 | `CLFThinkingIndicator` 接受 `ICLFOutput*`，替代直接 CLFTerminal 调用 |
| 5 | `CLFTerminal` 实现 `ICLFOutput`（继承 + 委托现有方法 + 新方法空实现） |
| 6 | `CLFRepl` 持有 `ICLFOutput*`（用于 emitContent），`CLFTerminal` 持有 `CLFRepl*`（用于 confirm 委托，见提醒#2）|
| 7 | `main.cpp` 创建 Terminal → 注入各组件 → 启动 REPL |
| 8 | CMake 更新：`clf_core` 移除 `clf_ui` 链接，`clf_network` 不依赖 `clf_ui` |
| 9 | 构建 + 测试验证 |
| 10 | 🆕 创建 `MockOutput` 类（继承 ICLFOutput, 记录调用序列），在 `qa_CLFAgentLoop` 中验证 |

### 实施提醒（不是设计问题，但不注意会重构）

**1. CLFTerminal 新旧状态接口的统一底层**

`setStatus` 是统一接口，但第一期历史代码可能还在调旧的 `showWorking`。确保 CLFTerminal 内部走同一个状态槽：

```cpp
void CLFTerminal::showWorking(const std::string& t) { setStatus(t, -1, -1); }
void CLFTerminal::setStatus(const std::string& t, int cur, int total) {
    // 唯一的状态行刷新逻辑, cur=-1 时不显示进度
}
```

**2. confirm 的双向注入**

`CLFTerminal::confirm()` 需要委托 `CLFRepl::confirmDialog()`，但二者互不知晓。需要双向注入：

- `CLFTerminal::setRepl(CLFRepl*)` — Terminal 获得 REPL 引用用于 confirm 委托
- `CLFRepl` 构造时接受 `ICLFOutput*` — REPL 获得输出接口用于 emitContent
- `main.cpp` 负责双向装配：
  ```cpp
  CLF::CLFUI::CLFTerminal terminal;
  CLF::CLFUI::CLFRepl repl(agent, historyDir, &terminal);  // REPL ← ICLFOutput
  terminal.setRepl(&repl);                                   // Terminal ← CLFRepl*
  ```
- `confirm` 期间：暂停 REPL 主循环 → 独占 stdin → setStatus("") 清状态行 → 返回结果 → 恢复 REPL

**3. CLFConsole::checkEscape 的退役路径**

`onInterrupt` 替代了 `checkEscape`。实施时：

- 检查 `CLFConsole` 是否还有其他调用方（颜色输出？光标控制？）
- 如果只有 `checkEscape` 被外部使用 → 删掉该方法，完全走 `onInterrupt`
- 如果 CLFConsole 还被 CLFTools/CLFNetwork 引用 → 保留为 CLFUI 私有辅助类
- 目标：第一期结束后，`CLFCore/` 无 `#include "CLFConsole.hpp"`

**4. emitRaw 第一期不实现互斥**

第一期 `emitRaw` 只是 `scrollPrint(data)`，不处理 ANSI 进度条。注释中标注：

```cpp
// 第一期: 暂透传至 scrollPrint，不处理 ANSI 进度条互斥
// 第二期: 实现自动清状态 + 原始模式切换
void emitRaw(const std::string& data) override { scrollPrint(data); }
```

**5. CLFCommandDispatcher 的第一期处理**

`CLFCommandDispatcher` 也直接调 `scrollPrint`。第一期方案：
- 给它也注入 `ICLFOutput*`（通过构造函数或 setter）
- 所有 `CLFTerminal::scrollPrint()` 替换为 `m_output->emitContent()`
- CLFCommandDispatcher 已在 CLFUI/ 目录，不破坏依赖方向 |

## 7. 实施注意事项 (阻塞/高风险问题解决方案)

### B1. CLFRepl 构造期不得调用 ICLFOutput 方法 ✅

`CLFTerminal` 和 `CLFRepl` 存在双向注入（Terminal→Repl, Repl→Output），构造顺序为：
```cpp
CLFTerminal terminal;           // ① 先构造
CLFRepl repl(agent, historyDir); // ② 构造 (不可调 terminal 方法!)
terminal.setRepl(&repl);         // ③ 注入完成
```

**约束**：CLFRepl 构造函数必须"安静"——不得调用任何 ICLFOutput 方法（包括 emitContent、setStatus）。所有输出（banner 等）应在 `run()` 中完成。

### B2. requestShutdown 最小可行实现 ✅

第一期 `CLFTerminal::requestShutdown(reason)` 输出 `[FATAL]` 到 `cerr` + 设置 `m_shutdownRequested` 标志。CLFRepl 主循环检查该标志后 `break` 退出。

### H3. onInterrupt 回调生命周期 ✅

- `ICLFOutput::onInterrupt` 支持传入 `nullptr` 清空回调
- `CLFAgentLoop::~CLFAgentLoop()` 中调用 `m_output->onInterrupt(nullptr)`
- 确保 AgentLoop 析构后，terminal 不再持有悬空 lambda

### H1. emitRaw 第一期行为 ✅

第一期 `emitRaw` 直接委托 `scrollPrint`，不处理互斥。**第一期不应有任何调用路径触发 emitRaw**——CLFCommandExec 的输出走 `onToolResult`。

### H2. CLFConsole 退役路径

第一期后 `CLFCore/` 无 `#include "CLFConsole.hpp"` ✅。CLFConsole 保留在 CLFUI/ 作为 `CLFTerminal` 的私有实现细节。

## 8. 验证标准

1. `clf_core` 不链接 `clf_ui`（依赖方向单向 ✅）
2. `CLFCore/` 目录下无 `#include "CLFUI/..."` ✅
3. 构建通过 + 全量测试通过
4. 运行 CLFCode 功能无退化
5. 🆕 `qa_CLFAgentLoop` 测试中使用 `MockOutput` 验证：
   - 流式响应每个 chunk 都调用了 `emitContent`
   - 工具执行先后调用了 `onToolCall` → `onToolResult`
   - 中断触发时抛出了 `InterruptError`
   - 任何错误路径都不直接调 `CLFTerminal::red()` 等 UI 函数
   → 这是解耦后的核心收益

## 附录A：各步骤验收测试

| 步骤 | 验收方式 | 通过标准 |
|:---|:---|:---|
| 1 | 编译 | `ICLFOutput.hpp` 独立编译通过，零项目依赖 |
| 2 | MockOutput 单元测试 | `setOutput()` 后 `onInterrupt` 回调被注册 |
| 3 | 集成测试 | `CLFToolExecutor` 调用 `emitContent` 而非 `scrollPrint` |
| 4 | 集成测试 | `CLFThinkingIndicator` 调用 `setStatus` 而非 `showThinking` |
| 5 | 编译 | `CLFTerminal` 继承 `ICLFOutput`，所有纯虚方法有实现 |
| 6 | 手动测试 | confirm 对话框正常弹出，ESC/Enter 行为正确 |
| 7 | 手动测试 | 程序启动正常，banner+固定区 显示正常 |
| 8 | 编译+链接 | `clf_core` 不链接 `clf_ui`，`CLFCore/` 无 `"CLFUI/"` include |
| 9 | ctest | 全量 6 个测试通过 |
| 10 | MockOutput 单元测试 | emitContent/onToolCall/onToolResult 调用序列验证通过 |

## 附录B：SOLID 合规性分析

| 原则 | 评估 | 说明 |
|:---|:---|:---|
| **S** 单一职责 | ⚠️ 改善中 | CLFAgentLoop 仍有编排+重试+持久化, 但 UI 职责已移除; CLFTerminal 仍是上帝对象但被 ICLFOutput 约束 |
| **O** 开闭原则 | ✅ | ICLFOutput 对扩展开放(新实现), 对修改关闭(接口定稿) |
| **L** 里氏替换 | ✅ | MockOutput 可替换 CLFTerminal, 测试和生产行为一致 |
| **I** 接口隔离 | ⚠️ 可接受 | 13 方法偏多, 但当前拆分会增加注入复杂度; 未来可按 内容/状态/工具/交互/控制 五组拆分 |
| **D** 依赖倒置 | ✅ | CLFCore 依赖抽象(ICLFOutput), CLFUI 实现抽象; main 做依赖注入 |

| 模块 | 职责数 | 评价 |
|:---|:---|:---|
| CLFTypes | 1 (类型定义+输出接口) | ✅ |
| CLFNetwork | 2 (HTTP传输+思考指示器) | ✅ |
| CLFCore | 5 (编排+上下文+安全+会话+工具执行) | ⚠️ 可继续拆, 本次不动 |
| CLFTools | 1 (工具实现) | ✅ |
| CLFUI | 4 (渲染+输入+REPL+命令) | ⚠️ 第二期可拆, 本次不动 |

## 附录C：CLI 子进程策略

### 当前 CLFCode 实现

`CLFCommandExec.cpp` 使用 `CreateProcess` + 匿名管道 + `PeekNamedPipe` 轮询，**捕获输出**后再通过 `emitContent` 转发。属于 `exec` 模式。

### 建议保持的策略

| 场景 | 策略 | 通过接口 |
|:---|:---|:---|
| 短命令 (< 10s, 少量输出) | `exec` 捕获 → `onToolResult` | 有超时控制、可截断 |
| 长构建 (make, npm install) | `spawn` 继承 stdio → `emitRaw` 透传 | 保留 ANSI 进度条 |
| 交互式命令 (vim, ssh) | 不通过 Agent 执行 | 安全策略直接拒绝 |

### 关键约定

1. `emitRaw` 输出的数据**不加任何前缀**（破坏 ANSI 转义码）
2. `exec` 捕获模式必须处理 `maxBuffer` 超限 → `emitError`
3. 子进程超时 Kill 后必须调用 `clearStatus` 退出瞬时状态行
