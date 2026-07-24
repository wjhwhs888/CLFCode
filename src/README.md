# 源码模块说明

| 目录 | 库名 | 说明 |
|------|------|------|
| `CLFCore/` | `clf_core` | Agent 主循环 + 上下文管理 |
| `CLFNetwork/` | `clf_network` | HTTP 通信客户端（封装 cpp-httplib） |
| `CLFTools/` | `clf_tools` | 文件操作 + 命令执行 |

## 依赖关系

```
CLFCode (main.cpp)
  └── clf_core ──→ clf_network
        │              └── httplib.h
        └── nlohmann/json.hpp
```

## 命名规范

- 文件名：`CLF` 前缀 + 大驼峰（如 `CLFAgentLoop.hpp`）
- 实现文件与头文件同名（`.cpp` / `.hpp`）
- 测试文件：`qa_` 前缀（如 `qa_CLFAgentLoop.cpp`）
