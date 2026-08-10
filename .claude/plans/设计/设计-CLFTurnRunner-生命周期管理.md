# 设计：CLFTurnRunner — Turn 生命周期管理

> 状态：设计中 | 创建：2026-08-10

## 〇、动机

`CLFAgentLoop::runTurn()` 当前承担了过多职责：API 调用、工具执行、思考计时、中断处理、重试逻辑、内容输出——全挤在一个函数里，200+ 行，违反单一职责。

提取 `CLFTurnRunner`，只做一件事：**管理一次 turn 从开始到结束的完整生命周期**。

## 一、职责边界

| 类 | 负责 | 不负责 |
|----|------|--------|
| **CLFTurnRunner** | turn 计时、API 调用、工具执行循环、重试/中断、emit 生命周期标记 | 消息构建、上下文管理、工具注册、协议解析细节 |
| **CLFAgentLoop** | 上下文准备、请求体构建、结果记录、工具注册 | turn 内部计时和执行循环 |

## 二、双计时器设计

### 2.1 Timer #1：每动作计时（内容区）

| 动作类型 | 进行中（状态栏） | 完成后（内容区） |
|----------|-----------------|-----------------|
| API 思考 | `· Thinking… (5s)` | `  Thought for 9s (ctrl+o to expand)` |
| 工具执行 | （不显示，工具本身已有 `● tool(args)` 行 + 结果 ✓） | （融入工具输出流） |

- Timer #1 通过已有的 `CLFThinkingIndicator`（`setStatus`）实现
- 工具执行不计时（用户可见 `● tool → ✓` 不需要额外计时条目）

### 2.2 Timer #2：整体任务计时（底部 + 内容区）

| 阶段 | 显示位置 | 内容 |
|------|---------|------|
| turn 运行中 | modeLine（底部状态栏） | `Working for 55s…` |
| turn 完成 | 内容区 | `  Worked for 1m55s` |

- Timer #2 由 `CLFTurnRunner` 启动和停止
- 运行中：通过 `setStatus()` 持续更新
- 完成后：`emitContent` 写入内容区

## 三、接口设计

```cpp
// CLFTurnRunner.hpp
namespace CLF::CLFCore {

class CLFTurnRunner {
public:
    struct Config {
        CLF::CLFNetwork::ICLFHttpClient* httpClient = nullptr;
        CLFToolExecutor* toolExecutor = nullptr;
        CLF::CLFTypes::ICLFOutput* output = nullptr;
        CLFRetryPolicy retryPolicy;
        int maxToolCallIterations = 16;
        bool stream = true;
        std::atomic<bool>* interruptFlag = nullptr;
    };

    struct Result {
        std::string finalContent;
        std::string error;
        bool interrupted = false;
        int toolCallIterations = 0;
    };

    explicit CLFTurnRunner(Config config);
    ~CLFTurnRunner();

    // 运行一次 turn
    // requestBody: chat completion JSON 请求体
    // buildRequestBody: 工具执行后重建请求体（AgentLoop 提供）
    // onContentDelta: 流式内容增量回调（AgentLoop 注入 reasoning 处理等）
    Result run(const std::string& requestBody,
               std::function<std::string()> buildRequestBody,
               std::function<void(const std::string& delta)> onContentDelta = nullptr);

private:
    Config m_config;
};

} // namespace CLF::CLFCore
```

## 四、内部流程

```
CLFTurnRunner::run(requestBody, buildRequestBody, onContentDelta):

  turnStart = now()
  启动 Timer #2 线程（每秒 setStatus("Working for Xs…")）

  while (iterations < maxIterations):
    // ===== API 等待阶段 =====
    启动 CLFThinkingIndicator（Timer #1）
    response = httpClient->postJsonStream(requestBody, chunkCb)
    停止 CLFThinkingIndicator
    → emitContent("  Thought for Xs (ctrl+o to expand)\n")

    // ===== 错误/中断/重试 =====
    if (hadError):
      if (fatal) → return error
      retry with backoff; continue
    if (interrupted):
      → return interrupted

    // ===== 工具执行阶段 =====
    if (hasToolCalls):
      results = toolExecutor->execute(toolCalls)
      // toolExecutor 内部输出 ● tool / diff / ✓
      requestBody = buildRequestBody()    // AgentLoop 重建请求（含工具结果）
      ++iterations
      continue

    // ===== 完成 =====
    break

  停止 Timer #2 线程
  elapsed = now() - turnStart
  emitContent("\n  Worked for " + formatDuration(elapsed) + "\n\n")

  return Result{finalContent, ...}
```

## 五、CLFAgentLoop 瘦身

```
改造前 runTurn()：~200 行，混合所有逻辑

改造后 runTurn():
  CLFTurnRunner::Config cfg{...};
  CLFTurnRunner runner(cfg);

  auto body = m_protocolAdapter.buildChatRequest(...);   // AgentLoop 职责

  auto result = runner.run(body, [&]() {                 // 交给 runner
      // 工具执行后重建请求
      m_context.addMessage("assistant", content, toolCalls);
      for (auto& r : results)
          m_context.addMessage("tool", r.m_content, r.m_toolCallId, r.m_name);
      return m_protocolAdapter.buildChatRequest(...);
  }, [&](const std::string& delta) {
      // reasoning 处理（AgentLoop 特有逻辑）
      ...
  });

  if (!result.finalContent.empty())
      m_context.addMessage("assistant", result.finalContent);
  return result.finalContent;
```

## 六、实施步骤

| 步骤 | 内容 | 涉及文件 |
|------|------|----------|
| Step 1 | 新增 `CLFTurnRunner.hpp/.cpp` | 新文件 |
| Step 2 | 瘦身 `CLFAgentLoop::runTurn()` | `CLFAgentLoop.cpp` |
| Step 3 | `CLFRepl` modeLine 显示 Timer #2 | `CLFRepl.cpp` |
| Step 4 | CMakeLists 注册新文件 | `src/CMakeLists.txt` |
| Step 5 | 构建 + 测试 | — |

## 七、不在此方案范围

- diff 颜色修复（独立 issue，走 `ICLFOutput::emitLine(style)` 方案）
- 工具间间距（`CLFToolExecutor` 微调，与 CLFTurnRunner 无关）
- thinking 标签可配置（后续扩展 `CLFThinkingIndicator::setLabel()`）
