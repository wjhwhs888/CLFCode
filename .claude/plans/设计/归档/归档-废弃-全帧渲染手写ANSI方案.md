# 设计-终端UI重构-全帧渲染

> 状态：设计中 | 创建：2026-08-04

## 1. 废弃方案总结

以下方案已尝试并证实存在不可解决的架构问题，归档于 `归档/` 目录：

| 方案 | 核心思路 | 致命缺陷 |
|:---|:---|:---|
| DECSTBM 紧凑布局 | 动态 `contentBottom()` + `compactRedraw` | `\033[2J` 在 DECSTBM 下不可靠、旧固定区清除范围不匹配 |
| 备用屏幕 + 全帧渲染 | `\033[?1049h` + `clearScreen` + `fullRender` | 备用屏幕无 scrollback 历史、`clearScreen` 推空白行污染滚动 |
| 行内重定位 `recompact` | 不清屏，只移固定区 | 逐行清除范围基于新 `contentBottom`，旧固定区清不到 |

**共同根因**：试图在 DECSTBM 滚动区 + 绝对定位固定区之间做"紧凑重排"，但两者的坐标体系从根本上不可调和。

## 2. 新方案：放弃紧凑，终端自然滚动

### 核心思路

**不再尝试"紧凑布局"**。回归终端最自然的工作方式：
- 内容从上往下流式输出，终端自动滚动
- 固定区始终钉在终端底部（绝对定位）
- 每次提交像"新页面"一样从顶部开始
- 历史内容自然进入终端 scrollback

### 终端模型

```
┌─ 终端可见区 (H行) ─────────────┐
│                                │
│  ⑦ 滚动内容区 (第1行 ~ H-fixed) │
│  - 自然滚动                     │
│  - 不设 DECSTBM（内容区=全屏）   │
│                                │
│  ─── clfcode ───  ⑧ 上分隔线    │
│  · Thinking…      ⑥ 状态区      │
│  > 输入内容        ⑤ 输入区      │
│  ───────────────  ⑨ 下分隔线    │
│  edit mode on     ④ 模式行      │
│  [●]确认 [ ]取消   ③ 确认区      │
└────────────────────────────────┘
```

### 关键约束

1. **不设 DECSTBM**（或仅在流式输出期间临时设置）
2. **不清屏**（不调 `\033[2J`，不调用 `clearScreen`）
3. **每次提交：打印分隔空行，内容自然流动**
4. **流式期间：DECSTBM 保护固定区，流式结束移除 DECSTBM**
5. **固定区始终在底部，用绝对定位刷新**

### 数据流

```
[启动]
main.cpp → 配置加载 → 日志初始化 → 进入 raw 模式
  ↓
initLayout: 清状态 + 清屏(\033[2J) + 绘固定区@底部 + 光标归位
  ↓
printBanner: 输出 banner 到滚动区 (scrollPrint)
  ↓
drawInput(""): 绘制输入光标
  ↓
[REPL 主循环]
  ↓
用户输入 "你是谁" → 回车
  ↓
submit():
  toContentArea: 清屏 + DECSTBM + 固定区@底部
  scrollPrint("> 你是谁\n")
  scrollPrint("● CLFCode: ")
  runTurn(): 流式输出 (HTTP 回调 → scrollPrint)
  thoughtMark / clearStatus / scrollPrint("\n")
  // 流式结束后：重置 DECSTBM，固定区保持在底部
  redrawAll() 或 renderFixedArea()

[下一轮]
  同上
```

### 与现状的差异

| 项目 | 当前实现 | 新方案 |
|:---|:---|:---|
| 启动清屏 | `\033[2J\033[H` | 保持 |
| 流式布局 | `toContentArea` → DECSTBM + 固定区 | 保持 |
| 流式后处理 | 无 | `resetSR` → `renderFixedArea` |
| 固定区定位 | `lowerSepRow=H-2` 等 | 保持（绝对底部） |
| contentBottom | 始终 `H-4-inputLineCount-statusLineCount` | 保持（不搞动态） |
| 紧凑 | 不需要 | 不需要 |

### 本次不需要做的事

- ~~紧凑布局~~
- ~~备用屏幕~~
- ~~动态 contentBottom~~
- ~~fullRender / beginStream / endStream~~
- ~~内容重印~~
- ~~行内重定位固定区~~
- ~~lowerSepRow 改为内容相对定位~~

## 3. 实施步骤

### Step 1: CLFTerminal 清理
- 移除所有之前尝试中新增的方法和成员
- 保持原始 DECSTBM 布局逻辑不变
- `showThinking` 清除行数从 `10` 改为 `oldSL`（已有修复需移植）

### Step 2: 流式结束后刷新固定区
- `submit()` 末尾添加 `resetSR(); renderFixedArea();`
- 确保固定区不被流式期间的状态清理覆盖

### Step 3: 测试验证
- 启动显示正常（banner + 固定区）
- 输入提交 → 响应显示正常
- 固定区始终可见
- 滚动历史可查
- 确认对话框显示正常
- Shift+Tab 模式切换正常

## 4. 改动清单

### CLFScrollBuffer（已完成 ✅）
- `m_pending` 跨调用累积
- `flushPending()` + `clear()` 更新

### CLFTerminal.hpp（需改动）
- 无新增方法声明（回退到原始）
- 无需 compactLayout / fullRender 等

### CLFTerminal.cpp（需改动）
- `showThinking`: 仅清除 `oldSL` 行（移植之前的修复）
- `clearStatus`: 仅清除 `oldSL` 行（移植之前的修复）
- 其余保持原始不变

### CLFRepl.cpp（需改动）
- `submit()` 末尾：`resetSR() + renderFixedArea()`

## 5. 验证

1. 构建：`cmake --build cmake-build-debug -j6`
2. 运行 `ctest --test-dir cmake-build-debug --output-on-failure -j6`
3. 手动测试：
   - 启动 → banner + "> " + mode（一份）
   - 输入 "你是谁" → 流式输出 → 固定区可见
   - 输入 "请回复，连接正常" → 响应正常
   - 可以向上滚动查看历史
   - Shift+Tab 模式切换正常
