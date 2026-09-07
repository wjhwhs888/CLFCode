# 鸿蒙 App 开发 · 用 CLFCode 驱动 —— 初步设计 v0.1

> **性质**：指导性文件（可独立拷贝带走）。用途 ① 作为用 CLFCode 开发鸿蒙 App 的操作指导；② 顺带在完整开发流程上验证 CLFCode 工具能力是否有异常（异常回流 CLFCode 项目）。
> **日期**：2026-09-03 ｜ **作者**：设计（人）+ AI 协作
> **目标读者**：将来的自己 / 接手的 agent
> **背景**：C++/CLI 出身，不熟移动开发；目标 = 最小成本跑通"从零到模拟器/真机可用 hap"，并摸清 CLFCode 在其中的能力边界。

---

## 一、目标与验收

- **目标**：CLFCode 端到端驱动一个鸿蒙 App 的最小完整流程（工程接手 → 写码 → 构建 → 测试 → 出包），人工只守"画面验收"和"签名/发布"闸门
- **验收物**：
  1. 一个可安装到模拟器（理想含真机）的 hap
  2. 本文 §五 能力验证表填完，异常按 §七 分类
  3. 结论：CLFCode 驱动鸿蒙开发的可行性判断

---

## 二、前提认知（先读，避免走弯路）

1. **移动 app 没有"本地运行"**：代码必须装进"运行环境"。体验阶梯三级：**Previewer**（IDE 内单页预览，零设备）→ **模拟器**（完整 App，不用买手机）→ **真机**（手感验收，最后才做）。**不是必须真机**。
2. **构建 ≠ 可分发**：桌面发 zip 即可；移动要 打包(hap) → 签名 → 装环境；上架还要过商店。
3. **签名是硬门禁**：无签名包系统拒绝安装。DevEco 登录华为账号后**自动签名**（调试期），发布要另配发布证书。
4. **CLFCode 是终端 agent**：只能驱动"命令行闭环"（写文件/跑命令/查文档）；IDE 图形操作（Previewer 画面、模拟器点按）= 人工。
5. **鸿蒙不只有 ArkTS**：UI 层 ArkTS（必学），**逻辑层可 native C++（NAPI）**——C++ 能力可复用。
6. **CLI 工具名**（CLFCode 会用到）：
   - `hvigorw`：构建/打包（工程根的 hvigorw.bat）
   - `ohpm`：包管理（装依赖）
   - `hdc`：设备连接（装包/日志/文件）
   - `ohosTest` + **Hypium**：官方单元测试框架

---

## 三、环境准备（一次性人工，约半天——CLFCode 不碰）

| # | 步骤 | 人力 | 说明 |
|---|---|---|---|
| 0 | 注册华为开发者账号 | 人工（网页） | 模拟器镜像 + 自动签名都要登录 |
| 1 | 安装 DevEco Studio（Windows） | 人工 | 下载几个 GB 是大头 |
| 2 | 首次启动装 HarmonyOS SDK + 工具链 | 人工 | hvigor/ohpm/hdc 随 IDE 带 |
| 3 | 登录账号 → 自动签名配置 | 半自动 | 之后签名自动 |
| 4 | Device Manager 创建模拟器 + 下载系统镜像 | 人工 | 网络大头 |
| 5 | **建 Empty Ability 空工程 → 模拟器跑通一次** | **人工（关键）** | 生成工程骨架/签名态/本地缓存预热；此后 CLFCode 才接手 |
| 6 | 记录环境事实 | 写笔记 | 见 §四 头部清单 |

> ⚠ 步骤 5 是分水岭：**空工程跑通前 CLFCode 帮不上忙**（IDE 向导 + 首次签名 + 镜像都是图形/账号流程）；跑通后才有命令行闭环可驱动。

---

## 四、CLFCode 驱动开发循环（核心设计）

### 4.1 环境事实清单（步骤 6 记录后贴这里）

```
工程根路径：
hvigorw 路径（工程根 .\hvigorw.bat？）：
SDK 路径：
构建命令验证：hvigorw assembleHap（或 assembleApp）
装包命令：hdc install <hap路径>
单测命令：hvigorw test ？（ohosTest 执行方式）
ohpm 可用性：
```

### 4.2 闭环

```
CLFCode（在工程根工作）
  ├─ 理解：listDirectory/read 读工程树、配置文件、已有代码
  ├─ 写码：write_file/edit_file 写 ArkTS/ArkUI（必要时 C++/NAPI 逻辑层）
  ├─ 构建：跑 hvigorw → 读编译错 → 迭代（★硬反馈闭环，CLFCode 自愈）
  ├─ 依赖：ohpm install（如需要）
  ├─ 查 API：web_fetch developer.huawei.com（★见风险 R1）
  ├─ 测试：ohosTest/Hypium 命令行执行 → 读结果
  └─ 出包：assembleHap 产物 → 交人工
人工闸门：Previewer 看画面 / 模拟器跑交互 / 真机手感 / 发布
```

---

## 五、CLFCode 能力验证表（逐项勾记，异常记 §七）

> 用法：每项执行后填"结果"列；异常 = 该能力在鸿蒙流程中表现不符预期。

| # | 流程步骤 | 依赖能力 | 验证点 | 结果/异常 |
|---|---|---|---|---|
| V1 | 接手工程 | read + listDirectory | 大目录列举性能；`.hvigor`/`oh_modules` 缓存目录是否拖慢/误读；中文路径 | |
| V2 | 理解工程 | read + grep(搜索) | 配置文件/ets 树结构阅读；search_content 定位符号 | |
| V3 | 写 ArkTS | write_file/edit_file | 中文注释 UTF-8；大文件 edit；新建多文件 | |
| V4 | 命令行构建 | pwsh/命令执行 | 首次构建长耗时（超时？）；长输出截断（编译错能否看清）；非零退出码解读 | |
| V5 | 依赖安装 | 命令执行 | ohpm 网络操作；输出解析 | |
| V6 | API 文档查询 | web_fetch | **官方文档 JS 渲染页能否抓到正文**（预检：FAQ 页实测正文空——见 R1） | |
| V7 | 单元测试 | 命令执行 | hypium/ohosTest 报告解析 | |
| V8 | 多轮迭代任务 | todo + 会话 jsonl + resume | 任务清单驱动；长会话上下文；中断后续跑 | |
| V9 | 装包指令 | 命令执行 | hdc list targets / install 指令生成 | |
| V10 | 非交互模式 | --prompt | 可否一键"构建并汇报"（无 UI 驱动） | |

---

## 六、最小验证实验剧本（先跑这个，再扩展）

**目标**：Empty Ability 工程 → 加一个可交互页面 → 构建过 → 单测过 → 出 hap → 人工模拟器验收。

1. （人工已做 §三-5）空工程在模拟器跑通
2. CLFCode 读工程树（V1/V2）
3. CLFCode 改 `Index.ets`：加一个状态变量 + 按钮交互（V3）
4. CLFCode 跑 `hvigorw assembleHap`，读错迭代直到过（V4）——**此步是核心验证：编译错闭环**
5. 加一个 Hypium 单测用例，命令行执行（V7）
6. 定位 hap 产物路径，给人工 `hdc install` 指令（V9）
7. 人工：模拟器安装 → 点按验收 → 反馈（是否要调 UI）
8. 记录全程能力表现

---

## 七、异常记录与回流约定

| 异常编号 | 步骤 | 现象 | 根因初判 | 分类 | 处理 |
|---|---|---|---|---|---|
| | | | | A=CLFCode 缺陷 / B=用法问题 / C=环境限制 / D=模型知识 | 回流 CLFCode P 项 / 调用法 / 接受 |

**预填风险（已知，先到先得）**：

- **R1 web_fetch 抓官方文档**：developer.huawei.com 为 JS 渲染，实测抓 FAQ 正文为空 → 备选：华为云博客/掘金等静态页、或人肉贴关键文档片段给 CLFCode。**若 R1 成立，模型生态知识不足的补偿通道打折，ArkTS 写码质量风险上升**——实验中重点观察
- **R2 构建长输出截断**：首次构建日志可能超 CLFCode 输出截断阈值 → 需要 `2>&1 | 重定向到文件` 再 read 的方式绕过（用法问题）
- **R3 命令超时**：首次构建/镜像相关命令慢 → 调大超时或后台跑
- **R4 中文路径/编码**：工程若在中文路径，注意 CLFCode 已知的 u8path 纪律是否延伸到 pwsh 命令
- **R5 模型对 ArkTS 的熟悉度**：比 Android/Web 弱；策略 = 让 CLFCode 先 web 查证再写，或人工提供官方示例片段

---

## 八、验收与结论输出

1. hap 可装进模拟器（§六跑通）
2. V1-V10 填完，异常按 §七 分类完毕
3. 输出一段结论：CLFCode 驱动鸿蒙开发 → 可行性 / 最大瓶颈（预计 R1 文档抓取 或 UI 验收）/ 建议的长期分工
4. 本文件拷贝带走；异常若有 A 类（CLFCode 缺陷）→ 回流 CLFCode 项目问题清单

---

## 附：参考链接

- 模拟器/真机/预览器差异（官方 FAQ）：https://developer.huawei.com/consumer/cn/doc/doccenter-tools-faq/faqs-simulator-5
- 命令行/流水线构建安装（官方 FAQ）：https://developer.huawei.com/consumer/cn/doc/doccenter-tools-faq/faqs-command-line-tool-35
- 签名（官方）：https://developer.huawei.com/consumer/cn/doc/harmonyos-guides-V5/ide-signing-V5
- Hypium 测试框架（官方）：https://developer.huawei.com/consumer/cn/market/prod-detail/25345c6221a343dabf37cde528ea76e8/PLATFORM
- Previewer 限制 FAQ：https://developer.huawei.com/consumer/cn/doc/harmonyos-faqs/faqs-previewer-operating-7
