# CLFCode — CLI Agent Framework for Code

本地运行的 AI Coding Agent，Harness 架构，FTXUI 终端 UI。

## 安装（Windows 10+）

```powershell
irm https://gitee.com/sherlock0923/CLFCode/raw/master/install.ps1 | iex
```

升级：

```powershell
irm https://gitee.com/sherlock0923/CLFCode/raw/master/upgrade.ps1 | iex
```

卸载：

```powershell
& "$env:USERPROFILE\CLFCode\uninstall.ps1"
```

首次运行前编辑 `%USERPROFILE%\CLFCode\config\agent_settings.json` 填入 API Key。

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
  - [FTXUI](https://github.com/ArthurSonzogni/FTXUI) v7.0.0 — 终端 UI 框架
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

## 快捷键

| 功能 | 快捷键 |
|------|--------|
| 提交输入 | `Enter` / `Ctrl+D` |
| 换行 | `Ctrl+N` |
| 中断 Agent | `Esc`（单击，中断后回显上次输入） |
| 退出程序 | `Esc Esc`（空闲时双击） |
| 切换安全模式 | `Shift+Tab` |
| 历史导航 | `↑` / `↓`（首行↑翻历史，尾行↓回草稿） |
| 滚动内容 | 鼠标滚轮 / `PgUp` / `PgDn` / `Home` / `End` |
| 粘贴 | `Ctrl+V`（终端原生） |
| 确认弹窗 | `← →` 选择 / `Enter` 确认或返回 / `Esc` 返回 |

## 设计文档

详见 `.claude/plans/设计/`。

## 进度

详见 `.claude/progress.md`。
