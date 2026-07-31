# 归档-API协议适配

> 状态：✅ 已实现并验证 | 提交：ef2ddc9

## 背景

CLFCode 使用 DeepSeek API（OpenAI 兼容 Chat Completions 协议），需实现 tool_calling 协议适配，使 Agent 能注册工具、接收模型 tool_calls、执行并回传结果。

## 设计决策

| 决策 | 结论 |
|------|------|
| 协议层独立类 | `CLFProtocolAdapter`（SRP：JSON 序列化/反序列化） |
| 消息模型 | 扁平结构体 + 可选字段（不搞继承/虚函数） |
| 参数 Schema | `std::string` 存 JSON Schema（避免头文件依赖 nlohmann/json） |
| 请求参数 | 直接传 `CLFAgentConfig`（加字段不用改接口） |
| 实现顺序 | 先同步后流式（流式 delta 累积归入流式响应问题） |

## 实现内容

### 核心文件

| 文件 | 职责 |
|------|------|
| `CLFProtocolAdapter.hpp/.cpp` | `buildChatRequest()` / `parseAssistantResponse()` / 消息序列化 |
| `CLFContext.hpp/.cpp` | `CLFMessage` 扩展：`m_toolCalls` / `m_toolCallId` / `m_name` |
| `CLFAgentLoop.cpp` | `runTurn()` tool-calling 循环 + `executeTools()` |
| `CLFConfigLoader.hpp/.cpp` | 配置加载（JSON 文件 + 环境变量覆盖 + 多路径查找） |

### runTurn 循环算法

```
addMessage("user", input)
loop (max 16):
    buildChatRequest(messages, tools, config) → POST
    parseAssistantResponse()
    has content → accumulate
    has tool_calls:
        addAssistantToolCalls() → executeTools() → addToolResult() 逐个
        continue
    addMessage("assistant", finalContent); return
```

### 配置对齐 DeepSeek API（100% 参数覆盖）

`model` / `messages` / `temperature` / `max_tokens` / `top_p` / `frequency_penalty` / `presence_penalty` / `stop` / `response_format` / `stream` / `tools` / `tool_choice`

### 配置安全策略

- `agent_settings.json`：模板（api_key 空），提交 Git
- `agent_settings.local.json`：真实配置（含 key），`.gitignore` 保护
- 加载优先级：`.local.json` → 环境变量 → `agent_settings.json`

## 遇到的坑（已解决）

1. Release 模式崩溃（0xC0000409）→ 未捕获异常 + 控制台 GBK 编码，`SetConsoleCP(CP_UTF8)` + 全局 try-catch
2. `std::tm` 未初始化 → 零初始化 + `localtime_s` 返回值检查
3. 模型自称 Claude → 注入身份提示词（CLFCode，独立 Agent 产品）

## 验证

- `> 现在几点了` → 模型调用 get_current_time，返回时间 ✅
- 多轮工具调用循环正常 ✅
- 模型切换 `deepseek-v4-flash` 后一切正常 ✅
