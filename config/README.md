# 配置文件说明

## agent_settings.json

Agent 运行时核心配置。API Key **严禁**写入此文件，应通过以下方式提供：

1. **环境变量**：设置 `DEEPSEEK_API_KEY`
2. **本地覆盖文件**：创建 `agent_settings.local.json`（已在 `.gitignore` 中忽略）

### 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `api.base_url` | string | API 服务地址 |
| `api.api_key` | string | API 密钥（**留空**，通过环境变量注入） |
| `api.model` | string | 模型名称 |
| `api.max_tokens` | int | 单次请求最大 token 数 |
| `api.temperature` | float | 生成温度（0.0 = 确定性输出） |
| `agent.max_context_window` | int | 上下文窗口大小 |
| `agent.max_tool_calls_per_turn` | int | 每轮最大工具调用次数 |
| `agent.enable_thinking` | bool | 是否启用思考模式 |
| `logging.level` | string | 日志级别：debug / info / warn / error |
| `logging.file` | string | 日志文件路径 |
