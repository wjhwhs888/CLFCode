# 问题-API协议适配

## 背景
CLFCode 使用 DeepSeek API 作为后端，需确认 DeepSeek 的 tool_calling 格式与 OpenAI 协议的兼容程度。

## 当前状态
- `CLFAgentLoop::parseToolCalls()` — TODO，空实现
- `CLFAgentLoop::executeTools()` — TODO，空实现
- `CLFAgentLoop::runTurn()` — 当前仅处理纯文本回复，不处理 tool_calls 字段

## 待解决
1. DeepSeek API 是否完全兼容 OpenAI tool_calling 格式（`tool_choice`、`tools` 数组、`tool_calls` 响应）
2. 流式响应中 tool_calls 的分片拼接（delta 累积）
3. `buildRequestBody` 需支持携带 tools 定义
4. 响应解析需区分：普通文本回复 / tool_calls 请求 / 混合场景

## 验证方式
- 用 DeepSeek API 文档对比 OpenAI tool_calling 协议
- 发送带 tools 定义的请求，检查返回的 tool_calls 结构
- 处理 tool_call_id 回传（tool role 消息格式）

## 涉及文件
- `src/CLFCore/CLFAgentLoop.cpp`
- `src/CLFCore/CLFAgentLoop.hpp`
- `src/CLFCore/CLFContext.hpp`（CLFMessage 结构可能需要扩展）
