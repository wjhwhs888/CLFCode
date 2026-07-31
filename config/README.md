# CLFCode 配置文件说明

`agent_settings.json` 是 CLFCode 的运行时配置文件，遵循 DeepSeek Chat Completions API 规范。

API Key **严禁**提交到 Git。推荐通过环境变量注入：`set CLF_API_KEY=sk-xxx`

---

## 配置结构

```
{
    "connection": { ... },       // 连接认证
    "chat_completions": { ... }, // 对齐 DeepSeek API 参数
    "agent": { ... },            // Agent 自身行为
    "logging": { ... }           // 日志
}
```

---

## 一、connection — 连接认证

连接信息。换模型/换 Provider 时通常只改这里。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `base_url` | string | `https://api.deepseek.com` | API 服务地址，末尾不带 `/`。例如切换到 OpenAI：`https://api.openai.com` |
| `api_key` | string | `""` | API 密钥。**留空**，通过环境变量 `CLF_API_KEY` 注入。本地测试可临时填写，但切勿提交 Git |

---

## 二、chat_completions — Chat API 参数

完全对齐 [DeepSeek Chat Completions API](https://api-docs.deepseek.com/api/create-chat-completion) 参数。

### 核心参数

| 字段 | 类型 | 默认值 | DeepSeek 参数 | 说明 |
|------|------|--------|:---:|------|
| `model` | string | `deepseek-v4-pro` | `model` | **主模型**。可选：`deepseek-v4-pro`（1.6T MoE，49B 活跃）、`deepseek-v4-flash`（284B，13B 活跃）。复杂推理/工具调用用 v4-pro |
| `sub_model` | string | `deepseek-v4-flash` | — | **副模型**（CLF 自定义）。用于轻量任务：摘要生成、上下文压缩、标题提取等。推荐用 v4-flash（速度快、成本低） |
| `max_tokens` | int | `8192` | `max_tokens` | 单次响应最大输出 token 数。V4 模型上限 384K。Coding 场景 8192 已够用，长文本生成可调大 |
| `temperature` | float | `0.0` | `temperature` | 采样温度，范围 0.0~2.0。**0.0 = 确定性输出**（推荐 coding 场景），值越大越随机（创意写作可调到 0.7~1.0） |

### 采样控制

| 字段 | 类型 | 默认值 | DeepSeek 参数 | 说明 |
|------|------|--------|:---:|------|
| `top_p` | float | `1.0` | `top_p` | Nucleus sampling 阈值，范围 0.0~1.0。1.0 表示考虑所有 token，0.1 表示只考虑概率最高的前 10%。**建议与 `temperature` 二选一调整**，不要同时改 |
| `frequency_penalty` | float | `0.0` | `frequency_penalty` | 频率惩罚，范围 -2.0~2.0。**正值**：降低已出现 token 的概率，减少字面重复。**负值**：增加重复概率。代码生成不推荐使用 |
| `presence_penalty` | float | `0.0` | `presence_penalty` | 存在惩罚，范围 -2.0~2.0。**正值**：鼓励讨论新话题，适用于 brainstorm 场景。**负值**：倾向重复已提过的话题 |

### 输出控制

| 字段 | 类型 | 默认值 | DeepSeek 参数 | 说明 |
|------|------|--------|:---:|------|
| `stop` | array | `[]` | `stop` | 停止序列。模型生成遇到数组中任一字符串时立即停止，该字符串不会出现在输出中。最多 16 个。空数组 = 不限制。例如 `["```"]` 可防止代码块未闭合 |
| `response_format` | string | `text` | `response_format` | 输出格式。`"text"`：普通文本（默认）。`"json_object"`：强制 JSON 输出（**需要 prompt 中明确要求 JSON**，否则 API 报错） |
| `stream` | bool | `false` | `stream` | 是否启用 SSE 流式输出。当前默认 `false`（同步模式），流式响应功能实现后改为 `true` |
| `thinking_level` | string | `max` | — | **思考模式等级**（CLF 自定义）。`off`：不思考。`low` / `medium` / `high` / `max`：思考深度递增。当前仅 `deepseek-v4-pro` 模型支持 |

---

## 三、agent — Agent 行为参数

CLFCode 自身的运行行为参数，**不传给 API**。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `max_context_window` | int | `1048576` | 上下文窗口大小（token 估算值）。1048576 = 1M。超出部分自动截断（保留最新消息） |
| `max_tool_call_iterations` | int | `16` | 单轮对话中 tool-calling 最大循环次数。防止模型持续请求工具形成死循环 |
| `context_compression` | bool | `false` | 是否启用上下文压缩。`true`：当上下文接近窗口上限时，调用 `sub_model` 对历史消息进行摘要压缩（**功能待实现**） |
| `max_response_delay_sec` | int | `300` | 单次 API 请求最大等待时间（秒）。300 = 5 分钟。超时后返回错误信息 |
| `interaction_language` | string | `zh-CN` | 默认交互语言。用于 system prompt 中的语言指令，指导模型用目标语言回复 |

---

## 四、logging — 日志参数

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `level` | string | `info` | 日志级别：`debug` / `info` / `warn` / `error`。`debug` 包含完整请求/响应内容 |
| `file` | string | `clf_agent.log` | 日志文件路径。相对路径基于工作目录，建议用绝对路径 |

---

## 五、环境变量覆盖

以下环境变量优先级**高于**配置文件，用于 CI/CD 或容器化部署：

| 环境变量 | 覆盖字段 | 示例 |
|----------|---------|------|
| `CLF_API_KEY` | `connection.api_key` | `set CLF_API_KEY=sk-xxx` |
| `CLF_API_BASE_URL` | `connection.base_url` | `set CLF_API_BASE_URL=https://api.openai.com` |
| `CLF_MODEL` | `chat_completions.model` | `set CLF_MODEL=deepseek-v4-pro` |

---

## 六、完整示例

```
{
    "connection": {
        "base_url": "https://api.deepseek.com",
        "api_key": ""
    },
    "chat_completions": {
        "model": "deepseek-v4-pro",
        "sub_model": "deepseek-v4-pro",
        "max_tokens": 8192,
        "temperature": 0.0,
        "top_p": 1.0,
        "frequency_penalty": 0.0,
        "presence_penalty": 0.0,
        "response_format": "text",
        "stop": [],
        "stream": false,
        "thinking_level": "max"
    },
    "agent": {
        "max_context_window": 1048576,
        "max_tool_call_iterations": 16,
        "context_compression": false,
        "max_response_delay_sec": 300,
        "interaction_language": "zh-CN"
    },
    "logging": {
        "level": "info",
        "file": "clf_agent.log"
    }
}
```

---

## 七、切换其他模型 Provider

CLFCode 的协议适配器输出 OpenAI 兼容格式。切换到兼容 Provider 只需修改 `connection`：

**OpenAI：**
```
"connection": {
    "base_url": "https://api.openai.com",
    "api_key": ""
}
```

**其他兼容服务（Ollama / vLLM / 等）：**
```
"connection": {
    "base_url": "http://localhost:11434",
    "api_key": "not-needed"
}
```

> `chat_completions.model` 改为对应 provider 的模型名即可。
