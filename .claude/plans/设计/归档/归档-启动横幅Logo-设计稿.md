# 归档-启动横幅Logo-设计稿

> 状态：**已实施完成 → 已归档（2026-09-22，用户实机验收通过，随 v0.8.3 发布）**
> 范围：启动横幅（`CLFRepl::printBanner()`）+ 折行口径（`CLFReplView` + `CLFTextUtil`）+ 版本访问器抽取
>
> 三稿关系：
> - 本文 = **待实施主体**（定稿）
> - [归档-启动横幅Logo-决策简报.md](./归档-启动横幅Logo-决策简报.md) = 决策入口（已拍板）
> - [归档-启动横幅Logo-审查报告.md](./归档-启动横幅Logo-审查报告.md) = 审查依据（历史记录，5 致命 + 11 中等 + 8 轻微）
>
> 本定稿已吸收审查报告全部结论（F0-F5 修正均已落入对应章节）与 pro 拍板记录。

---

## 0. 前置说明

本文遵循 [分析-agent流程门禁与任务分级设计.md](../分析/分析-agent流程门禁与任务分级设计.md)：
设计自洽 ≠ 符合现实——本文所有"现状"附 file:line；实施时发现出入以源码为准并回写本文。

**分工约定**：flash/用户完成前期调研与审查；pro 完成拍板定稿与实施。
文中代码片段均为**规格说明**，由 pro 实施时落地。

---

## 拍板记录（pro 主审，2026-09-22）

| # | 项 | 裁决 |
|---|---|---|
| 路线 | (a) ASCII / (b) 修折行口径 / (c) 免折行 | **(b)**，前置 = 步骤 0 块字字体实机验证；若块字渲染 2 格则降级 (a) |
| Q3 | parser 修根 vs 立书写约定 | **立书写约定**（颜色在外、修饰在内、包裹内不拼接多段） |
| Q4 | skills 数 | **保留**（环境层第 2 行，`loadFromDir` 调用随之保留） |
| Q5 | 版本访问器 | 归 `CLFConfigLoader`，三态契约（§5.1） |
| Q7 | ⎿ 前缀 | **保留**（全仓 79 处通用列表项符号；`▸` 宽度未取证不引入） |
| M8 | 恢复会话路径语义 | 保持"进程启动目录"，文档写明即可 |
| M9 | probe 解析复刻 | 链接生产 parser；折行抽 `wrapLines` 三用 |
| 顺手批 | `CLFTextUtil.hpp:43` 错误注释 | 修正（"maxW<=0 返回原串"与实现矛盾） |

---

## 1. 需求（用户原话收敛）

1. Logo 作为**第一眼识别标识**，与轻量信息块**不冲突**——两者可共存，但轻量信息块应**淡化**
2. 淡化依据：模型、工作目录等已在终端底部常亮显示（模型 ✓ 真冗余；工作目录 △ 仅叶子名，见 §3.2）
3. 内容目标：**logo + `CLI Agent Framework for Code` + 版本**
4. 用户明确要求：在**品牌层下方**加一行显示完整路径（现状第 3 行已部分满足——本改版为**替换该行**非并存，见 §4.3）
5. 排版由 flash 提案、用户确认方向（块字）；用户自述无美学判断力，需明确提案而非开放式提问
6. 输出方式：**静态**一次性输出，不做动效
7. 版本号：复用现有接口，UI 层调用获取（现状无此接口，需抽取，见 §5.1）

---

## 2. 现状取证

### 2.1 现行 `printBanner()`【已取证】

[CLFRepl.cpp:277-293](../../../src/CLFUI/CLFRepl.cpp#L277-L293) 逐行 `emitContent` 发射 6 行：

| # | 内容 | 配色 |
|---|---|---|
| 1 | `● CLFCode` — CLI Agent Framework for Code | **仅 `● CLFCode` 加粗**（L5 修正：后半截 `— CLI Agent...` 在 `bold()` 之外，不加粗） |
| 2 | `⎿` + `diagnosticInfo()`（终端尺寸 + ANSI 开关） | gray |
| 3 | `⎿ 工作目录: <cwd 全路径>` | cyan |
| 4 | `⎿ 配置: <apiBaseUrl>` | cyan |
| 5 | `⎿ 模型: <modelName>` | cyan |
| 6 | `⎿ 知识库: N skills` | cyan（`sc > 0` 才显示） |

调用点：[CLFRepl.cpp:168](../../../src/CLFUI/CLFRepl.cpp#L168)，位于 `enableAnsi()`（135 行）之后，
故 `s_enabled == true`，着色生效。

> ⚠️ **历史事故（改动前必读）**：135 行注释记载
> "`enableAnsi` 声明后全仓零调用 → `● CLFCode` 无色无加粗"（2026-09-08 修复）。
> 改动本区域时勿破坏 135 行的调用位置与其早于 168 行的时序。

### 2.2 渲染链路与两条硬约束【已取证】

```
printBanner() → m_output->emitContent(...)     // CLFTerminal.cpp:110
   ├─ SGR 白名单过滤（非 SGR 序列剥离，防模型注入 OSC）
   └─ 遇 '\n' 切行 → m_contentBuffer 逐行
        └─ CLFReplView 硬折行 → CLFAnsiParser::parse(line)  // 单行解析
```

**约束 1：SGR 不跨行。** `parse()` 以单行为单位，`emitContent` 在 `'\n'` 处切行
（[CLFTerminal.cpp:138-150](../../../src/CLFUI/CLFTerminal.cpp#L138-L150)）。
→ **多行 logo 必须逐行独立着色**；整体包色只有首行生效（现行 `printBanner()` 逐行调用的原因）。

**约束 2：`CLFAnsi` 无"亮青 + 加粗"原语。** 渲染层可识别码仅
`1/22/0/30-37/39/90-97`（[CLFAnsiParser.cpp:38-44](../../../src/CLFUI/CLFAnsiParser.cpp#L38-L44)），
`CLFAnsi` 对外仅暴露 `cyan(36)/cyanLight(96)/red(31)/gray(90)/bold(1)`
（[CLFAnsi.hpp:20-25](../../../src/CLFUI/CLFAnsi.hpp#L20-L25)）。
→ 品牌主色定为亮青加粗 = `cyanLight(bold(x))` 组合调用即可，**无需新原语**（两者均为既有包装）。

**约束 3：无 256 色/真彩能力。** parser 不识别 `38;5;n` / `38;2;r;g;b`。
→ 渐变方案需扩 parser 与 `CLFAnsi` 两层——**不做**（未采纳方案表）。

### 2.3 嵌套包装语义（F2 修正重写——原"缺陷"判定已被证伪）

`CLFAnsiParser` 的重置是**状态清空**而非栈恢复
（[CLFAnsiParser.cpp:39](../../../src/CLFUI/CLFAnsiParser.cpp#L39)：
`if (code == 0) { bold = false; fg = -1; return; }`）。

**裁决后的正确语义**（裁决员 C 依源码定论 + pro 亲验）：

| 写法 | 解析结果 | 说明 |
|---|---|---|
| `cyan(bold(x))` | ✅ fg=36, bold=true | `flush()` 在 `applyCode` **之前**（:71），落段是**值快照**（:58） |
| `bold(cyan(x))` | ✅ fg=36, bold=true | **同样正确**——单段包裹双序等价 |

**真正的失效形态（与真实终端一致，根因在生成侧）**：

```
bold(cyan(A)+gray(B)) → "\033[1m\033[36mA\033[0m\033[90mB\033[0m\033[0m"
                      → A 段 bold+cyan ✅，B 段 gray 丢 bold ❌
```

机制：`CLFAnsi` 包装模板是 `\033[Xm` + s + `\033[0m`（全清）
（[CLFAnsi.cpp:32-36](../../../src/CLFUI/CLFAnsi.cpp#L32-L36)），
外层包装内**拼接多个子段**时，内层全清 reset 抹掉外层属性——真实终端同样如此。

**裁决（Q3，拍板记录）**：**立书写约定**，不修 parser、不修 `CLFAnsi`：

1. 颜色在外、修饰在内：`cyan(bold(x))`
2. 包裹内不拼接多段：`bold(cyan(A)+gray(B))` 属禁用写法；需多段时**逐段独立包装**后拼接
3. qa 固化：双序等价用例 + 跨段拼接语义用例（钉"与真实终端一致"的现有行为）

**不修根的理由（F0d）**：全仓 `bold(cyan(...))` 零处、嵌套仅 2 处且均为安全形态
（[CLFRepl.cpp:316](../../../src/CLFUI/CLFRepl.cpp#L316)、
[CLFCommands.cpp:267](../../../src/CLFUI/CLFCommands.cpp#L267)）；
修 parser（栈恢复）解决不了生成侧问题，且把变更面扩到所有内容渲染——YAGNI。

---

## 3. 现状再评估

### 3.1 "已在底部显示"的逐项核实【已取证】

底部常亮行定义于 [CLFReplView.cpp:378-399](../../../src/CLFUI/CLFReplView.cpp#L378-L399)：

| 横幅现有行 | 底部是否已有 | 结论 |
|---|---|---|
| 模型 | ✓ [381 行](../../../src/CLFUI/CLFReplView.cpp#L381) `m_modelName` | **真冗余 → 删除** |
| 工作目录 | △ [387-389 行](../../../src/CLFUI/CLFReplView.cpp#L387-L389) 仅 `filename()` | **半冗余**，见 §3.2 |
| 诊断（终端尺寸/ANSI） | ✗ | 无消费场景 → **删除** |
| 配置（apiBaseUrl） | ✗ | 面向使用者的横幅不应显示 API 端点 → **删除**（§3.3） |
| 知识库 skills 数 | ✗ | **保留**（Q4 裁决，§3.4） |

### 3.2 反证：工作目录**不是**完全冗余【已取证】

底部显示的是 `std::filesystem::u8path(getWorkingDir()).filename()`
（[CLFReplView.cpp:385-389](../../../src/CLFUI/CLFReplView.cpp#L385-L389)）
——**仅叶子目录名，不含父路径**。

**推论**：多副本场景下底部 `CLFCode` 无法区分实例
（本仓库即有 `build/`、`build_rel/`、`cmake-build-debug/`、`cmake-build-release/` 四套构建目录）。

→ 用户需求 §1-4（完整路径）与此反证一致，本改版保留全路径。

**M8 语义注记（拍板记录）**：路径行标的是**进程启动目录**
（`CLFConfigLoader::getWorkingDir()`），非会话工作区。
`/resume` 恢复其他目录创建的会话时不重发 banner，路径行可能误导——
**裁决：保持现状语义**（从会话文件取工作区改动面不值），文档写明即可。

### 3.3 配置行（apiBaseUrl）—— 删除

面向使用者的启动横幅不应显示 API 端点；非交互模式不经过此处（§5.2），
无 CI 可见性需求。确有需要时 `/help` 或配置命令可覆盖。

### 3.4 知识库 skills 数 —— 保留（Q4 裁决）

- 保留理由（裁决采纳）：**唯一"系统真的装好了"的正向信号**
  （skills 加载失败时静默为零，用户无从察觉）；属"确认系统状态"而非"展示配置"
- 位置：环境层第 2 行（§4.5 行序规格）
- **M3-b 连带**：`CLFSkillLoader::loadFromDir` 调用（[CLFRepl.cpp:290](../../../src/CLFUI/CLFRepl.cpp#L290)）
  必须保留（展示依赖该调用）

---

## 4. 排版提案（定稿）

### 4.1 设计意图

现行横幅把 **UI 呈现**（品牌，宜重）与 **诊断转储**（一次性启动信息，宜轻）
混在同一视觉层级。**拆为两个层级**：品牌层（块字 + tagline + 版本，重）+
环境层（完整路径 + skills，轻）。

气质匹配：项目视觉语言克制（细线 `⎿`、灰/青双色、底部 `dim` 次级信息）。
块字**窄、单色、无渐变**，避免与整体气质断裂。

### 4.2 品牌层（F0/F1 修正重写——宽度论证前提已重建）

**样例（ANSI Shadow）**：

```
  ██████╗██╗     ███████╗ ██████╗ ██████╗ ██████╗ ███████╗
 ██╔════╝██║     ██╔════╝██╔════╝██╔═══██╗██╔══██╗██╔════╝
 ██║     ██║     █████╗  ██║     ██║   ██║██║  ██║█████╗
 ██║     ██║     ██╔══╝  ██║     ██║   ██║██║  ██║██╔══╝
 ╚██████╗███████╗██║     ╚██████╗╚██████╔╝██████╔╝███████╗
  ╚═════╝╚══════╝╚═╝      ╚═════╝ ╚═════╝ ╚═════╝ ╚══════╝
```

**宽度事实（审查员 B + 主审实测，勿沿用旧稿"≤40 列"）**：

| 事实 | 值 |
|---|---|
| 最宽行码点数 | 58（**无任何 ≤40 的现成变体**——"紧凑变体"同样 58，F1） |
| 现行折行口径（`charWidth` 多字节恒 2） | 最宽行算 **110 列** → 80 列终端**必被劈成两行残片且续段掉色**（F0） |
| **b 路线后折行口径（`renderCharWidth` 渲染同表）** | 块字行 **58 列** → 121 列终端余量 63 ✅；**80 列终端存活**（58 ≤ 80）✅ |

**b 路线要点（拍板记录核心）**：折行切渲染口径后块字成立的前提 = 每行渲染宽度
≤ `wrapW`（80 列终端即存活）。**但字体层风险仍在**（§4.4/M11）——块字最终可行
性由步骤 0 实机验证决定，若块字渲染 2 格则整体降级 ASCII 变体（路线 a）。

**排版决策**：

| 决策 | 理由 |
|---|---|
| 块字 6 行 | 与 `⎿` 信息块重量拉开才不冲突（用户拍板方向） |
| 块字**单色** `cyanLight(bold(...))` | 克制气质；渐变需扩 parser + `CLFAnsi` 两层（不做）；**逐行独立包装**（约束 1：SGR 不跨行） |
| 包装顺序 `cyanLight(bold(x))` | §2.3 书写约定：颜色在外、修饰在内 |
| tagline 与版本**同一行**（§4.5 行 7） | 省一行；版本单空格紧跟 tagline + gray（**用户验收修订 2026-09-22**：右对齐在宽终端有 80+ 列空白视觉断开，改紧随） |
| 块字与 tagline 无空行 | 两者同属品牌层，视觉成组 |
| 品牌层与环境层之间**空行** | 层级分离的视觉手段（F5 悬空点裁决：留空行） |
| **窄终端降级** | `w > 0 && w < kBlockLogoMinWidth` 时**跳过块字**（仅 tagline + 环境层）；阈值 = 块字实测宽度 + 余量。判断不可只写 `w < 阈值`（`getTerminalWidth()` 失败返回 **-1**，[CLFAnsi.cpp:53-62](../../../src/CLFUI/CLFAnsi.cpp#L53-L62)——ConPTY 取不到尺寸时误判降级） |
| **块字阈值（步骤 0 回填 ✅）** | `kBlockLogoMinWidth = 62`（实测最宽行 `string_width` = 58 + 4 余量；步骤 0 实机：变体 1 最宽 58 / 紧凑 57 / ASCII 兜底 32） |

### 4.3 环境层（F4 修正重写——替代非并存 + Q7 裁决）

用户要求"logo 下方加一行完整路径"——**现状第 3 行已部分满足**，
本改版将现行第 3 行**替换**为环境层路径行（**非并存**，F4 消除"两行路径"歧义）。

**Q7 裁决：保留 `⎿` 前缀。** 审查报告已取证——`⎿` 全仓 79 处，是**通用列表项
装饰符号**（`/help`、`/config`、`/session`、工具输出、横幅均用），语义 =
"下面这些是细节条目"，**不蕴含"属于某次操作"**。原稿"⎿ = 诊断输出"的论证被证伪，
环境信息保留 `⎿` 反而更符合项目一致性。`▸` 不引入（宽度未取证，有复现拖选偏移风险）。

| 排版点 | 定稿 |
|---|---|
| 前缀 | `⎿`（Q7 裁决保留） |
| 配色 | `gray`（降权，与品牌层主色区分） |
| 缩进 | 1 空格（对齐块字**视觉左缘**——ANSI Shadow 字形行 2-5 前导 1 空格；L1 建议采纳） |
| skills 数 | 保留，环境层第 2 行 `⎿ 知识库: N skills`（gray，`sc > 0` 才显示） |

### 4.4 稳定性风险（M11 增强——字体歧义宽度）

块字混用两套字符集：**U+2580–259F 区块元素**（`█`）与 **U+2550–256C 框线**
（`╗ ╔ ═ ║ ╚ ╝`）。两者字体覆盖率与度量不同，风险分两层：

1. **缺字/粗细/基线不一致**（原稿已列）：Windows Terminal + CJK 字体下实心块与
   框线可能粗细不一，甚至豆腐块。
2. **宽度歧义（M11，比缺字更隐蔽）**：U+2500–259F 在 Unicode `EastAsianWidth.txt`
   中是 **Ambiguous(A)**；CJK 语境终端渲染成 **2 格**有历史先例（GNOME VTE 有专门
   "CJK: Fix width of Box Drawing and Block Elements" 提交）。一旦命中，FTXUI
   的 Screen 缓冲格数（宽表不含 U+2500–259F → 恒按 1 格布局）与实际终端格数
   **差一倍** → 整行右移/回绕，**不缺字、不报错，只是整体错位**。

→ 全案**唯一无法靠读代码消除**的不确定性，**步骤 0 必须实机验证**（§7 步骤 0，
含 58 列标记校验）。

**步骤 0 实机结论（2026-09-22，用户 PowerShell 实跑）**：全部断言通过。
四变体无缺字、块字行上下对齐（**每字符 1 格渲染，M11 歧义宽度未命中**）、
双序等价实证（生产链路）、前导空格保真、ASCII 兜底无 `|` 开头行、最宽 58 ≤ 80。
→ **块字方案成立**，按路线 (b) 继续实施。

### 4.5 完整行序规格（F5 新增——逐行定稿）

```
行 1   "  ██████╗██╗     ███████╗ ██████╗ ██████╗ ██████╗ ███████╗"   cyanLight+bold
行 2   " ██╔════╝██║     ██╔════╝██╔════╝██╔═══██╗██╔══██╗██╔════╝"   cyanLight+bold
行 3   " ██║     ██║     █████╗  ██║     ██║   ██║██║  ██║█████╗"     cyanLight+bold
行 4   " ██║     ██║     ██╔══╝  ██║     ██║   ██║██║  ██║██╔══╝"     cyanLight+bold
行 5   " ╚██████╗███████╗██║     ╚██████╗╚██████╔╝██████╔╝███████╗"   cyanLight+bold
行 6   "  ╚═════╝╚══════╝╚═╝      ╚═════╝ ╚═════╝ ╚═════╝ ╚══════╝"     cyanLight+bold
行 7   " CLI Agent Framework for Code" + " " + gray("v0.8.2")             tagline 无前缀、版本紧跟（用户验收修订：右对齐在宽终端视觉断开，改紧随）
行 8   （空行——品牌层与环境层分隔）
行 9   gray(" ⎿ 工作目录: " + cwd)                                       环境层 1
行 10  gray(" ⎿ 知识库: N skills")（sc > 0 才显示）                       环境层 2
```

- 行 1-6：每行独立 `emitContent(cyanLight(bold(line)) + "\n")`（SGR 不跨行）
- 行 7：版本号经 §5.1 访问器获取，单空格紧跟 tagline（用户验收修订——原右对齐
  填充方案作废，零边界计算）
- 行 8：`emitContent("\n")`（产生空行条目进 `m_contentBuffer`，渲染为空行）
- 行 9-10：`gray` 包装，替代现行第 2-6 行信息块（现行 6 行 → 定稿 9-10 行，M6 高度已入验收）
- 所有行保持 `if (m_output)` 空指针守卫（M3-a）
- 窄终端降级（§4.2）：`w > 0 && w < kBlockLogoMinWidth` → 跳过行 1-6，其余照发

---

## 5. 关联改动

### 5.1 版本访问器（F3 修正重写——三态契约）

**现状：无现成接口，逻辑重复两份且"逐字相同"不成立**（F3 亲验，三处实质差异）：

| 差异 | [main.cpp:42-53](../../../src/main.cpp#L42-L53) `printVersion()` | [CLFCommands.cpp:317-331](../../../src/CLFUI/CLFCommands.cpp#L317-L331) `cmdVersion()` |
|---|---|---|
| unknown 兜底 | `else` 分支输出 | 变量**预置** `"unknown"` |
| 流打开检查 | **只查 `exists`，不查 `is_open()`** | 查了 `is_open()` |
| 输出形制 | `std::cout << v << std::endl` | `emitContent("● CLFCode " + version + "\n")` |

**可达差异分支（既有缺陷）**：`VERSION` 存在但打不开（权限/被占用/是目录）时，
`--version` 输出**空行**，`/version` 输出 `● CLFCode unknown`。

**定稿契约**（Q5 裁决）——归 `CLFConfigLoader`（clf_core；CLFCommands 在 clf_ui
依赖 clf_core，方向允许）：

```cpp
// 读取应用版本（VERSION 文件首行）——三态契约：
//   文件不存在 → "unknown"；存在但读取失败 → ""；成功 → 首行内容。
// 不吞异常（resolvePath 抛异常时保持 set_terminate 语义，M4）。
// example:
//std::string v = CLFConfigLoader::readVersionFile();
static std::string readVersionFile();
```

**两调用点改造（输出逐字保持）**：

```cpp
// main.cpp printVersion()
std::cout << CLF::CLFCore::CLFConfigLoader::readVersionFile() << std::endl;
// 逐字一致：不存在→"unknown"；打不开→空行（is_open 缺陷随契约自然消失，输出不变）

// CLFCommands.cpp cmdVersion()
std::string version = CLFConfigLoader::readVersionFile();
if (version.empty()) version = "unknown";   // 存在但打不开：保持现状 "unknown"
if (output) output->emitContent("● CLFCode " + version + "\n");
```

- 不扩展 `CLFPluginApi` 宿主面（ABI 版本 1，增虚函数破坏二进制兼容）
- CMake 第三版本源（[CMakeLists.txt:2](../../../CMakeLists.txt#L2) `project(CLFCode VERSION 0.1.0)`
  与 `VERSION` 文件不一致——L6）为顺手批候选，实施时确认无消费点后同步

### 5.2 非交互模式：无需开关【已取证】

[main.cpp:155-162](../../../src/main.cpp#L155-L162)：`nonInteractive` 时直接
`agent.runTurn()` 后 `return 0`，**不构造 `CLFRepl`**。
→ Logo 不会进入 `--prompt` 的 CI 输出，**不需要 `--no-logo` 开关**。

### 5.3 tagline 一致性消费点（M7 补齐）

| 位置 | 处理 |
|---|---|
| [main.cpp:30](../../../src/main.cpp#L30) `printHelp()` | **不改** —— `--help` 走 `std::cout` 纯文本，须保持可管道化 |
| [release.ps1:161](../../../release.ps1#L161) | 不改（发行说明文本） |
| [README.md:1](../../../README.md#L1) | 不改 |
| [CLFRepl.cpp:342](../../../src/CLFUI/CLFRepl.cpp#L342) 每轮对话 `● CLFCode:` 锚点 | **保持不动（裁决）**——对话流标记与启动"第一眼"品牌不同场景；块字 banner 与轻量锚点同屏不冲突 |
| [CLFCommands.cpp:329](../../../src/CLFUI/CLFCommands.cpp#L329) `/version` 输出 | 保持形制（§5.1 改造后不变） |

→ 仅横幅改版，不追求全局字符串统一（场景语义不同）。

### 5.4 嵌套语义处置（Q3 裁决——立约定，§2.3）

**不修 parser、不修 `CLFAnsi`。** 约定：

1. 颜色在外、修饰在内：`cyan(bold(x))`
2. 包裹内不拼接多段；需多段时逐段独立包装后拼接
3. qa 固化（§7 步骤 5）：
   - 双序等价用例（`cyan(bold(x))` ≡ `bold(cyan(x))`，单段包裹）
   - 跨段拼接语义用例（`bold(cyan(A)+gray(B))` → B 丢 bold——钉"与真实终端一致"的现有行为，防未来意外改变）

---

## 6. 裁决记录（原待裁决点 → 全部已裁决）

| # | 待裁决 | 裁决 | 落点 |
|---|---|---|---|
| Q1 | 环境层排版细节 | 见 §4.3/§4.5（⎿ 保留、gray、2 空格缩进） | §4.5 行 9-10 |
| Q2 | 块字字符集备选 | **备 ASCII 变体**（零字体风险兜底）；步骤 0 实机决定主形 | §7 步骤 0 ① |
| Q3 | parser 修根 vs 立约定 | **立书写约定** | §2.3/§5.4 |
| Q4 | skills 数 | **保留** | §4.5 行 10 |
| Q5 | 版本访问器归属 | **CLFConfigLoader 三态契约** | §5.1 |
| Q6 | 窄终端降级 | **跳过块字**（仅 tagline + 环境层）；阈值步骤 0 回填 | §4.2 |
| Q7 | ⎿ 前缀语义 | **保留**（审查已取证，全仓通用列表项符号） | §4.3 |

---

## 7. 实施步骤

### 步骤 0【前置门】字形 / 字重 / 宽度实机验证

**交付物**：一份临时验证程序 + 实测输出记录。
**约束**：临时工具，验证完毕后删除，不进入生产代码路径、不注册进 ctest 套件。

**构建方式（原稿 g++ 命令已作废——含 3 个不存在的 `-I`）**：
在 `src/` 加**临时 CMake executable target**（复用项目构建体系，链接
`clf_ui`/`clf_core` 既有库——probe 直接链接**生产 `CLFAnsiParser` 与 `CLFTextUtil`**，
M9 裁决：不做解析逻辑复刻），验证完毕后删 target + 源文件。

**取证项**：

| # | 取证项 | 判读标准 |
|---|---|---|
| ① | 原始 ANSI 直出四种候选字形（ANSI Shadow / 紧凑 / 纯 ASCII / 半块 `▀ ▄`） | 字形等宽、等重、无缺字（重点：区块 `█` 与框线 `╗╔═║` 同粗细同基线） |
| ② | 每行 `ftxui::string_width()` | ≤ 阈值（**由本步骤实测回填** §4.2 `kBlockLogoMinWidth`；块字实测渲染宽度 = ② + ②b 共同定） |
| ②b | **实机单格渲染**（M11） | 块字行后紧跟 `\|<--N-->\|` 列标记同屏输出，确认标记落在第 N 列而非 2N 列；终端"歧义宽度字符"设置为窄 |
| ③ | 亮青(96)+加粗可辨识度 | **先断言 `CLFAnsi::isEnabled()`**（M5：stdout 重定向时静默失败 → 着色路径不执行，结论作废），且**在真控制台前台运行**；并排"亮青加粗/亮青常规/灰常规/灰加粗" |
| ④ | 嵌套双序等价 | 用**生产 parser** 解析 `cyan(bold(x))` 与 `bold(cyan(x))` 分段表，证实 §2.3 |
| ⑤ | 经 FTXUI 渲染复现 | 与生产同路径（parse → `color`/`bold` → `hbox` → `Screen::ToString`） |
| ⑥ | 前导空格保真 | 块字左侧缩进 1–2 空格在 FTXUI `hbox` 中是否被吞（审查报告额外发现 1：`Text::Render` 不过滤空白，可由读代码先行排除，仍留实机确认） |
| ⑦ | ASCII 变体表格吞噬防御 | 程序断言：**每行首个非空白字符不得为 `\|`**（F0b——否则被表格检测吞掉重排） |
| ⑧ | 宽度算术预判 | 块字行 `string_width()`（②）≤ 80 即证 b 路线后 80 列不折行（折行判据 = renderDisplayWidth ≤ wrapW，算术等价）；**生产折行复现由 qa 承担**（步骤 5 `wrapLines` 用例——长期有效证伪点，优于一次性 probe 复刻） |

**回填要求**：①/②/②b/③ 的实际输出回填至 §4.2（块字阈值）与 §4.4（字体结论），
作为**块字 vs ASCII 的最终裁定依据**。②b 若显示 2 格渲染 → **整体降级路线 (a)**。

### 步骤 1 `CLFTextUtil` 渲染口径工具

新增（`CLFTextUtil.hpp/.cpp`；**不动** `charWidth/displayWidth/substrByWidth` 本体——qa 钉子全保）：

```cpp
// ============ 渲染口径折行工具（2026-09-22 启动横幅批次）============
// 与 renderCharWidth 同表（FTXUI g_full_width_characters/wcwidth）：⎿/●/❯/块字等
// "非宽多字节符号"计 1 宽——charWidth 口径（多字节恒 2）对这类符号多算 1 宽，
// 折行点提前（含 ⎿ 长行折行位置错误的既有缺陷）。折行/选区换算统一为渲染口径
// （colToByte 已用 renderCharWidth）。displayWidth/substrByWidth 保持 charWidth
// 口径不动（qa 钉子：❯ 计 2 等既有语义）。
static int renderDisplayWidth(const std::string& s);
static std::string renderSubstrByWidth(const std::string& s, int maxW);
// 折行切分：按渲染宽度切成 ≤wrapW 的段序列（CLFReplView 折行 + probe + qa 三用）。
// 空段防御：宽度不足容纳单字符时返回单字节段（与 CLFReplView:193 fallback 同语义）。
static std::vector<std::string> wrapLines(const std::string& s, int wrapW);
```

实现要点：`skipAnsiEscape`（匿名命名空间既有 helper）复用；`renderSubstrByWidth`
用 `renderCharWidth` + `utf8CharLen` 推进（不劈半多字节）；`wrapLines` 循环
`renderSubstrByWidth`，空段 fallback 单字节。

**顺手批**：修正 [CLFTextUtil.hpp:43](../../../src/CLFTypes/CLFTextUtil.hpp#L43)
错误注释（"maxW<=0 返回原串"——实际实现首字符即返回空串）；
hpp:52-54 的"渲染视觉不受影响"注记补一句"折行路径已切渲染口径（2026-09-22）"。

### 步骤 2 `CLFReplView` 折行切换渲染口径

- **开工前 grep**：`qa_CLFReplView` / `qa_CLFSelectionModel` 既有折行断言——
  若存在按 charWidth 口径的折行点断言，属**有意变更**（口径切换），同步更新断言
  并记录；若无则零额外影响
- [CLFReplView.cpp:186-196](../../../src/CLFUI/CLFReplView.cpp#L186-L196)：
  `Sel::displayWidth` → `CLFTextUtil::renderDisplayWidth`；
  折行循环 → `for (const auto& part : CLFTextUtil::wrapLines(l, wrapW)) addRow(...)`
- pendingLine 切分（:203 附近）同步 `wrapLines`
- grep `CLFSelectionModel::displayWidth/substrByWidth` 剩余调用点：零调用则保留转发
  （零破坏，qa_CLFSelectionModel 若有转发断言不受影响）；`flushTable` 表格路径
  **不动**（含 ⎿ 单元格多补 1 空格，视觉无害且属既有行为）
- 预期影响：仅含非宽多字节符号（⎿/●/❯ 等）的长行折行点右移；中文行零变化
  （中文在宽表 → 渲染口径仍计 2）；折行与选区 colToByte 口径统一

### 步骤 3 `printBanner()` 重构

按 §4.5 行序规格实施：

- 块字 6 行常量（`CLFRepl.cpp` 匿名命名空间 `static const char*` 数组——单消费点，
  不新建文件）；每行独立 `cyanLight(bold(line))` 包装 + `emitContent` + `"\n"`
- tagline 行：`" CLI Agent Framework for Code"` + `" "` + `gray(readVersionFile())`
  ——**版本单空格紧随**（用户验收修订 2026-09-22，见 §4.2/§4.5 行 7：
  原右对齐方案在宽终端下有 80+ 列空白、版本孤悬远端视觉断开，已废弃）
- 空行 `emitContent("\n")`；环境层 2 行（路径 gray、skills gray，均带 `⎿` 前缀）
- 窄终端降级：`int w = getTerminalWidth(); if (w < 0 || w >= kBlockLogoMinWidth) 渲染块字`
  ——注意条件方向：**`w < 0`（取不到尺寸）时渲染块字**，因 `wrapW` 兜底为 78 > 58 不折行
  （与"`w > 0 && w < 62` 时跳过"等价，实施取前者）
- 保持：`if (m_output)` 早退守卫（M3-a）、`loadFromDir` 调用（M3-b）、`enableAnsi`
  时序（:135 早于 :168）
- 删除：诊断行（:283）、配置行（:287）、模型行（:289）

### 步骤 4 版本访问器抽取

按 §5.1 契约：`CLFConfigLoader::readVersionFile()` + 两调用点改造（输出逐字保持）。

### 步骤 5 qa 扩展 + 回归

- `qa_CLFTextUtil` 新增用例：
  - `renderDisplayWidth`：ASCII/CJK/⎿（1 宽 vs charWidth 口径 2）差异
  - `renderSubstrByWidth`：不劈半多字节 + ⎿ 行切分点正确
  - `wrapLines`：块字行 80 列不折 / 40 列折两段 / 空段 fallback / ANSI 跳过
- `qa_CLFAnsiParser` 新增（Q3 约定固化）：
  - 双序等价：`cyan(bold(x))` ≡ `bold(cyan(x))`（单段包裹分段表相同）
  - 跨段拼接语义：`bold(cyan(A)+gray(B))` → B 丢 bold（钉"与真实终端一致"现有行为）
- 回归：**全量 ctest 全绿**（既有 qa 钉子 `charWidth(0xE2)==2` 必须保持绿）
- `--help` / `--version` 冒烟：输出与改前逐字节一致

### 步骤 6 实机验收 + 收尾

- §8 验收全项 + 步骤 0 probe 产物删除 + 设计文档归档（`设计/归档/`）

---

## 8. 验收标准

1. 启动实机：块字呈品牌主色（亮青加粗）、tagline + 版本同行、空行分隔、环境层 2 行完整路径 + skills
2. 块字每行 `ftxui::string_width()` ≤ 块字阈值（步骤 0 回填；当前样例 58）
3. **80 列终端块字不折行**（`wrapLines` 断言 + 实机）
4. 窄终端（`w > 0 && w < 块字阈值`）降级：无块字、tagline + 环境层完整、不折行
5. `--help` / `--version` 输出与改前逐字节一致（**`VERSION` 正常可读前提**；打不开分支两处行为保持现状 = 已知差异，F3/M4）
6. ctest 全绿（新增用例 + 既有 qa 钉子全保）
7. 步骤 0 的 probe 产物已删除，仓库无残留
8. 横幅总高度 ≤ 10 行（6 块字 + tagline + 空行 + 2 环境层；M6）

---

## 9. 已知缺口

1. **块字字符集在用户实际终端/字体下的表现**（§4.4）——步骤 0 解决（前置门）
2. `emitContent` 对裸 `\n` 之外控制字符的处理边界（`\r` 已取证被跳过，[CLFTerminal.cpp:137](../../../src/CLFUI/CLFTerminal.cpp#L137)；余未穷举）——本批次不涉及，维持现状
3. `CLFSelectionModel::displayWidth/substrByWidth` 转发切换后可能零调用——步骤 2 grep 后决定去留（保守：保留）
4. CMake 第三版本源 `project(CLFCode VERSION 0.1.0)`（L6）——顺手批候选，确认无消费点后同步
5. 块字降级阈值 `kBlockLogoMinWidth`——步骤 0 实测回填

---

## 10. 实施记录（pro，2026-09-22）

**步骤 0 实机验证 ✅**：probe（临时 CMake target，链接生产 CLFAnsiParser/CLFAnsi/FTXUI）
全断言通过——四变体无缺字、块字 1 格渲染（M11 未命中）、双序等价生产链路实证、
前导空格保真、ASCII 兜底无 `|` 开头行、最宽 58 ≤ 80；M5 防御实锤（重定向环境
exit=2 正确拒绝）；块字阈值 62 已回填 §4.2。

**步骤 1-5 实施 ✅**：CLFTextUtil 新增 renderDisplayWidth/renderSubstrByWidth/wrapLines
（qa 钉子全保）+ CLFReplView 折行切渲染口径 + printBanner 重构 + readVersionFile
三态契约 + qa 新增 5 用例。**ctest 36/36 全绿** + `--version`/`--help` 冒烟逐字一致。

**实施期实抓 3 处（就地纠错）**：

1. **折叠块展开折行是第三处**——定稿步骤 2 只列了主内容 + pendingLine，
   `CLFReplView.cpp:242-246`（FoldLine 折行）同批切换渲染口径，与主内容口径统一
2. **qa 静态期陷阱（用户实抓 Debug 弹窗 exit=3）**：qa_CLFAnsiParser 新用例调生产
   `CLFAnsi` 包装 → 静态初始化期 `s_enabled` 恒 false → 包装退化裸串 → 跨段用例
   `segs[1]` 越界。修法沿用项目惯例：qa 写**字面转义序列**（包装展开形态）；
   probe 运行时验证已走真机生产链路（memory qa-static-init-pitfall 同族）
3. **用户验收修订**：版本号右对齐 → **单空格紧随 tagline**（宽终端下 80+ 列空白
   视觉断开）；填充/clamp 逻辑整体删除（§4.2/§4.5 已同步）

**用户实机验收 ✅（2026-09-22）**：块字品牌色/层级分离/环境层降权显示正常；
版本紧随修订后复验通过。

---

## 附：本次未采纳方案

| 方案 | 未采纳原因 |
|---|---|
| 路线 (a) 纯 ASCII 字形 | 放弃块字视觉冲击力（用户拍板方向）；**保留为步骤 0 失败后的降级兜底** |
| 路线 (c) 免折行短路 | 表方案——窄终端视觉溢出，违背修根不修表；且 `substrByWidth(0)` 陷阱（hpp:43 错注释） |
| parser 栈恢复修根（原 §5.4） | F2：修解析器解决不了生成侧问题；F0d：全仓无现存可复现故障；变更面扩至渲染核心 |
| `CLFAnsi` 部分复位（22/39 替代全清 0） | 立约定已足够（包裹内不拼接多段）；改生成侧影响所有既有调用点序列 |
| B 轻量框 / C 终端窗口梗 | 用户选定块字；C 行数最多，与信息块视觉重量冲突 |
| 打字机 / 渐显动效 | 用户选定静态；且需接入 FTXUI 渲染循环，改动 `m_output` 发射时序 |
| Logo 外置 `config/logo.txt` | 引入运行时 I/O 与缺文件降级路径，当前无主题化需求（YAGNI） |
| 修 `printBanner` 时顺手改 `--help` | `--help` 须保持可管道化纯文本 |
| 256 色 / 真彩渐变 | 渲染层不识别，需扩 parser 与 `CLFAnsi` 两层 |
| tagline 前缀 `▸` | `▸`(U+25B8) 宽度未取证（审查报告额外发现 3）；定稿改用无前缀 2 空格缩进 |
