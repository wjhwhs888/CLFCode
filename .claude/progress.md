# CLFCode 任务进度

## 进行中

### M1 传输层（CLFJsonRpcClient）— 待启动
- 依据：spike go 决策（`tools/spike/Spike报告.md`）+ 设计文档 M1 章节
- 范围：`src/CLFBackend/CLFJsonRpcClient`（spawn / 行帧 reader / 三分类路由 / waiter 表 / close 阶梯）+ 单测（fake runtime 回放 `tools/spike/frames/norm/*.norm.jsonl`）
- 对译蓝本：`tools/spike/spike_driver.mjs` 五模块（每函数头部有 M1 映射注释）
- 必读协议事实（Spike报告 §一）：事件先于响应（receipt 门控须缓冲回溯）/ sessionId 每次新 id / assistant/message 在 data.message.content / 双 finish 枚举 / tool-call 参数为 JSON 字符串
- 估时：2-3 天

## 已完成

### 2026-08-17 复制粘贴功能修改 ✅（验收通过，关闭）
- 分析/设计：`.claude/plans/分析/分析-复制粘贴功能修改.md` + `.claude/plans/设计/设计-复制粘贴功能修改.md`（Flash 四轮 12 条意见 + 终审 6 缺口全消化）
- 实现：CLFPasteCoalescer（粘贴事件突发合并，P1-P10 全过）+ CLFSelectionModel（选区状态机/提取，S1-S8 全过）+ 渲染器 RowMap 并行构建与高亮；ctest 基线 10/11（SessionManager 既有环境失败不变）
- **验收收敛定稿（用户决策）**：选区 = 纯鼠标左键拖选 + 松手自动复制（copy-on-select）；移除 Ctrl+S 键盘选区与 Ctrl+C/Enter 复制；Ctrl+C 空闲忽略（原误触即退出）、busy 中断保留
- **验收期根因修复 5 项**（事件日志取证实证）：① cv 谓词缺陷（wait→wait_until）② 鼠标坐标 0 基（WT/ConPTY 投递 0 基，FTXUI Box 对照）③ **ENABLE_PROCESSED_INPUT 未清**（FTXUI 不清理，Ctrl+C 被系统转信号、SIGINT 处理器直接退主循环——事件永远到不了应用层）④ 拖选末字符丢失（colToByteEnd 含入 + 松手补位）⑤ hitTest 下方 clamp 缺陷（输入框点击误判为选区）
- 顺带根因修复 qa_CLFSecurityPolicy 测试缺陷（const char* 指针比较 → std::string）
- 已知边界：粘贴源若为终端原生复制（Shift+拖选）会带渲染网格填充空格——用应用内拖选复制作源；超大粘贴分批（>40ms 批间隔）可能整段自动提交（设计 §2.1）
- 取证模式保留：`CLF_DEBUG_EVENTS=1` → `doc/log/clf_events.log`（独立追加）

### 2026-08-16 dsh 后端接入 Spike S1-S5 全部完成 ✅（go，M1 立项）
- 产出：`tools/spike/`（Spike报告.md + spike_driver.mjs 五模块 + cordis-smoke/final.yml + frames raw/norm 8 组，自包含）
- P1-P4 全过：全链路 8 轮跑通（流式/reasoning/usage/shutdown/exit 0）；工具面五类实测可用（fs 写不受沙箱约束、pwsh 写被拒+升级无审批服务）；subagent 四断言全过（父子会话隔离 731/644）；frames 双轨就绪
- 关键协议事实（M1/M2 必读）：事件先于响应（receipt 门控须缓冲回溯）/ sessionId 复用碰撞 / assistant/message 在 data.message.content / 双 finish 枚举 / tool-call 参数为 JSON 字符串 / spliced 带 removedCount
- 回填分析文档 #1/#4/#5（确认链 UX：审批请求不进 JSON-RPC 协议）
- 决策点 3 实测输入齐备：read-only 只约束 pwsh 通道、升级需装配审批服务

### 2026-08-16 dsh 后端接入 Spike S0 启动冒烟 ✅（四项全过）
- 产出：`tools/spike/cordis-smoke.yml`（零 !!js + 修正装配 + junction node_modules；后已移至 tools/）
- 四项清单全过：缺配置 exit(1) / 畸形行静默跳过 / 20 插件全树加载 / 持开 stdin 存活 10s stderr 零行
- 执行中抓出草案缺陷 3+4：pwsh-local 与 pwsh-sandbox 的 ctx.shell 服务冲突（只挂 sandbox 版即可，三件套实为两件）；缺 dsh-shell-env（tool-pwsh 挂起不激活）
- 部署事实实测：bare 包名自配置文件目录向上解析（"configuration project" 语义）→ M2/M3 部署时 cordis.yml 须与 runtime 闭包同目录；npm 发布版 0.0.1-rc.5 ≠ 钉住 0.1.0-rc.5（M3 核实项）
- 插曲：首轮 loader 失败一度怀疑 dsh web（:3080）并发干扰，隔离测试排除——根因是配置目录解析（bare 包名），与并发无关

### 2026-08-14 P2-UI M2+M3（审批卡强化 + usage 打通） ✅
- P2-2 审批卡：splitPrompt 纯函数 + headline 琥珀加粗/参数 dim 分层 + 确认结束清 prompt 防残影
- P2-4 usage 打通：同步解析 + 流式 `stream_options.include_usage` + **feedUsage 独立投喂**（usage chunk 的 choices 为空数组，须在 lambda 的 choices 过滤前提取——首版把提取放 feedDelta 被过滤挡死，用户验收当场发现，T10c 集成测试钉死）
- summary 行追加 `· X.Xk tok`（缺失省略）+ /context "本次会话累计"（条形仪表早已存在）
- 落定规则 R3：仅正常解析路径累计，中断/错误不累计（T10b）
- 测试：T10×5 + T10a/b/c + T11；ctest 8/9（SessionManager 既有环境失败不变）
- 人工验收：summary tok 计数 / context 累计 全部通过
- ⚠ 随机崩溃观察：19:16 交互式流式中崩溃一次（M3 代码在流式中无执行路径，疑似 08-11 长期观察 bug 自然复现）；已开启 HKCU LocalDumps 全量转储 + debug 日志待复现

### 2026-08-14 P2-UI M1（恢复回显折叠 + 时间戳） ✅
- 依据：《设计-P2-UI展示完善.md》（两轮外部审查 R1-R5 定稿）
- P2-1 恢复回显折叠：ICLFOutput 增 showFoldedBlock（默认空实现）/ CLFTerminal 折叠态 / Ctrl+R 切换 / restoreSession 改走折叠路径
- R5 视口保持：CLFScrollView::keepLineVisible（offset 计算），切换后折叠行不被顶出
- P2-3 时间戳：localDateStamp/localTimeStamp 双平台 helper + 用户消息行尾 HH:mm（跨日带日期）
- 顺带修复整体审查 P2-8：get_current_time POSIX 分支（localtime_r）
- 测试：T7（折叠回显不进滚动区）+ T9a/T9b（时间戳格式）；ctest 8/9（SessionManager 既有环境失败不变）
- 人工验收：折叠行/展开/时间戳/get_current_time 全部通过

### 2026-08-14 UI 信息展示借鉴 M2 ✅
- P1-1 四态状态点：接线全表（Running/Done/Warn/Error×7 + Repl catch 兜底 + /resume /clear 的 None）+ 渲染（running=蓝动画帧、done=绿●、warn=琥珀●、error=红✕）+ 计时 ≥15s 才显示
- D1 色语义落地：analyze 模式改紫，蓝让给 running
- P1-2 summary 增强："N 工具 (read A · search B · edited C)"——顺手修 search 双计数（原逻辑 search 同时计入 read 桶，T5 测试当场抓住）
- P1-3 思考折叠摘要：执行中=实时尾行、完成=首行（UTF-8 安全截断）+ Ctrl+O 注释修正
- 清理 useProgressive 同作用域遮蔽警告（C4456）
- 测试：T4a/T4b（含 F20 不被覆盖断言）+ qa_CLFToolExecutor 新套件（T5/F10/降噪保持）；ctest 8/9（SessionManager 既有环境失败不变）
- 人工验收：状态点四态 / 动画 / 15s 计时 / 折叠摘要 / analyze 紫 全部通过

### 2026-08-14 UI 信息展示借鉴 M1 ✅
- 依据：`设计/设计-UI信息展示借鉴.md`（四轮审查定稿 F1-F21）+ `分析/分析-UI信息展示借鉴.md`（dsh 展示设计对照）
- P0-1 错误首行摘要（emitError 单点收敛 + UTF-8 安全截断）
- P0-2 head/tail 截断：search_content 环形缓冲（head 240 + tail 240）+ renderDiff 16+16（公共 headTailCapWithMarker）
- P0-4 工具执行中单行 + 旋转动画（事件驱动无新线程）+ 读工具失败可见性（F10）
- P0-5 中断消息收敛 9 处→1 helper（文案统一 + clearThinking + Warn）
- F13 潜伏缺陷修复：ICLFOutput::requestRefresh + turnTimer 1Hz 驱动（工具执行期界面冻结）
- 测试：qa_CLFHeadTail（8 用例 59 断言）+ qa_CLFSearchContent（3 用例 311 断言）+ T6 三时点中断；ctest 7/8（SessionManager 既有环境失败，HEAD 对照实验确认与改动无关）
- 人工验收：执行中单行+动画 / 中断后显示 / 搜索截断标记触发（agent 自主 PowerShell 补全验证截断可感知）

### 2026-08-12 System Prompt 优化 ✅
- 设计文档：[归档-SystemPrompt优化](../../.claude/plans/设计/归档/归档-SystemPrompt优化.md)
- CLFSystemPromptBuilder：模板加载（降级默认）/ L1 宪法 mtime 缓存 / Git TTL 30s 惰性刷新 / 项目规则加载 / token 预算
- CLFContext::setSystemPrompt()（去重）+ removeSystemMessages()
- CLFAgentLoop::injectSystemPrompt() → Builder，injectSkillToContext() → 重建模式
- config/system_prompt_template.md 可编辑模板

### 2026-08-12 /init 项目初始化命令 ✅
- `/init` → 在工作目录创建 PROJECTRULES.md 模板（已有则不覆盖）
- 模板含 6 个区块，128 行限制提示

### 2026-08-12 P0 第一批 CLI 参数 + 非交互模式 + search_content ✅
- CLI 参数解析：`--help`/`--version`/`--prompt`/`--prompt-file`/`--allow-write`/`--config`/`--project-root`
- 非交互模式：`--prompt` 直接执行后退出，安全策略 Analyze（block 写），`--allow-write` 提升为 Auto
- `search_content` 工具：纯文本匹配/跳过 ignore 目录/1MB 上限/扩展名过滤/500 行截断
- bugfix: `ProgressGuard` 析构 null 检查 + `config.m_stream=false` 非交互模式

### 2026-08-12 v0.1.6 发布 ✅
- `/version` 命令 + `/help` 字母序排列
- install.ps1 本地版本检测（已是最新则跳过）
- 发布包新增 VERSION 文件，使用说明重写
- tag: v0.1.6，release: CLFCode-v0.1.6-win64.zip (12M)

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

## 长期观察

- **首次运行崩溃修复（2026-08-11）**：等待自然复现后根据日志定位根因
  - 2026-08-14 19:16 交互式流式中崩溃一次（iter=1 流式期间，无工具运行；非交互模式同提示完整通过；M3 代码在流式中无执行路径，疑似本 bug 自然复现）
  - 取证已武装：HKCU LocalDumps 全量转储（`doc/debug/`）+ 日志 debug 级（`agent_settings.local.json`），复现后分析 dump 定位根因

## 已知问题

- **install.ps1 版本检测闪退** ✅ 已修复（`exit 0` → `return`）
- Ctrl+C 确认栏退出（低优先，暂缓）
- emitRaw 钩子（设计预留）
