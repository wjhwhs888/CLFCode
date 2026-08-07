# CLFCode 任务进度

## 已完成

### 2026-08-07 快捷键方案 — 第 1 批核心输入输出（ESC 待解决）

**可工作功能**：
- ✅ Enter/Ctrl+D 提交，Ctrl+N 换行，滚轮/翻页/跳转，Tab 不切焦点，Shift+Tab 模式切换
- ✅ 确认栏两选项（确认/返回），"返回"=中断+回编辑，←→ 切换
- ✅ ESC 后输入框回显上次提交内容
- ✅ ESC 中断后再次输入正常工作（`CLFAgentLoop::m_interrupted` 已重置）
- ✅ `[1;1R` CPR 残留剥离（CatchEvent 尾部 + Renderer 双重清理）

**已知问题**：
- ❌ ESC 需按两次才中断：根因定位为 **httplib 内容回调返回 false 后仍 drain 剩余 body**，Agent 工作线程卡在 `recv()` 无法检查中断标志。"返回"能工作因为 Agent 在用户态（tool executor 循环内检查 `m_interruptFlag`）

**尝试过但无效的方案**：
- 缩短读超时（httplib `SO_RCVTIMEO` 不生效）
- 去掉 `stop()` 只靠 `m_aborted`（下一个 chunk 不触发回调，httplib 不响应 return false）
- Windows `CancelSynchronousIo`（取消一次 recv 后 httplib 立即发起下一次）

**设计文档**: [plans/设计-快捷键方案设计.md](../plans/设计-快捷键方案设计.md) — 终审通过

**文件变更**: `CMakeLists.txt`, `CLFRepl.hpp/cpp`, `CLFTerminal.hpp/cpp`, `CLFConfirmBar.cpp`, `CLFAgentLoop.cpp`, `CLFToolExecutor.hpp/cpp`, `CLFHttpClient.hpp/cpp`

**下一步**: 用 libcurl 替换 httplib，自建 `CLFHttpClient`，`XFERINFOFUNCTION` 回调轮询中断标志

### 2026-08-06 代码清理 + OCP 重构 + 组件提取 + CJK 缓解 ✅
- (略，详见 git log)

### 2026-08-05 UI 全面重构 ✅
- (略)

### 2026-08-04 Harness 架构重构 ✅
- (略)

### 2026-08-03 FTXUI 终端 UI 重构 ✅
- (略)

### 全量优化 P0-P3 ✅
- (略)

## 进行中

### libcurl 替换 httplib — 解决 ESC 中断阻塞问题
- 待开始

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