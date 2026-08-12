# CLFCode 系统架构设计

> 更新日期：2026-07-31 | 对应提交：master 最新（e716af2 之后）

## 1. 概述

CLFCode 是一个本地运行的 AI Coding Agent。用户通过终端 REPL 交互，Agent 通过 HTTP 调用 DeepSeek API（OpenAI 兼容协议），利用工具层（文件操作、命令执行）完成开发任务。

## 2. 架构图

```
┌──────────────────────────────────────────────────┐
│                    main.cpp                       │
│          入口编排：配置加载 · 工具注册 · REPL       │
└──────┬───────────────┬────────────────┬──────────┘
       │               │                │
┌──────▼─────────┐ ┌───▼────────────┐ ┌─▼───────────┐
│  CLFCore       │ │  CLFTools      │ │ CLFNetwork  │
│  （Agent 核心） │ │  （工具层）     │ │ （通信层）   │
│                │ │                │ │             │
│ CLFAgentLoop   │ │ CLFBuiltinTools│ │ CLFHttpClient│
│  （调度主循环） │ │  （内置工具）   │ │ （HTTP/SSE） │
│ CLFContext     │ │ CLFFileOps     │ │             │
│  （上下文管理） │ │ CLFCommandExec │ │             │
│ CLFProtocolAdapter              │ │             │
│  （协议适配）  │ │                │ │             │
│ CLFConfigLoader│ │                │ │             │
│  （配置加载）  │ │                │ │             │
│ CLFSkillLoader │ │                │ │             │
│  （知识库）    │ │                │ │             │
│ CLFSecurityPolicy │              │ │             │
│  （安全策略）  │ │                │ │             │
│ CLFLogger      │ │                │ │             │
│  （日志）      │ │                │ │             │
│ CLFStreamAccumulator │            │ │             │
│  （流式累积）  │ │                │ │             │
└───────────────┘ └────────────────┘ └─────────────┘
```

**依赖方向**：`clf_tools` → `clf_core`；`clf_network` 独立；`main` 组装三者（无环依赖 ADP）。

## 3. 模块职责

### main — 入口编排
- 确定项目根目录（`findProjectRoot`：从 exe 向上找 CMakeLists.txt）
- 加载配置（`.local.json` → 环境变量 → `agent_settings.json`）
- 初始化日志系统、加载知识库、注册内置工具
- 注入高风险工具确认回调（终端 y/n）
- 启动 REPL 循环 + 命令处理（/exit /help /clear /skill /mode）

### CLFCore — Agent 核心

| 类 | 职责 |
|----|------|
| `CLFAgentLoop` | 主循环：tool-calling 多轮循环 + 流式/同步双模式 + 安全策略检查 |
| `CLFContext` | 对话历史、token 估算（ASCII 0.25/字、CJK 1.5/字）、system 永不截断、长消息保护 |
| `CLFProtocolAdapter` | OpenAI 兼容协议 JSON 序列化/反序列化（请求构建 + 响应解析） |
| `CLFConfigLoader` | 配置解析 + 环境变量覆盖 + 项目根目录管理 |
| `CLFSkillLoader` | data/skills/ 规则文件加载（L1 常驻、L2/L3 按需注入） |
| `CLFSystemPromptBuilder` | System Prompt 构建器：模板/动态上下文/Git/项目规则/Token 预算 → 单条 system 消息 |
| `CLFSecurityPolicy` | 四模式安全策略（auto/analyze/edit/manual） |
| `CLFLogger` | 单例日志：级别过滤 + 时间戳 + 文件/控制台输出 |
| `CLFStreamAccumulator` | SSE 流式 delta 累积（文本 + tool_calls 分片合并） |

### CLFTools — 工具层
- `CLFBuiltinTools`：6 个内置工具统一注册（read_file / write_file / list_directory / execute_command / get_current_time / echo）
- `CLFFileOps`：文件读写、目录列举
- `CLFCommandExec`：shell 命令执行（临时文件捕获输出 + 超时检测）

### CLFNetwork — 通信层
- `CLFHttpClient`：封装 cpp-httplib，同步 POST + SSE 流式 POST（跨 chunk 行缓冲）

## 4. 数据流

```
用户输入 → main → CLFAgentLoop ──(buildChatRequest)──→ CLFHttpClient ──→ DeepSeek API
                     ↑                                      │
                     │                                (stream response)
                     │                                      │
                     └──(tool result)── CLFTools ←── tool_calls
                              │
                              ├──(安全策略检查)── 阻断/确认
                              ├──(command)──→ 本地 shell
                              └──(file)─────→ 本地文件系统
```

## 5. 关键设计决策

| 决策 | 说明 |
|------|------|
| 协议独立类 | `CLFProtocolAdapter` 负责 JSON 格式，AgentLoop 只管编排（SRP） |
| 配置驱动 | 全部行为参数从 agent_settings.json 读取，零硬编码 |
| 流式/同步 | `config.stream` 切换，同一 tool-calling 循环复用 |
| 安全四模式 | 读永不限制，写/命令按模式放行/阻断/确认 |
| System Prompt | CLFSystemPromptBuilder 构建：模板文件（可编辑）→ 动态上下文（Git/OS/Shell）→ 项目规则（PROJECTRULES.md）→ L1 宪法 + Skills → Token 预算，合并为单条 system 消息 |
| 路径统一 | 所有路径基于项目根目录（findProjectRoot） |

## 6. 扩展性设计

- 新工具：实现 `CLFTool` 结构（名称/描述/Schema/风险等级/handler）→ `registerTool()` 注册
- 新协议：`CLFProtocolAdapter` 内加方法（如 Responses API），不影响调用方
- 新模型 Provider：只改 `connection.base_url` + `model`
- 新知识规则：往 `data/skills/` 放 .md 文件即可
