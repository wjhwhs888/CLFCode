# CLFCode 任务进度

## 已完成

### 2026-08-07 快捷键方案 — 第 1 批核心输入输出 ✅

**功能确认**：
- ✅ Enter/Ctrl+D 提交，Ctrl+N 换行，滚轮/翻页/跳转，Tab 不切焦点
- ✅ Shift+Tab 模式切换（含确认栏期间）
- ✅ 确认栏两选项（确认/返回），"返回"=拒绝+中断+回编辑，ESC/CtrlC 同行为中断
- ✅ ESC 中断后输入框回显上次提交内容
- ✅ ESC 中断后再次输入正常工作
- ✅ 确认栏 Shift+Tab 透传模式切换
- ✅ `[1;1R` CPR 残留剥离（CatchEvent 尾部 + Renderer 双重）

**关键修复**：
- ESC 单次即中断：根因为 FTXUI v7 首次 ESC 生成 `Event::Special({27,27})`（双字节），不是 `Event::Special({27})`（单字节）。同时匹配两者即可
- 移除 `input->TakeFocus()`：修复光标频繁闪烁问题
- `m_interrupted` 在 `runTurn()` 开头重置
- Agent 三层中断检查：流式返回后 + ToolExecutor 逐个 tool 前 + 迭代开始
- 确认栏线程安全：`isConfirmActive()`/`setConfirmActive()` + `notify_one` 前加 `m_confirmMutex`

**设计文档**: [plans/设计-快捷键方案设计.md](../plans/设计-快捷键方案设计.md)

**文件变更**: `CMakeLists.txt`, `CLFRepl.hpp/cpp`, `CLFTerminal.hpp/cpp`, `CLFConfirmBar.cpp`, `CLFAgentLoop.cpp`, `CLFToolExecutor.hpp/cpp`, `CLFHttpClient.hpp/cpp`, `ICLFHttpClient.hpp`

### 2026-08-06 代码清理 + OCP 重构 + 组件提取 + CJK 缓解 ✅
### 2026-08-05 UI 全面重构 ✅
### 2026-08-04 Harness 架构重构 ✅
### 2026-08-03 FTXUI 终端 UI 重构 ✅
### 全量优化 P0-P3 ✅

## 待做

### 快捷键方案 — 第 2 批：Alt+Enter 换行 + 双击退出
### 快捷键方案 — 第 3 批：剪贴板 + 历史导航

## 已知问题

### Ctrl+C 确认栏退出
- Ctrl+C 在确认栏激活时经步骤 4 分流到 ExitLoopClosure，影响小，暂缓

### CJK 光标半字移动
- 根因在 FTXUI 内部

### emitRaw 钩子
- 接口保留，无调用方
