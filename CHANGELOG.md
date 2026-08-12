# CHANGELOG

> **v0.1.0 发布致谢**

衷心感谢：

- **DeepSeek** — v4 Pro 与 Flash API 的支持，初版针对 DeepSeek 进行适配、开发与测试，无数 token 的燃烧换来了这个项目
- **Claude Code CLI** — 在开发全程提供了便利的开发与交互环境，设计评审、代码审查、方案讨论均在其上完成
- **cc-switch** — 提供了开发过程中便捷的模型切换与配置，让多模型协作开发成为可能

---

## v0.1.6 (2026-08-12)

### 新增

- **`/version` 命令**：显示当前版本号
- **安装脚本版本检测**：`install.ps1` 对比本地/远程版本，已是最新则跳过

### 变更

- `/help` 命令列表按字母序排列
- 发布包根目录新增 `VERSION` 文件（供升级脚本版本比较）
- `使用说明.txt` 重写：安装/升级/卸载命令前置，新增 System Prompt 模板说明

---

## v0.1.5 (2026-08-12)

### 新增

- **`/init` 命令**：在当前工作目录创建 PROJECTRULES.md 项目规则模板（已有不覆盖），模板含篇幅建议（128 行以内）
- **System Prompt 优化**：模板化/动态上下文/项目规则/合并/Token 预算
  - `CLFSystemPromptBuilder`：可编辑模板（`config/system_prompt_template.md`）+ 降级默认
  - **动态上下文**：Git 状态快照（分支 + 最近提交 + 工作区），30s TTL 惰性刷新
  - **项目规则**：自动检测 `PROJECTRULES.md` → `CLAUDE.md`，注入 system prompt
  - **System 消息合并**：所有 system 内容合并为单条消息
  - **Token 预算保护**：system 区 30% 窗口上限，超出按完整 skill 丢弃
  - **性能优化**：L1 宪法 mtime 缓存 + `setSystemPrompt()` 内容去重
- **CLFContext 新接口**：`setSystemPrompt()`（含去重）/ `removeSystemMessages()`

### 变更

- `CLFAgentLoop::injectSystemPrompt()` 改为调用 `CLFSystemPromptBuilder`，`injectSkillToContext()` 改为记录后重建模式

---

## v0.1.4 (2026-08-11)

### 优化

- **底部状态栏着色**：模型名红色加粗、目录绿色、安全模式按级别分色（auto绿/analyze蓝/edit橙/manual灰）
- **输入框分割线优化**：`separatorLight()` 细线 + 浅蓝色，替代厚重默认分割线
- **Markdown 表格列对齐**：`emitContent` 层检测连续 `|` 开头的表格块，缓冲后按 CJK 列宽对齐一次性输出
- **模式切换提示**：状态栏模式后显示 `Shift+Tab 切换`

---

## v0.1.3 (2026-08-11)

### 修复

- **输入框灰色背景移除**：FTXUI `InputOption::Default().transform` 在聚焦时添加背景色，自定义 `transform` 跳过默认样式

---

## v0.1.2 (2026-08-11)

### 修复

- **独立安装启动失败**：`findProjectRoot()` 找不到 CMakeLists.txt 时回退到 `config/agent_settings.json` 查找，解决 `irm | iex` 安装后 clfcode 无法启动
- **安装脚本解压路径**：zip 内子目录 `CLFCode-vX.Y.Z/` 正确对正到 `%USERPROFILE%\CLFCode\`
- **发布包缺失 MinGW 运行时 DLL**：补充 `libstdc++-6.dll` / `libgcc_s_seh-1.dll` / `libwinpthread-1.dll`

---

## v0.1.1 (2026-08-11)

### 重构

- **会话保存模型重建**：`_incomplete.json` 模型废弃，改为 `latest.json`（原子写入 .tmp→rename）。每轮回合自动保存，关窗/崩溃不丢数据；`/exit` 归档为时间戳 `.json`；`/history` 直接可见 `[当前]` 标记
- **会话摘要压缩（独立模块 `CLFSessionSummarizer`）**：`/exit` 时调用 API 生成结构化摘要（主题/决策/文件/待办），降级为规则提取；恢复时注入为 system 消息，利用 system 永不截断特性保证长会话关键信息不丢失
- **Resume 恢复完善**：恢复时终端回显历史对话；skill 知识库从文件系统重新加载（保证最新版本）；`m_loadedSkills` 状态一致

### 修复

- `/resume` 恢复后 AI 丢失 skill 知识（`system` 消息一刀切跳过）
- `/exit` 时 Unicode 编码异常导致崩溃无法保存（`CLFRepl::submit` 渲染异常隔离）
- Windows Release 构建：ws2_32 链接 + NOMINMAX 重定义防护

### 工程

- 日志系统 debug 级扩展：保存/恢复/压缩链路全覆盖，崩溃可追溯（每条日志即时 flush）
- 会话文件损坏保护：解析失败自动备份 `.bak`，不崩溃
- 旧版 `_incomplete.json` 启动时自动迁移
- **部署脚本**：install.ps1 / upgrade.ps1 / uninstall.ps1，支持 `irm | iex` 一键安装

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
