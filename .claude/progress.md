# CLFCode 任务进度

## 已完成

### 2026-08-07 快捷键方案 — 全部 3 批完成 ✅

**第 1 批（核心输入输出）**：
- ✅ Enter/Ctrl+D 提交，Ctrl+N 换行，滚轮/翻页/跳转，Tab 不切焦点
- ✅ Shift+Tab 模式切换（含确认栏期间）
- ✅ 确认栏两选项（确认/返回），"返回"/ESC/CtrlC = 拒绝+中断+回编辑
- ✅ ESC 中断后输入框回显上次提交
- ✅ `[1;1R` CPR 残留剥离

**第 2 批（双击退出）**：
- ✅ 空闲时 500ms 内双击 ESC → `/exit`
- ❌ Alt+Enter — 弃用（终端全屏冲突），换行用 Ctrl+N

**第 3 批（历史导航）**：
- ✅ ↑/↓ 边界切换历史导航 + `m_historyDraft` 草稿恢复
- ❌ Ctrl+V — FTXUI 自带粘贴
- ❌ Ctrl+Y — 弃用（终端原生区域复制更优）

**关键修复**：
- ESC 单次即中断：FTXUI v7 首次 ESC = `Event::Special({27,27})`
- 移除 `input->TakeFocus()`：修复光标频繁闪烁
- 确认栏线程安全 + ToolExecutor 中断注入 + Agent 三层中断检查

**设计文档**: [plans/设计-快捷键方案设计.md](../plans/设计-快捷键方案设计.md)

### 2026-08-06 代码清理 + OCP 重构 + 组件提取 + CJK 缓解 ✅
### 2026-08-05 UI 全面重构 ✅
### 2026-08-04 Harness 架构重构 ✅
### 2026-08-03 FTXUI 终端 UI 重构 ✅
### 全量优化 P0-P3 ✅

## 已知问题

### Ctrl+C 确认栏退出
- Ctrl+C 在确认栏激活时经步骤 4 分流到 ExitLoopClosure，影响小，暂缓

### CJK 光标半字移动
- 根因在 FTXUI 内部

### emitRaw 钩子
- 接口保留，无调用方
