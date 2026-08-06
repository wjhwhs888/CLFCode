# CLFCode 项目约定

## 继承规则

本项目**直接继承**全局规范 `~/.claude/CLAUDE.md` 中的所有约定，包括但不限于：

- **设计原则**：SOLID + CCP/CRP/ADP/SDP/SAP
- **编程规范**：避免裸指针、优先复用已有功能
- **协作规则**：进度文件机制（`.claude/progress.md`）
- **命名规范**：CLF 前缀、m_ 成员、小驼峰函数等（见下方重申）

本文件仅记录 **CLFCode 项目特有的补充**，不重复全局规范。

---

## 工作宪法 (Constitution)

以下规则是硬约束，违反即为工作失误：

1. **深度定位，拒绝打补丁**：遇到 bug 必须追溯到根因（root cause），理解问题产生的完整因果链后才能动手修。不允许"碰巧能工作"的盲目修改。
2. **修根不修表**：修复必须从根本上解决问题，而不是绕过、掩盖或仅处理表面现象。如果修复引入了新的复杂度或破坏了其他功能，说明没找到真正的根因。
3. **系统性排查**：涉及数据流、状态机、多线程的逻辑，必须全链路追溯每一步的输入→处理→输出，画出数据流再动手。
4. **测试驱动修复**：修复后必须构造能复现原始 bug 的最小用例，验证修复有效且不引入回归。
5. **能力自信**：你的能力足以解决复杂问题。不要急于给出结论——花时间深入思考，直到找到真正的原因。

## 项目概述

- **项目名称**：CLFCode（CLI Agent Framework for Code）
- **项目目标**：构建一个可本地运行的 AI Coding Agent，具备文件操作、命令执行、网络调用等工具能力
- **语言标准**：C++17
- **构建系统**：CMake

---

## 命名规范重申

> 以下与全局规范一致，单独列出以消除歧义。

| 类别 | 规范 | 示例 |
|------|------|------|
| 文件名 | `CLF` 前缀 + 大驼峰 | `CLFClassTest.hpp`, `CLFDataStruct.cpp` |
| 类名 / 结构体名 | `CLF` 前缀 + 大驼峰 | `CLFAgentLoop`, `CLFHttpClient` |
| 成员变量 | `m_` 前缀 + 小驼峰 | `m_loggerSwitch`, `m_apiEndpoint` |
| 函数名 | 小驼峰 | `stepSize()`, `sendRequest()` |
| 局部变量 / 参数 | 小驼峰 | `inputPath`, `maxRetries` |
| 宏 | 全大写 + 下划线 | `DEBUG_LOG`, `MAX_BUFFER_SIZE` |
| 命名空间 | 全大写 | `CLF`, `CLF::CLFCore`, `CLF::CLFTools` |

---

## 模块化原则

- **main.cpp 只做入口编排**：加载配置、组装依赖、启动 REPL。具体实现（handler、adapter、注册逻辑）必须归属到对应功能模块
- **工具 handler** 实现在 `CLFTools/` 中，通过 `registerBuiltinTools()` 统一注册
- **配置解析** 归属 `CLFConfigLoader`，不放 main.cpp
- 模块间依赖：`clf_tools` → `clf_core`；`clf_network` 独立；`main` 组装三者

## 构建说明

- **Ninja 路径**：`D:\Program Files\JetBrains\CLion 2026.1.1\bin\ninja`
- **并行限制**：`-j6`（模板密集型编译，避免 OOM）
- **编译器优先级**：GCC 15 > Clang 20 > Emscripten > GCC 14
- **构建类型**：`Debug`（日常调试）/ `Release`（性能测试）
- **典型命令**：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja
cmake --build build -j6
ctest --test-dir build --output-on-failure -j6
```

---

## 项目目录结构

详见 `ProjectSetting.md`。

## Plans 目录

遵循全局 `~/.claude/CLAUDE.md` 中的 Plans 目录规范：

```
.claude/plans/
├── 分析/      # 分析可行性、技术选型
├── 测试/      # 测试记录与结果
├── 设计/      # 严格设计方案
│   └── 归档/   # 已完成或废弃
└── README.md
```
