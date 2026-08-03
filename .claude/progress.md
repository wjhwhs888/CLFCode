# CLFCode 任务进度

## 已完成

### 全量优化 P0-P3 ✅
- P0: Bug修复(5项) — 命令执行/HTTP RAII/序列化/原子变量/线程安全
- P1: 架构解耦 — CLFTypes提取 + 头依赖治理 + CMake链接
- P2: 大文件拆分(8新模块) + 去重(CLFEncoding/CLFMessageCodec) + 错误码
- P3: 6区终端UI + 事件系统 + 日志优化 + 多行输入/光标/快捷键

### 6区终端UI
- ✅ 布局: ③确认 ④模式 ⑤输入 ⑥状态 ⑦滚动 + LayoutEngine
- ✅ 事件: CLFEvent(25种) + CLFEventQueue(256环形) + 主循环dispatch
- ✅ 流式输出: DECSC/DECRC保护光标 + emitContent双通道(事件+直渲)
- ✅ 光标: prefixW对齐 + col计算 + Home/End/↑↓导航
- ✅ 多行输入: Shift+Enter换行 + 输入区动态扩展

## 遗留问题

### 1. 确认对话框期间固定区消失
- 现象: 高风险确认弹窗时，输入区和模式行不可见
- 原因: confirmDialog在主循环内同步阻塞，drawInput/drawMode不执行；showConfirm的DECSC/DECRC可能与固定区渲染冲突
- 尝试: showConfirm+drawInput组合 → 未完全解决
- 方向: 确认对话框需要独立的固定区保活渲染，或改为异步事件驱动

### 2. 每次输入后终端"刷新"效果
- 现象: submit时toContentArea的\033[2J全屏清除
- 状态: 设计行为（提交后清屏重新布局），非bug

### 3. 底部区域重复重绘
- 现象: 主循环每帧调用drawInput+renderFixedArea全量重绘固定区
- 状态: 低优先级优化项，可加dirty标记避免无变化重绘
