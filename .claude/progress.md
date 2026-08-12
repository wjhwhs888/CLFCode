# CLFCode 任务进度

## 已完成

### 2026-08-12 System Prompt 优化 ✅
- 设计文档：[归档-SystemPrompt优化](../../.claude/plans/设计/归档/归档-SystemPrompt优化.md)
- CLFSystemPromptBuilder：模板加载（降级默认）/ L1 宪法 mtime 缓存 / Git TTL 30s 惰性刷新 / 项目规则加载 / token 预算
- CLFContext::setSystemPrompt()（去重）+ removeSystemMessages()
- CLFAgentLoop::injectSystemPrompt() → Builder，injectSkillToContext() → 重建模式
- config/system_prompt_template.md 可编辑模板

### 2026-08-12 /init 项目初始化命令 ✅
- `/init` → 在工作目录创建 PROJECTRULES.md 模板（已有则不覆盖）
- 模板含 6 个区块，128 行限制提示

### 2026-08-12 /version 命令 + /help 字母序 ✅
- `/version`：读取 VERSION 文件显示版本号
- `/help`：命令列表按首字母排序
- 归入 v0.1.6 (unreleased)，待后续功能一起发布

### 2026-08-12 v0.1.5 发布 ✅
- 归档方案：[归档-SystemPrompt优化](../../.claude/plans/设计/归档/归档-SystemPrompt优化.md)
- tag: v0.1.5，release: CLFCode-v0.1.5-win64.zip (12M)

### 2026-08-12 渐进式工具显示细化 ✅
- 执行中：只显示当前工具，空行隔离；完成后折叠 summary，Ctrl+T 展开
- 状态栏：Working → Cooked 切换

### 2026-08-11 Resume 会话恢复完善 & 上下文智能压缩 ✅
- latest.json 原子写入 + /exit 归档 + CLFSessionSummarizer + 灾难保护

### 2026-08-11 UI 体验优化 ✅
- 输入框灰色背景移除 / 状态栏着色 / 分割线细化 / Markdown 表格列对齐

### 2026-08-11 首次运行崩溃修复（长期观察） 🔍
- 三层防御（L1 线程兜底 / L2 子进程 stdin 隔离 / L3 诊断日志），当前无法复现
- 诊断入口：`doc/log/clf_agent.log`，搜索 `[AsyncSubmit]` 和 `[ToolExec]`

### 更早版本
- 01-10: diff 着色 / 渐进式工具显示+双计时器 / 文件修改 diff 渲染
- 01-07: 显示区信息降噪 / 快捷键系统 / 推理过程显示
- 01-06: 代码清理+OCP重构+组件提取+CJK
- 01-05: UI 全面重构 (FTXUI)
- 01-04: Harness 架构重构
- 01-03: FTXUI 终端 UI 重构 / 全量优化 P0-P3

## 进行中 / 待做

- v0.1.6 功能攒量，下次一起打 tag 发布

## 长期观察

- 首次运行崩溃修复（2026-08-11）：等待自然复现后根据日志定位根因

## 已知问题

- Ctrl+C 确认栏退出（低优先，暂缓）
- emitRaw 钩子（设计预留）
