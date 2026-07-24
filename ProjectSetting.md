# CLFCode 工程结构设计

---

## 1. 目录结构

```
CLFCode/
├── CMakeLists.txt              # 顶层 CMake 构建入口
├── README.md                   # 项目整体说明（编译方法、依赖、快速开始）
├── CLAUDE.md                   # 项目级 AI 协作约定（继承全局规范）
├── ProjectSetting.md           # 本文件 — 工程结构设计文档
├── .gitignore                  # Git 忽略规则（必须忽略 bin/ lib/ build/）
│
├── 3rdparty/                   # 第三方依赖库（仅限 Header-Only 库）
│   ├── httplib/
│   │   └── httplib.h           # cpp-httplib 头文件
│   ├── nlohmann/
│   │   └── json.hpp            # nlohmann/json 头文件
│   └── README.md               # 记录各库的版本号与下载来源
│
├── config/                     # 运行时配置文件（机器读取）
│   ├── agent_settings.json     # Agent 核心配置（API Key、模型名、上下文窗口大小）
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
├── doc/                        # 项目设计文档（给人看）
│   ├── architecture_design.md  # 系统架构设计文档
│   ├── api_interface.md        # 模块接口定义
│   └── README.md               # 文档导航
│
├── src/                        # 项目源代码
│   ├── CMakeLists.txt          # 源码子目录构建文件
│   ├── main.cpp                # 程序入口（REPL 循环）
│   ├── CLFCore/                # Agent 核心逻辑
│   │   ├── CLFAgentLoop.hpp    # Agent 主循环调度器
│   │   ├── CLFAgentLoop.cpp
│   │   ├── CLFContext.hpp      # 对话上下文管理器
│   │   └── CLFContext.cpp
│   ├── CLFNetwork/             # 网络通信层
│   │   ├── CLFHttpClient.hpp   # HTTP 通信客户端
│   │   └── CLFHttpClient.cpp
│   ├── CLFTools/               # 工具实现层
│   │   ├── CLFFileOps.hpp      # 文件操作工具
│   │   ├── CLFFileOps.cpp
│   │   ├── CLFCommandExec.hpp  # 命令执行工具
│   │   └── CLFCommandExec.cpp
│   └── README.md               # 源码模块说明
│
├── bin/                        # 可执行文件输出目录（CMake 自动填充）
│   └── README.md               # 构建产物目录，内容已被 .gitignore 忽略
│
├── lib/                        # 静态/动态库输出目录（CMake 自动填充）
│   └── README.md               # 构建产物目录，内容已被 .gitignore 忽略
│
└── .claude/                    # Claude Code 协作配置
    ├── progress.md             # 任务进度跟踪（已完成 / 进行中 / 待做）
    ├── settings.json           # 项目级权限与钩子配置（不提交 Git）
    ├── plans/                  # 项目规划设计文档
    │   └── README.md           # 规划文档索引
    └── agents/                 # 自定义 Agent 定义
```

---

## 2. 命名规范

> 完整规范见 `~/.claude/CLAUDE.md`（全局）与项目 `CLAUDE.md`。此处仅列出关键约束。

| 类别 | 规范 | 示例 |
|------|------|------|
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
|:-----|:-----|:-------------|:-----|
| `bin/` | 存放编译生成的可执行文件 | ❌ 忽略（仅保留 README） | 由 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 控制 |
| `lib/` | 存放编译生成的库文件 `.a` / `.so` | ❌ 忽略（仅保留 README） | 由 `CMAKE_LIBRARY_OUTPUT_DIRECTORY` 控制 |
| `.claude/` | Claude Code 协作配置 | ✅ 提交（settings.json 除外） | 进度文件、规划设计、Agent 定义 |
| `3rdparty/` | 第三方库源码 | ✅ 提交（但只放头文件） | 不在此目录写 `CMakeLists.txt`，直接在顶层包含 |
| `config/` | 运行时配置 | ✅ 提交（不含敏感信息） | `agent_settings.json` 中禁止明文写入 API Key（留空位，由用户手动填写或使用环境变量） |
| `data/skills/` | Agent 工作流知识 | ✅ 提交 | 所有 `.md` 文件为 UTF-8 编码 |
| `doc/` | 设计文档 | ✅ 提交 | 便于团队协作与归档 |

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
|--------|-----|
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
# 构建产物
bin/
lib/
build/

# IDE 配置
.vscode/
.idea/
*.swp
*.swo

# Claude Code 协作目录
.claude/

# 本地敏感配置
config/agent_settings.local.json
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
