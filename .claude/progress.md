# CLFCode 任务进度

## 已完成

### 2026-08-10 文件修改 diff 渲染（v1 基本可用） ✅
- write_file / edit_file 工具均展示事前 diff 预览（`+`/`-` 行 + hunk header + 行号）
- LCS 行级 diff + 超限截断（3000 行 / 500KB）+ 换行符标准化
- 原子写入（tmp + flush + MoveFileEx/rename）+ EXDEV 降级
- FileSnapshot + TOCTOU 乐观锁校验
- Edit/Manual 模式超限阻断（不强迫盲批）
- 设计文档：[设计-文件修改diff渲染](../../设计/设计-文件修改diff渲染.md)

### 2026-08-07 显示区信息降噪方案 ✅
### 2026-08-07 快捷键方案 — 全部 3 批完成 ✅
### 2026-08-07 推理过程显示（中间发现） ✅
### 2026-08-06 代码清理 + OCP 重构 + 组件提取 + CJK 缓解 ✅
### 2026-08-05 UI 全面重构 ✅
### 2026-08-04 Harness 架构重构 ✅
### 2026-08-03 FTXUI 终端 UI 重构 ✅
### 全量优化 P0-P3 ✅

## 进行中

### 显示体验打磨
- [ ] diff 颜色（ANSI 被 emitContent 过滤 → 需 FTXUI color 方案）
- [ ] 工具输出间距（工具调用之间加空行分隔）
- [ ] 双计时器：Timer #1 每动作 / Timer #2 整体任务

## 待做

### CLFTurnRunner — Turn 生命周期管理
- 提取 AgentLoop 中计时/重试/中断逻辑
- 设计文档：[设计-CLFTurnRunner-生命周期管理](../../设计/设计-CLFTurnRunner-生命周期管理.md)

### 已知问题
- Ctrl+C 确认栏退出（低优先，暂缓）
- emitRaw 钩子（设计预留）
