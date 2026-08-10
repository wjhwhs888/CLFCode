# CHANGELOG

> **v0.1.0 发布致谢**

衷心感谢：

- **DeepSeek** — v4 Pro 与 Flash API 的支持，初版针对 DeepSeek 进行适配、开发与测试，无数 token 的燃烧换来了这个项目
- **Claude Code CLI** — 在开发全程提供了便利的开发与交互环境，设计评审、代码审查、方案讨论均在其上完成
- **cc-switch** — 提供了开发过程中便捷的模型切换与配置，让多模型协作开发成为可能

---

## v0.1.0 (2026-08-10)

### 新增

- **FTXUI v7.0.0 全帧终端 UI**：双缓冲渲染、滚动视口、ANSI 支持
- **流式 SSE 推理过程显示**：reasoning_content 独立累积，Ctrl+T 折叠/展开，实时计时
- **四模式安全策略**：Auto（全放行）/ Analyze（阻断写）/ Edit（写需确认）/ Manual（写需确认）
- **文件修改 diff 预览**：write_file / edit_file 执行前展示 `+`/`-` 行级差异，LCS 算法 + 超限截断（3000行/500KB），FTXUI 着色（绿+/红-/灰上下文）
- **原子写入**：临时文件 + flush + MoveFileEx/rename，EXDEV 降级，TOCTOU 乐观锁校验
- **渐进式工具显示 + 双计时器**：读类工具执行中只显示当前条，完成后折叠为 summary；整体 turn 计时 + StatusLine 实时更新
- **7 个内置工具**：read_file / write_file / edit_file / list_directory / execute_command / get_current_time / echo
- **快捷键系统**：Shift+Tab 切换安全模式，Ctrl+T 折叠思考，Ctrl+N 换行，Ctrl+V 粘贴，ESC 中断
- **信息降噪**：工具输出精简（成功单行 ✓ / 失败截断 ✗）、思考过程折叠

### 重构

- Harness 架构：ICLFOutput 抽象 + 模块解耦（CLFTypes / CLFCore / CLFNetwork / CLFTools / CLFUI）
- OCP 命令注册表：CLFCommandDispatcher 查表路由替代 if-else 链
- 组件提取：CLFClipboard / CLFAsyncSubmit / CLFScrollView / CLFConfirmBar
- 死代码清除：CLFConsole / CLFScrollBuffer / CLFEvent / CLFErrorCodes / CLFStreamProcessor
- CLFTurnRunner 方案废弃，回归轻量线程 + RAII Guard

### 修复

- P0-1：CLFCommandExec detach() 悬垂引用
- P0-2：m_activeCli 非 RAII 泄漏
- P0-3：json::dump() 无异常保护
- execute_command 安全确认拦截失效（diff 重构回归）

### 工程

- 项目根目录 `.claude/plans/` 统一管理设计方案，22 个方案归档
- 3rdparty/ftxui 剔除 Git 子模块，源码纳入版本控制
