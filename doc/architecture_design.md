# CLFCode 系统架构设计

> 更新日期：2026-09-21 | 对应提交：master 最新（v0.8.0 之后）

## 1. 概述

CLFCode 是一个本地运行的 AI Coding Agent。用户通过终端 REPL 交互，Agent 通过 HTTP 调用 DeepSeek API（OpenAI 兼容协议），利用工具层（文件操作、命令执行）完成开发任务。自 v0.8.0 起，插件系统全面生效：内置工具已迁为 4 个能力域可热插拔插件（文件操作/命令/搜索/网络），经 /plugin 命令管理；core 内建工具（任务清单/上下文压缩/时间/回显）保持静态注册。

## 2. 架构图

```
┌──────────────────────────────────────────────────┐
│                    main.cpp                       │
│          入口编排：配置加载 · 工具注册 · REPL       │
└──────┬───────────────┬────────────────┬──────────┘
       │               │                │
┌──────▼─────────┐ ┌───▼────────────┐ ┌─▼──────────────┐
│  CLFUI         │ │  CLFCore       │ │ CLFNetwork     │
│  （终端界面）   │ │  （Agent 核心） │ │ （通信层）      │
│ CLFRepl        │ │ CLFAgentLoop   │ │ CLFHttpClient  │
│ CLFReplView    │ │ CLFContext     │ │ （HTTP/SSE）    │
│ CLFInputHandler│ │ CLFProtocolAdapter               │
│ CLFTerminal    │ │ CLFConfigLoader│ │                │
│ CLFScrollView  │ │ CLFSystemPromptBuilder            │
│ CLFSelectionModel               │ │                │
│                │ │ CLFSecurityPolicy                │
│                │ │ CLFPluginManager                 │
│                │ │  （插件管理器） │ │                │
│                │ │ CLFLogger      │ │                │
└───────┬────────┘ └───────┬────────┘ └────────────────┘
        │                  │
        │           ┌──────▼──────────┐    ┌─────────────────────┐
        │           │ CLFCapabilities │    │ CLFPluginApi (ABI)  │
        │           │  （能力层）      │◄──►│ （跨 DLL 共享接口）  │
        │           │ FileOps / Diff  │    │ CLFPlugin/CLFHostApi│
        └──────────►└─────────────────┘    │ ICLFToolProvider    │
         CLFTools（工具层：BuiltinTools     └─────────┬───────────┘
         / CommandExec / Search / WebFetch）          │
                                                      ▼
                                           插件 DLL（阶段 2 迁出目标：
                                           tools.fileops / command /
                                           search / web / misc）
```

**依赖方向**：`clf_tools` → `clf_core` → `clf_capabilities` → `clf_types`；`clf_ui` → `clf_core`；`clf_network` 独立；`clf_plugin_api` 为接口目标（禁依赖其目录之外，插件与宿主共享）；`main` 组装（无环依赖 ADP）。

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
| `CLFPluginManager` | 插件管理器（阶段 2，v0.7.6）：扫描 plugins/ 目录 DLL → 版本闸门 → 服务名依赖图（拓扑/环检测）→ 加载/卸载/热替换 → 服务注册表路由；单插件失败隔离兜底 |
| `CLFHostApiImpl` | 宿主 API 实现（注入插件的宿主面）：日志/配置段读取/服务查询/跨边界内存通道 |

### CLFTools — 工具层
- `CLFBuiltinTools`：内置工具统一注册（read_file / write_file / edit_file / list_directory / execute_command / search_content / web_fetch / get_current_time / echo + todo_write / compress_context）
- `CLFCommandExec`：shell 命令执行（临时文件捕获输出 + 超时检测）
- `CLFSearchContent`：文本搜索（扩展名白名单 + 忽略目录 + 非法 UTF-8 行跳过）
- `CLFWebFetch`：URL 抓取（1MB 上限 + head/tail 截断 + 不携带凭据）

### CLFCapabilities — 能力层（C1 归属修正）
- `CLFFileOps`：文件读写、目录列举、边界校验
- `CLFDiff`：行级 diff（着色渲染数据源）

### CLFPluginApi — 插件 ABI（跨 DLL 唯一共享面，禁依赖其目录之外）
- `CLFPluginApi.hpp`：C 工厂符号 + 纯虚接口 + POD 服务表 + 版本化（禁 STL/异常/RTTI 跨边界）
- `CLFToolApi.hpp`：工具插件域（元数据 POD + 调用回调 + ICLFToolProvider）
- `CLFFileService.hpp`：文件能力域（回调推送模式）

### CLFNetwork — 通信层
- `CLFHttpClient`：封装 cpp-httplib，同步 POST + SSE 流式 POST（跨 chunk 行缓冲）

### CLFUI — 终端界面（FTXUI）
- `CLFRepl`：主循环编排（run/render/生命周期装配）；任务面板行构建
- `CLFReplView`：渲染器（内容区/状态行/滚动）
- `CLFInputHandler`：输入事件处理（提交/快捷键/拖选）
- `CLFTerminal`：ICLFOutput 四窄接口实现（内容/进度/交互/辅助）

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
