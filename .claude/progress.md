# CLFCode 任务进度

## 进行中

> **阶段划分（以"是否开始接入 dsh"为界）**：**A 阶段 = 本体自研**（CLFCode 自己的功能）✅ **全部完成（v0.5.0 发布中）** → 🚦**决策门**（唯一问题：subagent 值不值）→ **B 阶段 = dsh 对接**（8.5-12.5 天）。
> A 阶段产出在 B 阶段**不会白做**——双后端并存，直连后端永远是降级兜底路径。

### 【A 阶段】本体自研 — ✅ 全部完成（v0.5.0，2026-09-02 tag 已打，发布由用户执行）
- ~~S1 小修~~ ✅（v0.3.5）｜~~S2 安全+工具~~ ✅（v0.4.0）｜~~【插入批】todo 面板 + jsonl 追加式保存~~ ✅｜~~S3 净增点自研~~ ✅——详见下方已完成区
- **A4**（=S4，按需穿插）：配置校验 / session 版本分流 / 宽字符 / 多会话 / `/reload` / 信号 / 并发锁 / git 工具 / list_directory 增强 / **ask_user（N 选项确认栏 — 若确定走 B 阶段，建议提前到此做，B 阶段的 dsh 确认链可复用同一套 UI）**
- **A5【当前优先】工具调用循环上限机制改造（2026-09-02 二轮拍板定稿，已排期）**：设计文档 `设计/设计-工具调用循环上限机制改造.md`（flash 草案 → pro 定稿）。解决三问题：触顶 `[Error] Exceeded` 报错语义 / 工具无法声明回合结束 / 模型长时间无响应无提示。四个机制：A concludesTurn（静态声明、机制先行、本期零工具声明）B 触顶优雅收尾（触顶收尾请求——同步 + 独立 try/catch；默认值 16→48 用户确认）C max-tokens Warn（P2 小改）D Tips 行（config/tips.txt + 硬编码兜底；异常态本期仅静默计时）。收尾路径统一：完成分支 = 唯一汇聚点（自然停/concluded/max-tokens），触顶路径独立收敛（不做 todo 清单收尾）。**排期：阶段一 T1-T3+测试（A/B/C 机制，~1-1.5 天）→ 阶段二 T4-T6+测试（Tips 行，~1 天）→ 验证（干净重建 + ctest + 主程序冒烟）**。插队最前，不影响其他进度

### 【B 阶段】dsh 对接 — ⏸ 决策门暂缓（2026-09-02 用户定：慎重、不着急）
- **用户态度（2026-09-02）**：进入 dsh 前先慎重考虑；dsh 当前为 **alpha 版本、不稳定，不着急**——决策门保持挂起，暂不投入 B 阶段
- **A 阶段产出完毕**：S1/S2/S3 + 插入批（todo 面板 + jsonl）全部落地并发布 v0.5.0——dsh 的净增点已收敛为**仅剩 subagent**
- **过渡期选项**（决策门挂起期间的按需工作）：A4 可选批（配置校验 / 多会话 / /reload / 信号 / 并发锁 / git 工具 / list_directory 增强 / ask_user 确认栏）；或**自研轻量 subagent**（进程内嵌套 CLFAgentLoop 2-3 天，不依赖外部后端——若用户想先要 subagent 能力，这是更稳的路线）
- **决策门复盘素材（存档备查，下次评估时直接引用）**：
  - B0 环境重建 0.5 天 → B1(=M1) 传输层 3-4 天 → B2(=M2) 会话层 2-3 天 → B3(=M3) 收尾 3-5 天 = 全程 8.5-12.5 天
  - Spike S0-S5 全过（go 决策），素材齐备：`tools/spike/`（报告 + spike_driver.mjs 五模块 + frames/norm 12 组 fixture）
  - 2026-08-25 上游核实：仓库 `E:\deepseek-harness`；上游 `b150a55`(0.1.1-rc.2)；协议面几乎未动 → fixture 仍有效；platforms.json 仍无 Windows → Node 闭包唯一路径
  - 若走 → 先做 M1（CLFJsonRpcClient 与 MCP 传输同构，价值独立于决策）；若不走 → 自研轻量 subagent

## 已完成

### 2026-09-02 S3 净增点自研 ✅（未发布——攒入下一版本）

- **S3-1 上下文摘要**：`shouldSummarize` 阈值判定（剩余窗口 < `m_autoSummaryThreshold` 默认 4000）+ 频控（每 10 轮最多一次）+ runTurn 起始自动触发（先于任何 API 调用）：生成（同步 LLM，失败规则降级）→ **追加 jsonl summary 行落盘** → rebuildSystemMessage（摘要经 Builder 段落拼入 `{{project_context}}`——system 永不截断 + 老模板天然兼容）；`compress_context` 工具（Read，模型主动调用同路径 `compressContextNow`）；注入去重 = m_cachedSummary 单值覆盖语义
- **S3-2 /model 切换 + 多模型自适应**：`/model <name>` 运行时切换（不落盘，提示"新会话生效"）；max_tokens **按模型名查表覆盖**（配置 `agent.model_max_tokens` 对象，用户显式声明、程序零猜值，未命中保持全局值）；`include_usage` 按 base_url host 判定（deepseek.com 后缀才发送，其他 provider 防误发——usage 缺失由 R3 保持 0 预期降级）
- 测试：qa_CLFAgentLoop W1-W3 + qa_CLFProtocolAdapter S3-2（include_usage 三态）；ctest 20/20
- 验证：干净重建 161/161 + 双重冒烟（--version + dummy 端点 --prompt 标准输出吻合）
- **A 阶段全部完成** → 🚦 **进入 dsh 决策门**（见下）

### 2026-09-02 todo 面板 + jsonl 追加式保存 ✅（全流程闭环，未发布——攒入下一版本）

- **三批实施（D1-D3）**：T1/T2 m_todos 加锁；J1 codec 行编解码（header/turn/todo_snapshot/complete/summary）；J2 SessionManager 追加/逐行解析/list [当前] 重定义/cleanupOld 适配；J3 AgentLoop 会话上下文（四状态 + 9 接口 + beginSessionFile 懒创建/续写复制 + T6 完成分支收尾 + restoreSession 分流）；J4/J5 轮末追加与命令层（/exit 纯退出、/clear 摘要落盘）；T3 刷新链；T4/T5 常驻面板渲染；J6 resume 行级回显（每轮清单状态行 + complete 收尾行）
- **人工验收 43 项全过**（用户实机两轮：场景 A-F + 补测 exit/clear/强杀/中断；强杀 resume 显示未完成清单 = F20 预期行为实证——07:47 强杀时快照 1✓2✓/3-7 pending，resume 原样重现）
- **验收期修复 4 个 bug**：BUG-1 update 后面板消失（跨轮场景 update 未清面板隐藏标志，dsh projection 语义）；BUG-2 turn 行序列化失败丢失（非法 UTF-8 零容错 → dumpLine 降级 replace + loadJsonl 判损放宽——无 turn 但有快照的崩溃残留不再误判损坏）；收尾清单格式改多行（标识行 + 每任务一行，实机调整）；QA 中途修 3 个测试编码/设计问题
- **实施期真 bug 7 个**：up() 无限递归 SegFault、rename 共享冲突、中文窄路径构造 CP936 陷阱×3、静态非平凡对象（第三次）、sed 误删测试声明
- **设计修订**：审查补丁 §八 6 条（线程模型/四状态归属/API 契约/T6 判定去 m_todoDirty/lifecycle/list 语义）；实施期补丁（header 补 skills 字段、§3.9 契约收行文本）
- 测试增量：qa_CLFMessageCodec L1-L9、qa_CLFSessionManager J1-J11（重写+扩展）、qa_CLFAgentLoop U1/V1-V5、qa_CLFTodoPanel（新套件 P1-P6）、qa_CLFBuiltinTools B4、qa_CLFToolExecutor T12——**ctest 20/20 全绿（历史首次，旧 qa_CLFSessionManager 3 个过时用例已清理）**
- 设计文档已归档：`设计/归档/归档-任务清单UI显示.md`、`设计/归档/归档-会话追加式保存.jsonl.md`；测试记录：`测试/测试-todo面板与jsonl保存-人工验收.md` + `补测二.md`
- **与 S3 衔接**：summary 行类型已预留（S3 摘要自动触发复用同格式）；jsonl 先行反为 S3 铺路
- **待办**：下一版本发布时并入（CHANGELOG 未发布段落 + 版本号）

### 2026-08-31 中文路径全链路编码修复 ✅（两批，已提交推送，**未发布**——攒入下一个版本）
- **第一批（36a33cc）**：状态栏目录显示乱码（"椤圭洰"应为"项目"）→ `CLFRepl.cpp` modeLine 改 `u8path`；`CLFCommands.cpp` /init 项目根、`CLFAgentLoop.cpp` workspaceRoot（模型提示词中的项目路径）改 `.u8string()`。**用户已实测 modeLine 修复生效**
- **第二批（b3a1244，系统性排查）**——`path::string()`/窄字符 path 构造/A 系列 WinAPI 三类编码陷阱全扫：
  - `/init` 项目名 `path(projectRoot)` 窄构造双重乱码 → `u8path(...).filename().u8string()`
  - `CLFSearchContent` 结果相对路径与超限文件路径输出 → `.u8string()`（模型看到的搜索结果路径此前中文乱码）
  - `CLFConfigLoader` `GetModuleFileNameA` → **W 版本**（exe 位于中文目录时配置加载乱码）+ `path(exeDir)` → `u8path(exeDir)` + `dir.string()` → `.u8string()`
  - `CLFSessionManager` 会话列表中文标题 fallback → `.u8string()`；窄字符 `ifstream file(info.m_path)` → `u8path`（中文路径打开失败）
  - `CLFSkillLoader` skills 文件枚举/打开 → `.u8string()` + `u8path`
  - `CLFFileOps::toNativePath` fallback → `u8path`
  - **保留不动**：ASCII 名单比较类 `filename().string()`（后缀匹配乱码无害）、旧版兼容区（findIncomplete/promote/migrate，注释明确"新代码不应使用"，jsonl 方案将重写）
- 根因族：MSVC 窄字符文件系统 API 按 `GetACP()`（CP936）解释 UTF-8 字节——注意 `main.cpp` 的 `SetConsoleCP(CP_UTF8)` **不改 GetACP()**，只影响控制台 I/O
- 验证：MSVC Debug 构建 28/28；ctest 18/19（唯一失败 qa_CLFSessionManager 既有环境问题不变）
- 发布策略：**攒入下一个版本**（CHANGELOG"未发布"段落已更新；VERSION 保持 v0.4.2 不打 tag）

### 2026-08-31 自问自答 P0 Bug 修复 ✅（v0.4.2 已发布，全流程闭环）
- **现象**：用户 16:26 提交后零输入零按键，16:31:49 自动提交（会话 JSON [51] "你猜我咋想的…"），16:02:46 同类（[36]）；长回复时概率高（触发轮 21474 字符）
- **根因**（三重证据 + 用户实证，推翻初稿"上膛残留 5 分钟"推断）：终端注入 → Char 突发进 inputText → 末尾 Return → 40ms 窗满自动提交。核心缺陷 = 40ms 窗只检测"Return 后"不检测"**Return 前字符突发**"→ 单行注入末尾 Return 与手打回车在事件层同构，机制无法区分；「一次 Return+40ms 静默=提交意图」是脆弱假设
- **注入源**：右键粘贴用户同一终端实测排除（两次）；**Shift+Insert 与 Ctrl+V 实测注入生效** → 16:31 最可能为滚轮翻看时误触 Ctrl+V、剪贴板残留草稿（含行尾换行）。精确方式未做事件级确认，但不影响修复（机制级防住所有注入）
- **修复**（用户定案 2026-08-31：粘贴后二次 Enter）：`CLFPasteCoalescer` 前置突发检测——`onCharacter` 刷新 `m_lastCharTime`；`onReturn(Idle)` 判定 `now-lastChar ≤ 40ms` → 置 `m_pendingFromPaste`；定时线程窗满时粘贴上下文 → **不置 confirmed、复位 Idle**（文本留输入框、零自动请求、wakeCb 零触发）；手打回车（间隔>40ms）窗满提交不变。构造加 `pasteBurstMs` 参数（默认 40，测试注入）
- 测试：qa_CLFPasteCoalescer P1-P10 全绿 + 新增 N1-N6（粘贴末尾不提交+wakeCb 零触发 / 手打不回归 / 二次 Enter 提交 / 多行粘贴 PasteMode / 阈值边界）；P3 注入 burst=5ms；P7 重写为新语义
- 验证：MSVC Debug 全量重建 **18/19**（唯一失败 qa_CLFSessionManager 既有环境问题不变）；主程序 --version 冒烟 exit=0
- **实机验收（用户执行，全过）**：busy 期间 Ctrl+V 多次注入 → 零自动提交（agent 日志无自动 [Submit]）；注入文本停输入框（含末尾换行）；二次 Enter 提交残留；5.4 万字符超长回复期间注入无干扰；多行粘贴全链路正确（PENDING→PasteMode→InsertNewline 换行全保留）
- 排除项：右键粘贴（终端层不注入）；CLFPasserby 与提交无关
- 设计文档已归档：`.claude/plans/设计/归档/归档-自问自答严重Bug分析与修复.md`；原 F0-F5 不实施（F0 与 P8 时序矛盾且对注入场景无效，F1/F2 现状已防，F4 留作可选增强）
- 踩坑：vcvars64.bat 在 VS 18 环境 call 失败 → 手动组装 INCLUDE/LIB/LIBPATH（MSVC 14.51.36231 + Windows Kits 10.0.26100.0 均在 D 盘）绕过，方法已存 memory
- 顺带发现：`m_maxToolCallIterations` 实测为 48（progress 旧记 16 已过时）

### 2026-08-25 定时器退出机制优化 ✅（v0.4.1 已提交推送 + tag 已打，发布由用户执行）
- **背景**：`qa_CLFAgentLoop` 28.6s → **1.38s**（20.7×），全量 ctest 30s → **1.26s**
- **CLFThinkingIndicator 线程删除**：查证为纯空转——循环体算的 `elapsed` 从未使用（StatusLine 已由 turnTimer 统一管理）、`m_http` 成员从未被引用；唯一实效是退出时 `setStatus("")`，同步做即可。`stop()` 现在立即返回
- **新增 `CLFPeriodicTimer`**（`clf_types`）：条件变量唤醒，`stop()` 不等剩余间隔；回调执行期放锁防拖住 stop；回调异常在定时器内兜住（线程逸出异常 = std::terminate，v0.3.3 事故根因之一）。放 clf_types 是因 AgentLoop(core) 与 ThinkingIndicator(network) 都依赖
- **turnTimer / thinkingTimer 换用新定时器**：thinkingSec 曾被怀疑可删线程，但 `CLFToolExecutor.cpp:594` 确实读值，保留计数
- 新增 `qa_CLFPeriodicTimer`（8 用例）：核心断言 stop() 在 30s 间隔下 200ms 内返回；回调抛异常后线程不死（stderr 见 "boom" 兜底）
- ⚠ **用户验收**：状态行 `Working for Ns…` 每秒递增 / 回合结束状态清理 / 工具执行期刷新 / Esc 中断 + 安装目录 exe 替换测试，全部通过
- ⚠ **验证盲区事故**：A2 全程只跑 ctest 没启动主程序 → `main.cpp.obj` 陈旧（A2 改了 `CLFAgentConfig` 布局但 main.cpp 未重编）→ 主程序启动段错误（139 零输出）。用户当场抓出。教训已写设计文档：改公共结构必须干净重建 + 验证必须含主程序启动冒烟

### 2026-08-25 A1（=S1）小修批 ✅（v0.3.5）
- **S1-1 edit_file 空 old_string 校验**：`CLFFileOps::editFile` 入口提前返回。原行为不是死循环而是**误导性错误**——`find("")` 每个位置都算命中，会遍历全文后报 "matches N times"（N = 文件长度+1）。校验刻意置于 `readFile` 之前，qa 用 F2 用例钉死该顺序
- **S1-2 force 文案**：去掉 `CLFToolExecutor` 中对不存在参数的引用（两处：`m_content` 与 `emitContent`）
- **S1-3 重试策略分级**：`CLFRetryPolicy` 新增 `extractHttpStatus`（**前缀匹配**，原 `find` 会把响应体里的 "HTTP 400" 误读为状态码）+ `maxAttemptsForError`（三档：致命=1 / 其他4xx=2 / 429·5xx·网络=3）；致命集合由 400-403 扩至含 404/405/409/413/422。`CLFAgentLoop` 流式(:224)与同步(:279)两处判定改用分类上限；**catch 异常分支(:367)保持 kMaxRetries**（无状态码可分级，刻意不改）
- **S1-4 m_wasAborted 接线**：⚠️ 设计文档描述有误——`m_wasAborted` 是 `CLFHttpResponse` 的**响应字段**（`ICLFHttpClient.hpp:15`），非客户端成员，`abort()` 无法直接赋值。实际修法：4 个返回点均由 `m_aborted` 写入响应；并补 `postJson` 起始的标志复位（原先只有 `postJsonStream` 有，同步路径不对称）。根因链：中断→`stop()`→httplib 报连接失败→**被上层当网络故障重试**
- 新增测试：`qa_CLFRetryPolicy`（16 用例 45 断言）+ `qa_CLFFileOps`（5 用例 12 断言），均已加入 `CLF_TEST_TARGETS` 列表（该列表统一设 `CXX_STANDARD 20`，boost::ut 必需——注册新测试时**必须同时加入此列表**，否则 C++17 下 ut.hpp 编译失败）
- 构建：MSVC Debug 25/25 通过。**命令行构建需先导入 vcvars64**（CLion 内部自带环境，裸 bash 调用 cl.exe 会找不到 `<atomic>` 等标准库头）
- ⚠️ **基线记录更正**：progress 原记"ctest 12/13"已过时，实测基线为 **14/15**（唯一失败为 `qa_CLFSessionManager` 既有环境失败）

### 2026-08-25 A2（=S2）安全 + 工具批 ✅（v0.4.0，6 项全完成）
- **S2-1 read_file 边界+50MB+行范围**：三项均在 **handler 层**实施——`CLFFileOps::readFile` 还被 previewEdit / SystemPromptBuilder 等内部路径调用，在底层加限制会误伤配置读取。边界用 `weakly_canonical` 跟随 symlink 防逃逸；**逐段比较而非字符串前缀**（否则 `<cwd>-evil` 会被误判在 `<cwd>` 内，qa B1e 专门钉死）；逃生口 `agent.allow_absolute_read`
- **S2-2 危险命令检测**：⚠ **架构修正**——设计文档原写"放 CLFCommandExec"，但它在 clf_tools 而触发确认的 CLFToolExecutor 在 clf_core，依赖方向 tools→core，**core 调不到 tools**。改放 `CLFSecurityPolicy`，allowlist 也挂其上（避免给已有 8 参数的 ToolExecutor 构造再加参数）。命中强制确认，**不受安全模式影响**；定位为提示层，模型可绕过
- **S2-3 退出码白名单 + cwd**：grep/rg/findstr/diff/fc 退出码 1 判成功；首 token 归一化（去引号/路径/扩展名/大小写）；仅退出码 1 参与白名单（grep 的 2 仍判失败）。cwd 走 `lpCurrentDirectory`(Win)/`chdir`(POSIX)。**env 移入 S4**：两平台合计 60-80 行，而 `set VAR=x && cmd` 可变通
- **S2-4 search 增强**：默认文本扩展名白名单 / 忽略目录补 bin·lib·out·cmake-build-*·.idea·.vscode / 命中行非法 UTF-8 则跳过（只校验命中行，GBK 文件里的 ASCII 行仍可匹配）。`isValidUtf8` 提取到 `CLFEncoding` 供 fileops 与 search 共用
- **S2-5 web_fetch**（新模块）：⚠ **刻意不复用 CLFHttpClient**——后者恒带 `Authorization: Bearer`，抓第三方 URL 会泄漏 API key。1MB 上限 + head8K/tail2K **字节级**截断（`headTailCapWithMarker` 是 vector 模板按元素数，语义不同不可复用）+ NUL 二进制探测。风险级取 Read，**POST 由执行器动态升级为强制确认**（`m_risk` 是单值不能随参数变）
- **S2-6 todo_write**：并入会话状态、不独立落盘（对标 dsh/Claude Code 实证）。`CLFTodoItem` 放 CLFTypes（分层约束：codec 在 core，不能依赖 tools）；codec/SessionManager 加**带默认值**的 todos 参数，`version` 维持 1 双向兼容；**首个捕获 agent 引用的 handler**——已给 `CLFAgentLoop` 显式 `= delete` 拷贝/移动钉死自引用约束
- 新增测试 4 套：`qa_CLFBuiltinTools`(16/33) `qa_CLFWebFetch`(11/37) `qa_CLFMessageCodec`(7/21) + `qa_CLFSecurityPolicy` 扩展(+6 用例)
- 🚨 **踩坑：静态非平凡对象（本项目第二次）**——S2-4 首版三张查表用文件级 `std::set/std::vector`，`qa_CLFSearchContent` 立即段错误（零输出）。根因：boost::ut 在**静态析构阶段**运行测试，跨 TU 析构顺序未定义。改 `constexpr const char* const[]` + 线性查找解决。与 A1 的 magic static 死锁同源，已写入设计文档为项目级教训
- 验证：MSVC Debug 构建通过；**ctest 17/18**（唯一失败 `qa_CLFSessionManager` 为既有环境问题）

### 2026-08-25 qa_CLFAgentLoop 超时根因排查 ✅（两个独立问题，非"单一既有问题"）
- **排查手段**：ctest 输出为空曾被误判为"早期挂起"——实为 stdout 重定向到管道是全缓冲、进程被 kill 时缓冲区丢失。改用 **stderr 逐用例插桩**（无缓冲）定位到 case 5 = T6b，再逐行插桩收窄到 `S4b → S9` 之间
- **问题①（本次引入，已修）**：`CLFRetryPolicy::extractHttpStatus` 中的函数局部 `static const std::string kPrefix` —— MSVC 的 magic static 走 `_Init_thread_header` 全局锁，在该多线程路径上**死锁**，表现为进程永久挂起、零输出。改为 `constexpr const char*` 后消失
  - 判定证据：禁用 `fireInterrupt` 后**仍然挂起**（排除中断竞态）；改 constexpr 后立即全绿
- **问题②（既有，未修）**：即便无死锁，该测试仍需 **28-29 秒**。根因是 `turnTimer` / `thinkingTimer` / `CLFThinkingIndicator` 三个后台线程都用 `sleep_for(1s)` 轮询退出标志，每次 join 平均等 ~0.8s，12 个用例累计约 2.4s×12。已另立待办（条件变量改造）
- **顺带修复**：`MockHttpClient` 队列耗尽时只 `expect` 后继续对空 deque 调 `front()/pop_front()` 是 **UB**（boost::ut 的 expect 不终止执行），改为抛异常由 `runTurn` 的 catch 兜住
- ⚠️ **更正 A1 提交中的错误结论**：当时记"基线也超时，与本批改动无关"——**只对了一半**。基线超时确实存在（原因是②的慢，29s > 我设的 25s timeout），但我**另外引入了①这个真死锁**，当时未能区分。教训：`git stash` 验证基线时只看了退出码，没有区分"慢"与"挂死"
- **真实基线：14/15**（唯一失败 `qa_CLFSessionManager` 环境问题）；跑 ctest 需 `--timeout 90` 以上，否则 AgentLoop 会被误判超时

### 2026-08-25 设计文档核实与合并 ✅
- 对两份新增设计文档（08-19 产出）的 **23 条代码断言逐条对照源码验证**，修正 7 处后合并为 `设计/设计-功能修复与工具补充.md`，原两份删除
- **关键修正**（原文档错误）：① `generateAndCacheSummary` 实由 `/exit`+`/clear` 调用而非压缩路径；~~`shouldSummarize` 已实现~~ **08-25 二次核实为臆造——`CLFSessionSummarizer` 无此方法（全库零命中），仅 `isEnabled()` 判开关**；S3-1 阈值判定需新写，成本回到原估 ② CLFMessageCodec 已有 `version:1` 字段（P2-4 降级为只补分流）③ `m_wasAborted` 检查在 `:212`/`:266`（非 203-207）④ CLFToolExecutor 在 `CLFCore/` 非 `CLFTools/` ⑤ **web_fetch 不可复用 CLFHttpClient——恒带 `Authorization: Bearer` 会向第三方泄漏 API key**（安全缺陷）⑥ ask_user 的"CLFRepl 无需改"不成立（`:524` 硬编码 0↔1 取反、Enter 二分支、返回 bool）⑦ `.clf/` 目录约定不存在（全库零命中）
- 其余 16 条断言属实，可照做
- 路线决策（用户定）：走 S1→S2→S3，dsh 挂决策门；**多模型（S3-2）用户定为必做**

### 2026-08-19 v0.3.4 小功能更新 ✅
- 新增一项小功能性更新（不影响既有功能与使用，公开口径见 CHANGELOG v0.3.4）；tag v0.3.4 已推，`release/CLFCode-v0.3.4-win64.zip` 已出包（3.6M）
- 🔒 **隐私备忘（维护者/pro 必读，勿删除）**：v0.3.4 该项更新含用户隐私内容。**其触发条件、特殊日期、名称/编码方式、实现细节一律不得在本仓库任何文档（本文件 / CHANGELOG / 设计文档 / README）中体现**。维护时仅按公开口径"小功能性更新"提及，不展开、不说明。此前版本曾写入具体触发细节，已按用户要求移除——后续涉及该功能时请保持"隐形"。

### 2026-08-18 笔记本输入必崩根因修复 ✅（FTXUI 事件队列竞态，v0.3.3 候选）
- **现象**：安装版交互输入必崩——流式回合静默退出，无报错/WER/转储，日志截断于 `[API] streaming request`；退出码 3（terminate/abort）；非交互模式同提示完整跑通；桌面同包正常
- **四轮取证**：17:47/17:50/18:07 三次真实复现 + 监控时间线（iter=2 工具回合流式 → 进程消失）→ `EXIT=3` + stderr 空 + 零 WER → 排除 [Fatal]/硬崩/包问题 → **退出码 3 = std::terminate（未处理异常在非 UI 线程）**
- **根因**：FTXUI v7 `MultiReceiverBuffer`（event_buffer）**无锁**——CLFCode turnTimer/提交/思考指示器/粘贴定时器多线程 `PostEvent`→`Push`，UI 主循环每帧 `Pop`/`Prune` 并发访问同一 deque → 数据竞争（UB）→ 内存破坏 → turnTimer 线程 `push_back` 抛异常 → 无兜底 → terminate → abort（退出码 3）；破坏若打在 UI 线程则为访问违例（0xc0000005——08-12 那 36 条 WER 硬崩疑似同一竞态的另一形态）
- **修复（A→B→C 三层）**：A 根治——`multi_receiver_buffer.hpp` 全方法加 `std::recursive_mutex`（12 处锁）；A+ `previous_animation_time_` 原子化（app.cpp）；B 防御——turnTimer/思考指示器/粘贴定时器线程体 try/catch（异常记 `[TurnTimer]`/`[ThinkingIndicator]`/`[PasteTimer]`，分层约束下 clf_network 用 cerr）；C 可观测——`set_terminate` 全局兜底留痕
- **验证**：Debug/Release（MSVC）构建通过；ctest 11/12（SessionManager 既有失败不变）；**E:\deepseek-harness 现场 11 轮工具迭代 + 40 消息上下文完整跑通不再崩**；B/C 防御零触发
- 设计文档：`.claude/plans/设计/设计-FTXUI事件队列竞态修复.md`（含实施记录）
- 收尾：bump v0.3.3 + CHANGELOG 条目；设计文档归档至 `设计/归档/归档-FTXUI事件队列竞态修复.md`；install.ps1/upgrade.ps1 修复——升级不再丢会话历史/日志/崩溃转储（GUID 唯一备份目录 + 四目录备份恢复，模拟测试通过）；用户手动替换安装版 exe 验证通过
- 遗留：08-12 AV 家族同根因假设待观察
- ~~发布 v0.3.3 未执行~~ → **更正（08-25 核实）**：v0.3.3 已发布，`release/CLFCode-v0.3.3-win64.zip` 08-18 出包，tag 已推。包体从 12M 降至 3.6M 系 v0.3.2 起 DLL 只带 OpenSSL 对、不再携带历史 MinGW 运行库所致（预期变化）

### 2026-08-18 发布版必崩溃根因修复 ✅（v0.3.2，取证闭环）
- **现象**：v0.3.1 打包版在台式机+笔记本必崩溃（输入"帮我查看当前项目信息"），源码 Debug 运行正常；同一 exe 时崩时不崩（时序相关假象）
- **四轮取证定位**（诊断 exe + 异常陷阱，`CLF_DEBUG_EVENTS` 日志）：`[Fatal] No mapping for the Unicode character exists in the target multi-byte code page` → `[HandlerExc]`（Esc 退出路径，回合完成后 51s 触发）→ `[EscExitExc]`（/exit 分发）→ `[ExitSaveExc]`（**saveSession finalize 归档**）
- **根因**：MSVC 窄字符文件系统 API 按 ANSI 代码页（CP936）解释 UTF-8 路径字节——`/exit` 归档中文标题会话（`时间戳_帮我查看一下项目信息.json`）时转换失败抛异常 → run() 兜底退出。诊断版 6-7 轮循环全绿验证修复
- **修复**：CLFSessionManager/CLFConfigLoader/CLFFileOps 全链路 `u8path`/`u8string`；标题截断 UTF-8 边界安全；readFile UTF-8 内容探测（顺带修乱码隐患）；listDirectory 宽路径直读
- **连带修正**：release.ps1 构建目录（旧脚本静默打包陈旧 exe 的间接成因）+ 构建失败硬退出 + exe 新旧自检 + vcvars 环境导入 + DLL 只带 OpenSSL 对
- 取证设施清理完毕（复现钩子/陷阱移除；增强异常捕获与事件日志设施保留）

### 2026-08-17 复制粘贴功能修改 ✅（验收通过，关闭，已清理归档）
- 分析：`.claude/plans/分析/分析-复制粘贴功能修改.md`；设计已归档：`.claude/plans/设计/归档/归档-复制粘贴功能修改.md`（Flash 四轮 12 条意见 + 终审 6 缺口全消化）
- 实现：CLFPasteCoalescer（粘贴事件突发合并，P1-P10）+ CLFSelectionModel（选区状态机/提取，S1-S7）+ 渲染器 RowMap 并行构建与高亮 + qa_CLFInputRender（Ref 光标同步回归）；ctest 基线 11/12（SessionManager 既有环境失败不变）
- **验收收敛定稿（用户决策）**：选区 = 纯鼠标左键拖选 + 松手自动复制（copy-on-select）；移除 Ctrl+S 键盘选区与 Ctrl+C/Enter 复制；Ctrl+C 空闲忽略（原误触即退出）、busy 中断保留
- **验收期根因修复 7 项**（事件日志取证实证）：① cv 谓词缺陷（wait→wait_until）② 鼠标坐标 0 基（WT/ConPTY 投递 0 基，FTXUI Box 对照）③ **ENABLE_PROCESSED_INPUT 未清**（FTXUI 不清理，Ctrl+C 被系统转信号、SIGINT 处理器直接退主循环——事件永远到不了应用层）④ 拖选末字符丢失（colToByteEnd 含入 + 松手补位）⑤ hitTest 下方 clamp 缺陷（输入框点击误判为选区）⑥ **Ref<int> 拥有型构造致光标不同步**（粘贴首两行合并，\n 被推到末尾——改引用型 Ref，字节级定位）⑦ 防重复守卫误吞粘贴 Return（500ms→100ms+无字符间隔，后随 Ctrl+C 复制移除一并删除）
- 顺带根因修复 qa_CLFSecurityPolicy 测试缺陷（const char* 指针比较 → std::string）
- 已知边界：粘贴源若为终端原生复制（Shift+拖选）会带渲染网格填充空格——用应用内拖选复制作源；超大粘贴分批（>40ms 批间隔）可能整段自动提交（设计 §2.1）
- 取证模式保留：`CLF_DEBUG_EVENTS=1` → `doc/log/clf_events.log`（独立追加）；取证屏幕转储、键盘选区 API（moveCursor）等临时设施已清理
- 文档同步：/help 与 README 快捷键表、CHANGELOG v0.3.1 条目

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
