# CLFCode — CLI Agent Framework for Code

本地运行的 AI Coding Agent，Harness 架构，FTXUI 终端 UI。

## 架构

```
src/
├── CLFTypes/     — 基础类型 + ICLFOutput 接口
├── CLFNetwork/   — HTTP 传输 + 思考指示器
├── CLFCore/      — Agent 核心逻辑 (编排/上下文/安全/会话)
├── CLFTools/     — 工具实现 (文件/命令)
├── CLFUI/        — 终端 UI (FTXUI 组件树)
├── main.cpp      — 组合根 (依赖注入)
└── test/         — 单元测试
```

依赖方向：`CLFTypes → CLFNetwork → CLFCore → {CLFTools, CLFUI} → main`

## 依赖

- **编译器**: MSVC 2022+ / GCC 15+ / Clang 20+（C++17）
- **构建工具**: CMake 3.20+ / Ninja
- **第三方库**（在 `3rdparty/` 或 `FetchContent`）:
  - [cpp-httplib](https://github.com/yhirose/cpp-httplib) — HTTP/HTTPS 客户端
  - [nlohmann/json](https://github.com/nlohmann/json) — JSON 解析
  - [FTXUI](https://github.com/ArthurSonzogni/FTXUI) v6.1.9 — 终端 UI 框架
  - [Boost.UT](https://github.com/boost-ext/ut) — 单元测试

## 快速开始

```bash
# 配置
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja

# 编译 (限制并行防 OOM)
cmake --build build -j6

# 运行
./bin/Debug/CLFCode

# 测试
ctest --test-dir build --output-on-failure -j6
```

## 设计文档

详见 `.claude/plans/设计/`。

## 进度

详见 `.claude/progress.md`。
