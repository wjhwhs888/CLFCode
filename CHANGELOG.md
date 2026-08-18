# CHANGELOG

> 面向用户，只描述功能变更（新增/删除/修复），不写源代码细节。

---

> **v0.1.0 发布致谢**

衷心感谢：

- **DeepSeek** — v4 Pro 与 Flash API 的支持，初版针对 DeepSeek 进行适配、开发与测试，无数 token 的燃烧换来了这个项目
- **Claude Code CLI** — 在开发全程提供了便利的开发与交互环境，设计评审、代码审查、方案讨论均在其上完成
- **cc-switch** — 提供了开发过程中便捷的模型切换与配置，让多模型协作开发成为可能

---

## v0.3.3 (2026-08-18)

### 修复

- **交互输入必崩溃（静默退出）**：任意触发 LLM 流式请求的回合可能无声退出（无报错、无崩溃窗口），表现为"输入命令就消失"——根因是终端 UI 框架的事件缓冲存在多线程竞争（后台刷新线程与界面渲染线程并发访问同一事件队列），在流式/工具执行期间触发内存破坏。修复为事件队列加锁 + 后台线程异常兜底 + 全局异常留痕
- **升级脚本数据保护**：`install.ps1` / `upgrade.ps1` 升级时不再丢失会话历史（`doc/contextHistory`）、日志（`doc/log`）与崩溃转储（`doc/debug`）——与配置一起备份并在升级后恢复

---

## v0.3.2 (2026-08-18)

### 修复

- **发布版必崩溃**（台式机+笔记本稳定复现，Debug 正常）：`/exit` 或 Esc Esc 退出时归档中文标题会话触发 `No mapping for the Unicode character exists in the target multi-byte code page` 异常——MSVC 窄字符文件系统 API 按 ANSI 代码页（CP936）解释 UTF-8 路径字节。全链路修复：`CLFSessionManager`/`CLFConfigLoader`/`CLFFileOps` 文件操作统一 `u8path`/`u8string`，会话标题截断改 UTF-8 边界安全
- **read_file 内容编码**：增加 UTF-8 内容探测，合法 UTF-8 直接采用（原先按 GBK 解读既有乱码隐患，非法序列还会抛转换异常）；目录列出的文件名宽路径直读，消除 ACP 往返
- **release.ps1**：构建目录修正为 `cmake-build-release`（旧脚本指向不存在的目录，构建失败后静默打包陈旧 exe——本次崩溃发布事故的间接成因）；新增 vcvars64 环境导入、构建失败硬退出、exe 新旧自检；DLL 只携带 OpenSSL 对，不再逐版携带历史 MinGW 运行库
- 取证设施清理：移除崩溃定位期间的临时复现钩子与异常陷阱（保留增强异常捕获——打印 `e.what()`，及 `CLF_DEBUG_EVENTS` 事件日志取证设施）

---

## v0.3.1 (2026-08-17)

### 新增

- **粘贴多行合并**：Ctrl+V / Shift+右键 粘贴多行原样插入输入框（事件突发合并器，40ms 静默窗区分粘贴换行与手打回车），不再逐行触发提交
- **选区复制**：鼠标左键拖选，松手自动复制（copy-on-select）——复制逻辑文本而非渲染网格，换行与原文一致（终端原生 Shift+拖选+右键复制受限于渲染网格，无法修复）
- 新增 3 个测试套件：粘贴合并器（P1-P10）、选区模型（S1-S7）、输入渲染回归（Ref 光标同步 / \r 渲染 / 多行渲染）

### 修复

- **Ctrl+C 失效**（深层根因）：FTXUI 未清除 `ENABLE_PROCESSED_INPUT`，Ctrl+C 被系统转信号、其 SIGINT 处理器直接退出主循环，事件从未到达应用层——应用启动时自行清除该位后恢复
- **粘贴首两行合并**（深层根因）：`ftxui::Ref<int>` 拥有型构造致输入光标为 Input 内部副本，恢复逻辑对光标的赋值不同步，后续字符插在换行符之前——改引用型 Ref（指针构造）
- **拖选末字符丢失**：游标列改含入语义（鼠标落在字符格内即包含该字符）
- **输入框点击误判为选区**：内容区以下坐标不再 clamp 到末行
- **Ctrl+C 空闲误触退出**：空闲时忽略（退出统一 Esc Esc / /exit），运行中中断保留
- qa_CLFSecurityPolicy 指针比较缺陷（`const char* ==` 依赖链接器字面量合并，MSVC Debug 下失败）

### 交互调整

- 移除 Ctrl+S 键盘选区与 Ctrl+C/Enter 复制（验收收敛：纯鼠标拖选 + 松手自动复制；键盘选区 API 已随 git 历史移除）

---

## v0.3.0 (2026-08-14)

### 新增

- **UI 信息降噪**（借鉴 dsh 展示设计）：错误首行摘要、head/tail 截断（搜索 240+240 环形缓冲、diff 16+16）、工具执行中单行显示 + 旋转动画、读工具失败保留永久 ✗ 行
- **四态状态点**：running 蓝色动画 / done 绿 / warn 琥珀 / error 红；计时 ≥15s 才显示；analyze 模式改紫色（蓝让给 running）
- **中断消息单点收敛**：9 处内联 → `emitInterrupted()` 统一（文案 + 思考清空 + Warn 状态点）
- **恢复回显折叠**：`/resume` 历史折叠为一行（`Ctrl+R` 展开/收起），不再全量灌屏
- **消息级时间戳**：用户消息行尾 `HH:mm`（跨日自动带日期）
- **审批卡强化**：headline 琥珀加粗 + 参数 dim 分层 + 确认结束防残影
- **token 统计**：API usage 打通（流式 `include_usage` + 独立投喂），工具摘要显示 `X.Xk tok`，`/context` 显示会话累计
- **统计摘要增强**：summary 显示总工具数 + read/search/edited 分桶计数

### 修复

- **工具执行期界面冻结**（潜伏缺陷）：状态与进度更新不触发刷新，执行期显示静止——turnTimer 1Hz 驱动
- **search 工具双计数**：同时计入 read 与 search 桶
- **get_current_time 仅 Windows**：补充 POSIX 分支（`localtime_r`）
- useProgressive 同作用域遮蔽警告、Ctrl+O 注释错误等清理

### 测试

- 新增 3 个测试套件（UI 工具 / 搜索截断 / 工具执行器），累计 9 套件 20+ 新用例
- 集成测试覆盖：三时点中断、usage 穿透流式过滤、折叠回显、状态点不被 TurnGuard 覆盖

---

## v0.2.0 (2026-08-12)

### 新增

- **CLI 参数解析**：`--help` / `--version` / `--prompt` / `--prompt-file` / `--allow-write` / `--config` / `--project-root`
- **非交互模式**：`--prompt` 单次执行后退出，安全策略 Analyze（block 写），`--allow-write` 提升 Auto
- **`search_content` 工具**：纯文本匹配，跳过 `.git`/`node_modules` 等目录，1MB 文件上限，扩展名过滤，500 行截断

### 修复

- **安装/升级脚本终端闪退**：`exit 0` → `return`
- **ProgressGuard 空指针**：非交互模式 `m_output=null` 导致崩溃
- **非交互模式流式响应丢失**：`--prompt` 强制关流式

### 文档

- README 补充卸载命令 + 安装升级卸载格式化
- 整体功能审查方案（26 问题，3 批实施计划）
- P0 第一批测试方案（13 条用例）

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
