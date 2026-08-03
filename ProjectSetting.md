# CLFCode 工程结构设计

---

## 1. 目录结构

```
CLFCode/
├── CMakeLists.txt              # 顶层 CMake 构建入口
├── README.md                   # 项目整体说明（编译方法、依赖、快速开始）
├── CLAUDE.md                   # 项目级 AI 协作约定（继承全局规范）
├── LICENSE                     # 开源许可（MIT）
├── ProjectSetting.md           # 本文件 — 工程结构设计文档
├── .gitignore                  # Git 忽略规则
├── todo.md                     # 本地个人待办（不入库，.gitignore 排除）
│
├── .idea/                      # JetBrains IDE 配置（.gitignore 排除）
│
├── cmake-build-debug/          # CMake Debug 构建输出（.gitignore 排除）
├── cmake-build-release/        # CMake Release 构建输出（.gitignore 排除）
│
├── 3rdparty/                   # 第三方依赖库（Header-Only 库）
│   ├── httplib/
│   │   └── httplib.h           # cpp-httplib 头文件
│   ├── nlohmann/
│   │   └── json.hpp            # nlohmann/json 头文件
│   ├── boost-ut/
│   │   └── boost/ut.hpp        # Boost.UT 单头测试框架
│   └── README.md               # 记录各库的版本号与下载来源
│
├── config/                     # 运行时配置文件
│   ├── agent_settings.json     # Agent 核心配置模板（API Key 留空，由用户填写）
│   ├── agent_settings.local.json  # 本地配置覆盖（含 API Key，不提交 Git）
│   └── README.md               # 配置字段说明文档
│
├── data/                       # Agent 知识库数据（按需加载的 Skills）
│   ├── skills/
│   │   ├── constitution.md     # L1 编码宪法（始终加载的系统规则）
│   │   ├── architecture.md     # L2 架构设计原则库
│   │   ├── debug.md            # L3 Debug 标准工作流
│   │   ├── code_review.md      # L3 代码审查清单
│   │   └── unittest.md         # L3 单元测试脚手架生成规则
│   └── README.md               # 知识库文件索引与说明
│
├── doc/                        # 项目设计文档与运行时产出
│   ├── architecture_design.md  # 系统架构设计文档
│   ├── api_interface.md        # 模块接口定义
│   ├── README.md               # 文档导航
│   ├── log/                    # 运行日志输出（.gitignore 排除，仅保留 README）
│   │   ├── README.md
│   │   └── clf_agent.log       # Agent 运行日志
│   ├── debug/                  # 调试输出目录（.gitignore 排除，仅保留 README）
│   │   └── README.md
│   └── contextHistory/         # 会话历史存档（运行时自动生成，.gitignore 排除，仅保留 README）
│       ├── README.md
│       └── *.json              # 按时间戳命名的会话 JSON
│
├── src/                        # 项目源代码
│   ├── CMakeLists.txt          # 源码子目录构建文件
│   ├── main.cpp                # 程序入口（加载配置 → 初始化日志 → 创建 Agent → 启动 REPL）
│   ├── README.md               # 源码模块说明
│   │
│   ├── CLFCore/                # Agent 核心逻辑
│   │   ├── CLFAgentLoop.hpp    # Agent 主循环调度器（工具调用闭环）
│   │   ├── CLFAgentLoop.cpp
│   │   ├── CLFConfigLoader.hpp # 配置加载器（JSON 解析 + 环境变量覆盖）
│   │   ├── CLFConfigLoader.cpp
│   │   ├── CLFConsole.hpp      # 控制台输出辅助（彩色、进度指示）
│   │   ├── CLFConsole.cpp
│   │   ├── CLFContext.hpp      # 对话上下文管理器（消息历史、token 估算）
│   │   ├── CLFContext.cpp
│   │   ├── CLFLogger.hpp       # 日志系统（级别过滤、文件/控制台双通道）
│   │   ├── CLFLogger.cpp
│   │   ├── CLFProtocolAdapter.hpp  # API 协议适配（DeepSeek / OpenAI 格式互转）
│   │   ├── CLFProtocolAdapter.cpp
│   │   ├── CLFRepl.hpp         # REPL 交互循环（5 区 Terminal UI）
│   │   ├── CLFRepl.cpp
│   │   ├── CLFSecurityPolicy.hpp   # 安全策略（命令白名单、路径沙箱）
│   │   ├── CLFSecurityPolicy.cpp
│   │   ├── CLFSessionManager.hpp   # 会话管理器（历史持久化、清理策略）
│   │   ├── CLFSessionManager.cpp
│   │   ├── CLFSkillLoader.hpp  # 知识库加载器（按需加载 data/skills/）
│   │   ├── CLFSkillLoader.cpp
│   │   ├── CLFStreamAccumulator.hpp  # SSE 流式响应累加器（增量解析 JSON）
│   │   ├── CLFTerminal.hpp     # 终端 UI 渲染器（DECSTBM 滚动区 + ANSI 控制）
│   │   └── CLFTerminal.cpp
│   │
│   ├── CLFNetwork/             # 网络通信层
│   │   ├── CLFHttpClient.hpp   # HTTP 通信客户端（同步/流式请求，HTTPS 支持）
│   │   └── CLFHttpClient.cpp
│   │
│   ├── CLFTools/               # 工具实现层
│   │   ├── CLFBuiltinTools.hpp # 内置工具注册入口（注册 FileOps + CommandExec）
│   │   ├── CLFBuiltinTools.cpp
│   │   ├── CLFFileOps.hpp      # 文件操作工具（读/写/编辑/搜索）
│   │   ├── CLFFileOps.cpp
│   │   ├── CLFCommandExec.hpp  # 命令执行工具（沙箱子进程）
│   │   └── CLFCommandExec.cpp
│   │
│   └── test/                   # 单元测试（QA 前缀，Boost.UT 框架）
│       ├── qa_CLFAgentLoop.cpp
│       ├── qa_CLFContext.cpp
│       ├── qa_CLFProtocolAdapter.cpp
│       ├── qa_CLFSecurityPolicy.cpp
│       └── qa_CLFSessionManager.cpp
│
├── bin/                        # 可执行文件输出目录（CMake 自动填充）
│   ├── Debug/                  # Debug 构建产物
│   ├── Release/                # Release 构建产物
│   └── README.md               # 构建产物目录，内容已被 .gitignore 忽略
│
├── lib/                        # 静态/动态库输出目录（CMake 自动填充）
│   ├── Debug/                  # Debug 构建产物
│   ├── Release/                # Release 构建产物
│   └── README.md               # 构建产物目录，内容已被 .gitignore 忽略
│
└── .claude/                    # Claude Code 协作配置
    ├── progress.md             # 任务进度跟踪（已完成 / 进行中 / 待做）
    ├── settings.json           # 项目级权限与钩子配置
    ├── settings.local.json     # 本地设置覆盖（不提交 Git）
    ├── plans/                  # 项目规划设计文档
    │   ├── README.md           # 规划文档索引
    │   ├── 分析/               # 需求分析文档
    │   ├── 测试/               # 测试计划
    │   └── 设计/               # 设计方案 + 归档
    └── agents/                 # 自定义 Agent 定义
```

---

## 2. 命名规范

> 完整规范见 `~/.claude/CLAUDE.md`（全局）与项目 `CLAUDE.md`。此处仅列出关键约束。

| 类别 | 规范 | 示例 |
|---|---|---|
| 文件名 | `CLF` 前缀 + 大驼峰 | `CLFAgentLoop.hpp` |
| 类名 / 结构体名 | `CLF` 前缀 + 大驼峰 | `CLFHttpClient` |
| 成员变量 | `m_` 前缀 + 小驼峰 | `m_retryCount` |
| 函数名 / 局部变量 | 小驼峰 | `parseResponse()`, `inputPath` |
| 宏 | 全大写 + 下划线 | `CLF_MAX_TOKENS` |
| 命名空间 | 全大写 | `CLF`, `CLF::CLFCore`, `CLF::CLFTools` |

---

## 3. 设计原则

开发中遵循以下原则（按优先级排列）：

### 必须遵守（SOLID）
- **S** 单一职责：一个类只负责一件事
- **O** 开闭原则：对扩展开放，对修改关闭
- **L** 里氏替换：子类可替换父类而不破坏程序
- **I** 接口隔离：不强迫用户依赖不需要的接口
- **D** 依赖倒置：依赖抽象而非具体实现

### 建议遵守（组件原则）
- **CCP** 共同闭包：同样原因变化的类放在同一组件
- **CRP** 共同复用：不强迫用户依赖不需要的组件功能
- **ADP** 无环依赖：组件依赖图中不允许循环
- **SDP** 稳定依赖：依赖指向更稳定的方向
- **SAP** 稳定抽象：稳定组件应抽象，易变组件应具体

---

## 4. 重要约定

| 目录 | 用途 | 是否提交 Git | 备注 |
|:---|:---|:---|:---|
| `bin/` | 存放编译生成的可执行文件 | ❌ 忽略（仅保留 README） | 由 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 控制 |
| `lib/` | 存放编译生成的库文件 `.a` / `.so` | ❌ 忽略（仅保留 README） | 由 `CMAKE_LIBRARY_OUTPUT_DIRECTORY` 控制 |
| `build/` | CMake 构建输出 | ❌ 忽略 | `cmake-build-*` 目录 |
| `.idea/` `.vscode/` | IDE 配置目录 | ❌ 忽略 | 各开发者自行配置 |
| `.claude/` | Claude Code 协作配置 | ✅ 提交（`settings.json` / `settings.local.json` 除外） | 进度文件、规划设计、Agent 定义 |
| `3rdparty/` | 第三方库源码 | ✅ 提交（仅放头文件） | 不在此目录写 `CMakeLists.txt`，直接在顶层包含 |
| `config/` | 运行时配置 | ✅ 提交（`agent_settings.local.json` 除外） | `agent_settings.json` 中禁止明文写入 API Key（留空位，由用户手动填写或使用环境变量），本地配置写入 `agent_settings.local.json` |
| `data/skills/` | Agent 工作流知识 | ✅ 提交 | 所有 `.md` 文件为 UTF-8 编码 |
| `doc/` | 设计文档 | ✅ 提交（`log/` `debug/` `contextHistory/` 除外） | 运行时产出的日志、会话历史不入库 |
| `todo.md` | 本地个人待办 | ❌ 忽略 | 用户自用，不入库 |

---

## 5. CMake 输出路径配置

在顶层 `CMakeLists.txt` 中添加以下代码：

```cmake
# 使用 $<CONFIG> 确保 Debug/Release 输出到不同子目录，跨平台生成器行为一致
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/$<CONFIG>)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/$<CONFIG>)
```

---

## 6. 构建说明

| 配置项 | 值 |
|---|---|
| Ninja 可执行文件 | `D:\Program Files\JetBrains\CLion 2026.1.1\bin\ninja` |
| 并行编译上限 | `-j6`（避免模板实例化 OOM） |
| 编译器优先级 | GCC 15 → Clang 20 → Emscripten → GCC 14 |
| 日常构建类型 | `Debug` |
| 性能测试构建类型 | `Release` |

```bash
# 典型构建流程
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja
cmake --build build -j6

# 运行测试
ctest --test-dir build --output-on-failure -j6
```

---

## 7. .gitignore

```gitignore
# 构建产物（保留 README）
bin/*
!bin/README.md
lib/*
!lib/README.md
build/

# IDE 配置
.vscode/
.idea/
*.swp
*.swo

# Claude Code 协作配置（仅忽略本地设置文件，其余提交）
.claude/settings.json
.claude/settings.local.json

# 本地敏感配置
config/agent_settings.local.json

# 运行日志
doc/log/
!doc/log/README.md
*.log

# 崩溃调试
doc/debug/
!doc/debug/README.md

# 会话历史（运行时自动生成）
doc/contextHistory/*
!doc/contextHistory/README.md

# 本地个人待办（用户自用，不入库）
todo.md
```

---

## 8. 协作约定

### 进度跟踪
- 每次会话开始前，读取 `.claude/progress.md` 了解进度
- 会话结束前或上下文不足时，更新进度
- 进度文件包含三个部分：**已完成** / **进行中** / **待做**

### 临时文件
- 工作过程中的临时文件在功能完成时清理
- 可随时在 `progress.md` 中记录防止记忆丢失
