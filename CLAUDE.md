# CLFCode 项目约定

## 继承规则

本项目**直接继承**全局规范 `~/.claude/CLAUDE.md` 中的所有约定，包括但不限于：

- **设计原则**：SOLID + CCP/CRP/ADP/SDP/SAP
- **编程规范**：避免裸指针、优先复用已有功能
- **协作规则**：进度文件机制（`.claude/progress.md`）
- **命名规范**：CLF 前缀、m_ 成员、小驼峰函数等（见下方重申）

本文件仅记录 **CLFCode 项目特有的补充**，不重复全局规范。

---

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
