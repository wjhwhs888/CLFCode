# CLFCode 任务进度

## 已完成

### 2026-08-10 diff 着色 ✅
- emitStyledLine 样式通道 + FTXUI color/dim 渲染

### 2026-08-10 渐进式工具显示 + 双计时器 ✅
- 设计文档：[归档/设计-计时器管理与显示-已完成](../../设计/归档/设计-计时器管理与显示-已完成.md)

### 2026-08-10 文件修改 diff 渲染 ✅
- 设计文档：[归档/设计-文件修改diff渲染-已完成](../../设计/归档/设计-文件修改diff渲染-已完成.md)

### 2026-08-07 显示区信息降噪方案 ✅
### 2026-08-07 快捷键方案 — 全部 3 批完成 ✅
### 2026-08-07 推理过程显示（中间发现） ✅
### 2026-08-06 代码清理 + OCP 重构 + 组件提取 + CJK 缓解 ✅
### 2026-08-05 UI 全面重构 ✅
### 2026-08-04 Harness 架构重构 ✅
### 2026-08-03 FTXUI 终端 UI 重构 ✅
### 全量优化 P0-P3 ✅

### 2026-08-11 首次运行崩溃修复（长期观察） 🔍
- 设计文档：[设计-首次运行崩溃修复](../../设计/设计-首次运行崩溃修复.md)
- 已实施三层防御（L1 线程兜底 / L2 子进程 stdin 隔离 / L3 诊断日志）
- 当前无法复现，防御代码保留作为长期防护基础设施
- **复现时诊断入口**：查看 `doc/log/clf_agent.log`，搜索 `[AsyncSubmit]` 和 `[ToolExec]` 关键词

## 长期观察
- 首次运行崩溃修复（2026-08-11）：等待自然复现后根据日志定位根因

### 2026-08-11 Resume 会话恢复完善 & 上下文智能压缩 ✅
- 设计文档：[设计-Resume会话恢复完善&上下文智能压缩](../../.claude/plans/async-stargazing-boole.md)
- 保存模型重建：latest.json 原子写入 + /exit 归档
- 会话摘要：CLFSessionSummarizer（独立模块），API 生成 + 降级规则提取
- Resume 修复：回显历史 + skill 重建 + 摘要注入（system 锚点）
- 灾难保护：原子写入、损坏备份、空会话保护、旧文件迁移
- 日志布点：debug/info/warn 三级全覆盖
- /exit Unicode 崩溃修复

### 2026-08-12 System Prompt 优化 ✅
- 设计文档：[设计-SystemPrompt优化](../../.claude/plans/设计/设计-SystemPrompt优化.md)
- CLFSystemPromptBuilder：模板加载（降级默认）/ L1 宪法 mtime 缓存 / Git TTL 30s 惰性刷新 / 项目规则加载 / token 预算
- CLFContext::setSystemPrompt()（去重）+ removeSystemMessages()
- CLFAgentLoop::injectSystemPrompt() → Builder，injectSkillToContext() → 重建模式
- config/system_prompt_template.md 可编辑模板

### 2026-08-12 /init 项目初始化命令 ✅
- `/init` → 在工作目录创建 PROJECTRULES.md 模板（已有则不覆盖）
- 模板含 6 个区块：项目概述 / 技术栈 / 编码规范 / 架构约定 / 构建与测试 / 注意事项

### 2026-08-12 渐进式工具显示细化 ✅
- 执行中：只显示当前工具（一条隐藏一条），空行隔离
- 完成后：折叠为 summary（thought for Xs, read N files），Ctrl+T 展开查看
- 文件更新不隐藏，直接展示 diff
- 状态栏：执行中显示 "Working for Xs…"，完成后追加 "Cooked for Xs"

### 2026-08-11 UI 体验优化 ✅
- 输入框灰色背景移除：自定义 transform 跳过 FTXUI 默认样式
- 状态栏着色：模型红/目录绿/模式分色（auto绿 analyze蓝 edit橙 manual灰）
- 分割线细化：separatorLight + 浅蓝色
- Markdown 表格列对齐：emitContent 层缓冲表格块，CJK 感知列宽对齐

## 已知问题
- Ctrl+C 确认栏退出（低优先，暂缓）
### 2026-08-12 v0.1.5 发布 ✅
- 归档方案：[归档-SystemPrompt优化](../../.claude/plans/设计/归档/归档-SystemPrompt优化.md)

## 已知问题
- Ctrl+C 确认栏退出（低优先，暂缓）
- emitRaw 钩子（设计预留）
