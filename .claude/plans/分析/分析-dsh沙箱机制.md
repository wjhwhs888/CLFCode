# 分析-dsh沙箱机制（CLFCode 安全模型升级参考资料）

> **状态**：调研分析（资料性，供后续安全模型设计引用，非实施方案）
> **创建**：2026-09-03
> **来源**：dsh checkout `E:\deepseek-harness`（packages/sandbox/* + SAFETY.md + docs/subsystems/sandbox.md）+ 本 agent 运行时第一手体验（本 agent 即 dsh 沙箱的消费方）
> **用途**：CLFCode 从"提示层安全"走向"强制层安全"的参考；阶段 3 第三方工具/插件接入时的安全模型设计依据
> **上游背景**：《分析-安全策略.md》（CLFCode 现有安全策略 = 提示层）、《设计-阶段3-第三方集成插件式方案.md》（第三方接入 = 不可信代码进进程）

---

## 〇、认知框架：沙箱是一条光谱（先把"沙箱"这个词摆正）

"沙箱"在 AI agent 语境下不是一个东西，是一条**强制力递增的光谱**：

```
提示/自律  →  审批门禁  →  文件效应策略(OS机制)  →  容器/进程隔离  →  VM/硬件隔离
(最弱)                                                        (最强)
模型自觉    人把关       同内核限文件写              换内核视角/命名空间   换机器
```

- **提示/自律**：CLFCode 现在的 `CLFSecurityPolicy` 危险命令检测——"提示层"，注释自承**模型可绕过**
- **审批门禁**：人确认后才执行——靠人不靠机制
- **文件效应策略**：dsh 沙箱所在的位置——**同内核**，用 OS 机制（token/ACL/挂载）限制"文件写"这一类效应，模型无法绕过，除非升级+人批
- **容器/VM**：换隔离环境，能力缝整体替换（dsh 明说这不是它做的事）

**关键认知**：dsh 沙箱只覆盖光谱中"同内核限文件效应"一格。它**不管读、不管网络、不管进程可见性**。威胁模型 = "宿主愿意让 agent 读到一切，只是防它乱写/防它把机器搞坏"。这是理解它一切设计的前提。

**另一个关键认知**：dsh 官方 SAFETY.md 第一句话——experimental、**未经安全审计**、不保证隔离，"不要把它当唯一安全控制"。任何沙箱都防不了"你允许它访问的资源"。

---

## 一、定位：它解决什么问题

模型输出**不可信**（幻觉、prompt injection、跑偏命令），但 agent 必须**真执行**才能干活。矛盾 = "最小权限 + 按需放开"。

dsh 的答案：每个子进程调用都套一个**文件效应策略**（file-effect policy），默认收着，越权时报错并给一条**升级路径**（人审批一次）。

设计哲学四条（源码 index.ts 明写）：
1. **Same-world by contract**：共享宿主内核和文件系统；容器/microVM/远端执行是替换整个能力缝，不是加后端
2. **Policy rides the call**：策略**随调用携带**，不固定在 provider 上——两个 consumer 可同一瞬间跑不同模式；升级重试 = 一个带更宽策略的**新调用**
3. **Fail closed**：要的模式无后端可用 → 抛 `SANDBOX_UNAVAILABLE`，**绝不静默无沙箱运行**
4. **一套否决/升级词汇**：denial 标记、hint 文本、升级阶梯单点定义，bash 与 fs 两族消费方不会漂移

---

## 二、模式与词表

| 模式 | 效果 |
|---|---|
| `read-only` | 禁写（保留 /dev/null 等必要 sink） |
| `workspace-write` | 可写 workspace 根 + 后端定义的私有 temp 区 |
| `danger-full-access` | 绕过沙箱，跑原始 argv |

类型词表（跨 DLL/服务契约用）：
- `SandboxMode`：三模式
- `SandboxPolicy` = mode（受限二选一）+ `workspaceRoot` + `sessionId`
- `SandboxExecutionPolicy`：完整解析后的单次执行策略（root 恒携带，即使 mode 不用它）
- `SandboxEnforcement`：`full` / `partial`——**上报的事实不是承诺**，partial 就如实说 partial

### 升级阶梯（escalation）

闭表，**运行时检查，不烧进工具 schema**：
```
read-only        → workspace-write 或 danger-full-access
workspace-write  → danger-full-access
```
流程：受限调用被拒 → 报 denial 标记 `[sandbox: file access denied under <mode> mode]` + hint（提示可升级）→ 模型用 `sandbox_permissions`（最窄够用模式）+ `justification` **原样重试一次** → 用户一个审批弹窗（allow once / reject / cancel）→ 结果成为该调用返回文本。升级必须**严格更宽**，非加宽请求不弹人直接拒。

### 失败分类（排障关键）

- 无后端 → `SANDBOX_UNAVAILABLE`（错误文本点名缺哪个平台 runner）
- runner 启动后坏了 → 结构化 **runner-failure signature**（stderr 致命行 + 退出码规则），**可区分"沙箱坏了"与"命令被拒"**
- 被拒 → 该后端的 denial 方言

### 否决方言（denial dialect）

每个后端内核说自己的否决语言，`denialSignatures` **按后端**给，不做跨后端 union：
- bwrap（Linux 只读 bind）→ EROFS 文本
- Landlock（Linux）→ EACCES
- Seatbelt（macOS）→ EPERM
- Windows ACL → EPERM

> 设计理由：union 会声称某后端从不产生的否决，消费方误判。

---

## 三、平台后端与选择

| 平台 | 后端链 | 机制 |
|---|---|---|
| Linux | bwrap（优先）→ Landlock launcher | bwrap：只读宿主根 + 私有 PID namespace（子进程看不见宿主进程 → procfs magic links 无法绕过挂载）；Landlock：npm native addon，模式映射为 grants |
| macOS | Seatbelt（sandbox-exec） | allow-default + `(deny file-write*)` + writableRoots 白名单；根全部 canonical 化（/tmp IS /private/tmp） |
| Windows | **ACL restricted-token runner** | 见下 |

选择策略：平台优先、功能 probe 其次；唯一候选不 probe；竞争候选按链序 probe 一次，**首个可用结果缓存整个 provider 生命周期**（换 runner 要重载插件）。

---

## 四、Windows 机制详解（本机平台，重点）

### 4.1 核心机制：WRITE_RESTRICTED token 双检查

1. 调用者的 token 被复制成 `WRITE_RESTRICTED` token，限制 SID 携带两组能力：**workspace** 与**私有 temp**
2. Windows 访问检查跑**两遍**：正常 SID 一遍 + 限制 SID 一遍；写类访问必须**两遍都过**
3. 效果：子进程在受限 token 下，写权限被收窄到白名单目录

### 4.2 身份设计：确定性 vs 随机性

| | workspace SID | temp SID |
|---|---|---|
| 派生 | 从 canonical workspace 路径**确定性推导** | 随机私有 temp 路径派生 |
| 生命周期 | ACE **standing（常驻）**——每 workspace 每机器物化一次，后续全命中（复用缓存） | ACE **revocable（可撤销）**，dispose() 撤销 |
| 粒度 | 共享：同 workspace 的会话共享写权 | 隔离：每个 live session/workspace 对独立——**会话间互不能写对方 temp** |

崩溃残留处理（精妙）：新 provider 永远选**新 temp 路径 + 新 SID** → 崩溃残留既不能阻塞、也不能授权恢复的会话（"inert litter"）。

### 4.3 踩过的 Windows 坑（token 名单设计即踩坑史）

- **keep-alive 组（logon SID + Everyone）两模式都在**：去掉 → 早期 DLL 初始化死 `0xC0000142`、CNG 崩 pwsh `0xE0434352`
- **Authenticated Users 必须缺席**：否则 WMI 命名空间安全检查放行 → CIM cmdlet / Get-ComputerInfo 可用（不该可用）；缺席顺带关闭 C:\ 根树创建逃逸
- **INTERACTIVE/LOCAL 缺席**：宿主 Public 树对 INTERACTIVE 有写权 → Public 写入被拒
- **NUL 写是 ambient**（设备 DACL 允许），两种模式都能 `> NUL`；`Set-Content NUL` 两模式都失败（.NET 层效应）

### 4.4 如实申报的边界（为什么 enforcement = partial）

1. **Everyone 必须保留** → 外部对象若 DACL 给 Everyone 写权，双检查都过 → 仍可写（Windows 无法根治）
2. **NTFS 硬链接 = 文件对象别名**：工作区 ACE 传播到既有硬链接会改底层文件安全描述符 → 同一对象经外部别名可写（拒绝多链接文件对 pnpm 不现实）
3. **只限写**：WRITE_RESTRICTED 只交写访问——受限子进程**能读任何调用者可读文件、能开 socket**；read-only 要表达读限制需另配读侧策略
4. **控制台隔离不可用**：CREATE_NO_WINDOW / CREATE_NEW_CONSOLE 会 DLL 初始化失败（0xC0000142）→ 子进程共享宿主控制台
5. **ACL 授予是常驻目录变更**：workspace ACE 常驻（设计如此）；temp ACE 必须经本模块 revoke（手工 icacls 撤不了，报 ERROR_NONE_MAPPED 1332）
6. **授予目录须调用者所有**：靠 owner 隐式 WRITE_DAC 才能免提权改 DACL
7. **temp 根永不隐式授予**：必须显式给私有 tempDir + tempWriteSid，或 tempDir:null 禁 temp 写；temp 与 writable roots 必须不相交

### 4.5 消费方视角（模型眼里是什么样——本 agent 第一手）

- pwsh 通道：read-only 模式 = PowerShell **ConstrainedLanguage**（禁 .NET 静态调用/Add-Type/COM）；workspace-write = FullLanguage（除非 host policy 收紧）
- 受限模式**不能开 named pipe**：捕获子进程 stdio 的管道 spawn 报 EPERM——这是**进程逃逸面收口**（子进程不能经命名管道与外部通信）
- 文件工具：越权 → denial 标记；升级 = `sandbox_permissions`（workspace-write / danger-full-access）+ justification → 用户审批一次
- 部署可配：`sandbox-policy` 的 `mode` 定会话默认模式；approval 可整体禁用（禁用时需审批的动作直接拒，不弹窗）
- **审批与沙箱是两层**：审批 = 人同意；沙箱 = OS 强制。升级请求 = 两者交汇点。禁审批 ≠ 无沙箱（沙箱仍在，只是没有放开通道）

---

## 五、测试与回归验证（dsh 怎么证明沙箱有效）

### 5.1 分层测试金字塔——每层 oracle 来源不同

| 层 | 文件示例 | oracle（测试依据）来源 | 防什么 |
|---|---|---|---|
| L1 契约单测 | sandbox/tests/escalation.spec.ts、vocabulary.spec.ts | **源码单点定义的契约**：升级阶梯表、denial/hint **逐字文本**、fail-closed 顺序——测试钉死防漂移（escalation.spec 注释："pinned once, next to the vocabulary that owns them"） | 两族消费方（bash/fs）行为漂移 |
| L2 逻辑单测 | sandbox/tests/roots.spec.ts、fs-sandbox/tests/containment.spec.ts | **纯逻辑属性**：canonical 化、symlink 解析、writableRoots 推导、进程内围栏 | 根/围栏推导错误 |
| L3 实机 e2e | sandbox-local + shell/*-sandbox/tests/*.e2e.ts（bwrap/landlock/seatbelt/acl） | **真实 OS 强制行为**——真后端 + 真 shell spawn，验证"真的拦住了" | mock 永远证明不了的"真拦截" |
| L4 期望输出/发布 | *.expected.e2e.ts、sandbox-local/tests/packed-install.e2e.ts | 组装式期望输出 / 打包后实装行为 | CLI 行为回归、发布包漂移 |

### 5.2 实机 e2e 的验证技巧（以 pwsh-sandbox/tests/acl.e2e.ts 为例）

1. **真 spawn 真后端**：LocalSandboxProvider（win32 链 → windows-acl runner）+ 真 pwsh 在受限 token 下执行
2. **探测脚本真尝试**：一次运行内真写四类路径——workspace 内 / 私有 temp（`$env:TEMP`）/ 环境 temp / **escape 路径**，外加读一个 secret 文件
3. **双重断言防"脚本撒谎"**：不只信 stdout 的 `OK/DENIED` 标记，还用 `existsSync` **实查文件系统**——`expect(existsSync(...)).toBe(false)` 证明文件真没被创建；命令自己 catch 住拒绝时 exit 0、无 denial 事实，裸失败写时 exit≠0 且 `sandbox.denied=true`——两种形态都断言
4. **sandbox 事实对象进断言**：`{mode, denied, enforcement: 'partial'}` 随结果携带，partial 如实上报也验证
5. **平台纪律**：`describe.skipIf(!isWin32 || !pwshAvailable())`——平台不符/工具缺失自动跳过；**同一套测试文件**在 Linux 跑 bwrap/landlock、macOS 跑 seatbelt、Windows 跑 ACL，各验各的真后端
6. **细节验证**：私有 temp 重写（探测 `$env:TEMP` 指向 tmpdir 下私有目录、运行后该目录 `existsSync` = false——退出即清理）

### 5.3 套件分层与门禁矩阵

- **vitest 配置分层**（顶层的 vitest.*.config.ts）：单测 `test`；真 API e2e `test:e2e`（注释自承 *"spends tokens"*——无凭证自跳过、超时 120s、重试 2 防抖、`DSH_E2E_MAX_WORKERS` 控并发）；expected `test:expected`；snapshot `test:snapshot`（`DSH_SNAPSHOT=record/refresh/replay` 三态）
- **`check:ci:*` 门禁 = `scripts/run-gates.ts` 总控**：`ci-primary` / `ci-windows-blocking` / `ci-windows-complete` / `ci-windows-observational`——**Windows 测试按可信度分三档**：blocking 必须过、complete 全量、observational 不 gate
- **lefthook 本地钩子**：注释明说 *"keep local checkpoints fast; CI owns the full repository-wide gate matrix"*——本地只跑快检查点（lint/notices/whitespace），全量矩阵归 CI
- **verify-\* 一致性脚本群**（几十个）：`verify-tool-catalog` / `verify-config-catalog` / `verify-package-readme-model-experience` / `verify-package-readme-limitations`——**README 写的"模型会看到什么错误文本"与实现 verbatim 一致**，系统性防文档/代码漂移
- **发布验证**：`release:verify-packed-install`（打包后实装冒烟，对应 packed-install.e2e）

### 5.4 oracle 来源总结（回答"测试依据从哪来"）

1. **契约层**：源码单点定义的词表/阶梯/逐字文本——测试是钉书钉
2. **行为层**：真实 OS 强制行为 + 文件系统实查——证明"真拦住了"，不信被测试者自报
3. **意图层**：设计决策文档（`.agents/notes/.../2026-07-06-sandbox.md`）与 bug-fix 记录（`2026-08-06-bwrap-private-pid-namespace.md`）——每次修边界 bug 的教训回流成下一轮测试依据

### 5.5 对 CLFCode 的可抄点（测试侧）

1. **安全/沙箱类测试必须"真跑 + 实查副作用"**：验证危险命令确认时，不只信界面提示，要实查文件真没被写（CLFCode 非交互模式文本流 diff 已有雏形）
2. **契约测试钉文案/状态机**：qa 已有先例（force 文案、A5-9 双配置），可推广到安全策略词表/确认框文案
3. **可信度分层**：blocking / complete / observational 三档 idea——核心回归永远跑，重活按需
4. **verify 脚本防文档漂移**：P2-6 过时注释就是漂移实例；将来做插件 API 时配"文档示例与实现一致"校验

---

## 六、对 CLFCode 的启示

### 5.1 现状对比

| 维度 | CLFCode 现状 | dsh |
|---|---|---|
| 危险命令检测 | CLFSecurityPolicy：**提示层**（模型可绕过） | OS 机制强制（token/ACL/bwrap），不可绕过 |
| 写权限 | 工作区检查 isWithinWorkspace（提示+确认） | 文件效应策略 + 升级阶梯 + 人批 |
| 工具扩展 | 加工具改 executor（阶段 1 在改能力标签） | 插件元数据 + per-call policy |
| 威胁模型 | 单机自用 agent，模型是唯一执行者 | 宿主托管大量不可信子进程/插件 |

### 5.2 可抄的设计决策（与具体 OS 机制解耦，先抄决策）

1. **Fail closed**：宁可不跑，不裸跑。任何"没把握拦住"的操作默认拒绝，错误明说缺什么
2. **Per-call policy**：模式随调用走，不做全局开关——同一程序里读类工具永远 read-only、写类工具 workspace-write 是常态
3. **拒绝分类**：区分"机制坏了"（runner failure）与"被拒"（denial）与"命令失败"——排障体验的分水岭
4. **Enforcement 如实上报**：做不到的边界明说 partial，不虚报绝对
5. **升级阶梯 = 最小权限的操作化**：只宽一次、只宽到够用、要理由、要人批、非加宽直接拒

### 5.3 映射到 CLFCode 的形态（若未来要做）

- 三模式可对齐现成概念：`read-only` ≈ Analyze 模式；`workspace-write` ≈ 工作区内可写；`danger-full-access` ≈ --allow-write/确认放行
- denial → hint → 升级 → 人批 的 UI 链，CLFCode 已有确认栏（ask_user/ConfirmBar 先例）可复用
- 会话隔离思想可借鉴：每会话私有 temp/状态目录，互不越界（呼应它 jsonl 会话的隔离）

### 5.4 何时才需要 OS 级（判据）

- **现在（自用单 agent）不需要**：CLFCode 自己是执行者，危险命令确认（提示层）+ 工作区检查已够用；上 OS token 级隔离是过度设计
- **阶段 3 接入第三方工具/插件时需要**：插件 = **不可信代码进进程**——那时"提示层"守不住，需要真隔离（受限 token 子进程 / 独立进程 + IPC——呼应总纲"第三方 = IPC"的定案，IPC 本身就是隔离形态）
- 中段可选：工具注册时**能力声明 + 运行时检查**（能力标签方向已在阶段 1 B1）——是"静态声明"那一步，与 per-call policy 精神一致

### 5.5 词汇边界提醒（别抄错）

dsh 沙箱**只限文件写**。若 CLFCode 未来要防"agent 读敏感文件 / 联网外传 / 进程可见"，需要额外层（读侧策略/网络策略）——那是另一块设计，不是加个沙箱就有。

---

## 七、术语速查

| 术语 | 含义 |
|---|---|
| same-world | 同内核同文件系统的进程约束（非容器/VM） |
| file-effect policy | 只约束文件效应的策略词表（写/不写/全开） |
| per-call policy | 策略随单次调用携带，非全局 |
| fail closed | 无保障即拒绝，绝不静默裸跑 |
| enforcement full/partial | 后端对承诺效应的覆盖完整度（如实上报） |
| denial dialect | 各后端内核特有的拒绝文本（EROFS/EACCES/EPERM） |
| runner-failure signature | runner 自身损坏的特征（区分沙箱坏 vs 被拒） |
| escalation ladder | read-only→workspace-write→full-access 的加宽闭表 |
| restricted token | Windows：复制+限制 SID 的 token，双检查 |
| standing / revocable ACE | 常驻（复用缓存）/ 可撤销（会话级）ACL 项 |
| inert litter | 崩溃残留（新 temp+SID 使其既不能阻塞也不能授权） |

---

## 八、参考资产（溯源路径）

- 核心契约：`E:\deepseek-harness\packages\sandbox\sandbox\`（README + src/index.ts + src/escalation.ts + src/roots.ts）
- 平台后端：`packages\sandbox\sandbox-local\`（runner 链与选择）
- Windows 后端：`packages\sandbox\sandbox-windows-acl\`（README 含已验证边界清单）
- 消费方：`packages\shell\pwsh-sandbox\`、`packages\shell\bash-sandbox\`、`packages\fs\fs-sandbox\`
- 测试：契约单测 `sandbox/tests/*.spec.ts`；实机 e2e `sandbox-local/tests/*.e2e.ts`、`shell/*-sandbox/tests/*.e2e.ts`（acl.e2e.ts 为 Windows 实机范例）；套件配置顶层 `vitest*.config.ts`；门禁 `scripts/run-gates.ts` + package.json `check:ci:*`
- 权威总览：`docs/subsystems/sandbox.md`；免责声明：根 `SAFETY.md`
