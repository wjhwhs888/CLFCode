# 设计-Agent 行为 E2E 自动化回归（prompt 驱动 · 判定过不过）

> **日期**：2026-09-23（flash 出稿 → pro 审查定案）
> **触发**：用户人工一条条复制提示词测试发布版，意识到可自动化；且人工实测暴露成本失控（全量搜索/全量测试烧 token，用户手动 ESC 止损）
> **状态**：**定稿（2026-09-23 pro 拍板）**，待实施
> **边界**：本设计只管测试基建（用例文件 + 驱动 + 采集 + 断言 + 报告 + 隔离 + 成本门）；被测缺陷本身的修复另立设计（X*/M* 项）
> **分工**：用例内容的唯一权威 = `测试/测试-Agent行为实测记录.md`（提示词 + 预期 + 实测取证链）；本文规定实现形态

---

## 定稿摘要（先读这一页）

**一句话**：把"人工按提示词跑 + 肉眼看 ✗"升级为**用例文件驱动的可判定回归**——`--prompt` 逐例拉起非交互会话，从 jsonl + 日志 + stdout + 工作区 diff 提取事实，按断言原语判定 **PASS / FAIL / UNKNOWN**（UNKNOWN ≠ PASS），出报告。

**三层分工**（能确定的给 ctest，只有依赖模型决策的进本方案）：

| 层 | 覆盖 | 成本 | 谁判 |
|----|------|------|------|
| ① `ctest`（39 套件） | 执行器/白名单/错误归一化/显示规则 | 免费、确定 | 断言 |
| ② **本方案 AT** | 真 LLM + 真工具链的行为与信号 | 花钱、非确定 | 断言 + repeat |
| ③ 人工验收清单 | 交互类（ESC/确认栏/拖选/Home-End） | 人工 | 人 |

**核心原则**：负向断言优先；**成本也是断言**（token/时长/工具数上限——把"无界轮询"变成自动失败）；**必须有负控**（注入已知缺陷，harness 必须 FAIL——报绿则 harness 不可信）。

**pro 定案（实现形态，flash 方案 §四 的空白已填）**：

| 项 | 定案 |
|----|------|
| 驱动语言 | **PowerShell**（`run.ps1`——与 install.ps1/upgrade.ps1 同生态；解析 jsonl 用 ConvertFrom-Json 足够） |
| 目录 | `tools/agent-e2e/`：`cases/*.json`（资产集）+ `run.ps1` + `reports/` |
| 用例格式 | **JSON**（每例一文件；字段见 §六） |
| 隔离方式 | **环境变量** `CLFCODE_TEST_SESSION_DIR` / `CLFCODE_TEST_LOG_DIR`（沿用 CLFCODE_TEST_* 先例——不新增 CLI 参数，用户面零影响；`--help` 不用文档化测试钩子） |
| 成本门 | wall-clock **运行时硬门**（超时杀进程）；token/工具数 **跑后判定**（jsonl 统计） |
| 报告 | `reports/<时间戳>.md`：每例三态 + 通过率 + 证据行 |
| 触发方式 | 手动 / 发布前回归门（**不进 ctest、不进 CI**） |

**MVP 起步**：隔离（步骤 0）→ 成本门（步骤 2，用户第一痛点"不能这么造"）→ runner + 断言（步骤 1）→ 三例验证（`plat-selfreport` / `dir-nomatch` / `notfound-explain`）。

---

## 一、目标与非目标

**目标**：① 人工提示词测试固化为可重复、可判定、可回归的用例集 ② 每例输出三态（UNKNOWN 不许当 PASS）③ 模型行为退化自动发现（如又开始用 `head`）④ 成本纳入判定。

**非目标**：不进 ctest 常规套件；不替代单元测试（能确定的一律下沉 ctest）；不做通用输出质量评测。

---

## 二、现状取证（pro 亲验 2026-09-23）

| 项 | 结论 | 证据 |
|----|------|------|
| 无人值守入口 | ✅ `--prompt`/`--prompt-file` + 互斥检查 | `CLFArgParser.cpp:37-42`、`:51-54`（实读） |
| 工作区/配置隔离 | ✅ `--project-root`/`--config`/`--allow-write` | 同上 `:29-36` |
| **会话/日志落点隔离** | ❌ **缺**（参数表全表无 session/log 目录项；落点硬编码 → 用例互相污染 + 污染真实 doc/**） | `CLFArgParser.cpp:12-57` 全表实读 |
| 测试钩子先例 | ✅ `CLFCODE_TEST_INSTALL_DIR` 等 | `install.ps1` 3 处、`upgrade.ps1` 4 处 |
| 驱动 + fixture 比对先例 | ✅ | `tools/spike/spike_driver.mjs` + `frames/norm/` |
| 旧 AI 冒烟脚本 | ❌ 不存在（全仓 `test_ai` 零命中） | grep |

---

## 三、单例运行流程（行为约束）

1. 每例**独立工作区 + 独立配置 + 隔离落点**（环境变量）——不污染真实 `doc/**`
2. 非交互发起（`--prompt`）→ 采集事实源（jsonl / 日志 / stdout / 退出码 / `git status --porcelain`）
3. 断言判定三态 → **repeat N**（默认 3，全过才 PASS，记通过率）
4. **触成本门 → 立即杀进程 + FAIL**（不许挂死）
5. 结束清理临时目录、工作区干净

---

## 四、断言原语（从采集事实派生）

| 原语 | 来源 | 说明 |
|------|------|------|
| `toolCallsMax` / `toolSeq` | jsonl | 工具调用次数与顺序 |
| `cmdMustNotMatch` / `cmdMustMatch` | jsonl（`execute_command.command`） | 命令文本正则（**负向优先**：`\| head`、`findstr .* -A`、`start /b`、`%ERRORLEVEL%`、`& "`、`where /R`） |
| `noRepeat` | jsonl | 同一命令连续重复 ≤ 1 次 |
| `noUnpairedCalls` | jsonl | 声明数 == 结果数（恒真不变量） |
| `displayStateNot` | stdout | 用户可见状态不得为某值（如 ✗）——显示规则优先下沉 ctest |
| `errorKindIn` / `errorKindNotIn` | jsonl | 归一化错误类 |
| `budget.wallClockSecMax` | 计时 | **成本门（运行时硬门）** |
| `budget.tokensMax` | jsonl 统计 | **成本门（跑后判定）** |
| `workspaceClean` | `git status --porcelain` | 无临时文件残留 |
| `exitCodeIn` | 进程 | 退出码 |

**非确定性纪律**：负向优先；repeat N；固定变量（同 config/同模型/同 prompt/同 fixture）；UNKNOWN ≠ PASS 且必须进报告。

---

## 五、成本门（本方案的安全阀——用户第一痛点）

| 门 | 默认 | 超限行为 |
|----|------|----------|
| 单例 wall-clock | 180s | **杀进程 + FAIL（记 timeout）** |
| 单例 token | 60k | 跑后统计 → FAIL（记 over-budget） |
| 单例工具调用数 | 用例声明 | FAIL |
| 整套运行 | 15 min | 停止并出报告 |

> 意义：把"模型无限轮询/全量搜索"从"用户手动 ESC"变成**自动失败**——人工轮暴露的第一号成本问题（`P-01` 400k token、`C-01` 无界轮询、`S-04` 全盘搜索）。

**资产集成本筛查**（人工轮高危用例的提示词处理）：诱导"全量验证/全量搜索/全量测试"的提示词，改窄（限定验证次数/范围）或标 `skip`（保留记录、不自动跑）——资产集 JSON 加 `costGuard: "narrowed"|"skip"` 字段。

---

## 六、用例文件格式（cases/*.json，字段定稿）

```json
{
  "id": "dir-nomatch",
  "group": "failure-signal",
  "prompt": "列一下目录内容并过滤出 js 文件，同时告诉我当前目录",
  "projectRoot": "fixture/empty",          // 工作区 fixture（相对 tools/agent-e2e/）
  "allowWrite": false,
  "repeat": 3,
  "budget": { "wallClockSecMax": 120, "tokensMax": 20000, "toolCallsMax": 4 },
  "mustContain": [],
  "mustNotContain": {
    "command": ["\\\\| head", "ls -"],
    "stdout": ["✗"],
    "errorKind": ["not_found"]
  },
  "costGuard": "narrowed",                 // narrowed = 提示词已改窄 / skip = 不自动跑
  "status": "pending",
  "note": "人工轮 C2：曾误 ✗ + 原样重试"
}
```

- `mustNotContain` 子键对应 §四原语（command/stdout/errorKind）；`budget` 对应成本门
- 用例内容的权威来源是测试记录文档——**资产集是它的机器可执行投影**，两边同步更新
- 只追加不删除；弃用标 `"status": "deprecated"` + note 理由

---

## 七、实施步骤

| 步 | 能力 | 完成判据 |
|----|------|----------|
| **0** | **隔离钩子**：main/CLFLogger/CLFSessionManager 检查 `CLFCODE_TEST_SESSION_DIR`/`CLFCODE_TEST_LOG_DIR` 环境变量（存在则覆盖落点；命名与 CLFCODE_TEST_* 先例一致） | 跑完用例后真实 `doc/contextHistory`、`doc/log` **零新增** |
| 1 | runner 骨架 + 断言库：`tools/agent-e2e/run.ps1`（驱动 → 采集 → 断言 → 三态）+ 最小用例 JSON | 最小用例跑通给出 PASS/FAIL |
| 2 | **成本门**：wall-clock 超时杀进程（Start-Process + WaitForExit 超时 + Stop-Process 树） | 故意跑长任务 → 超时 FAIL 且进程无残留 |
| 3 | MVP 三例（`plat-selfreport`（改窄）/ `dir-nomatch` / `notfound-explain`）+ repeat 与通过率 | 三例出报告，判定可追溯到证据行 |
| 4 | 用例扩充（§测试记录 全量可自动化项） | 全量跑一遍出报告 |
| 5 | **负控**：注入已知缺陷（如坏配置触发已知 FAIL 模式） | harness 必须报 FAIL |
| 6 | 报告归档 + README 指引（README.md 加测试指引段；测试记录文档头部指向资产集） | 文档闭环 |

**实机验证项（审查门第 4 问）**：步骤 0 后实测 `--prompt` 非交互路径与交互路径的**工具确认/中断逻辑一致性**——用 `dir-nomatch` 分别两种模式跑，比对 jsonl 工具序列（附 B 第 1 条反证）。

---

## 八、审查门（pro 填 2026-09-23）

| # | 审查项 | 结论 |
|---|--------|------|
| 1 | 方向是否成立 | ☑ **成立**——三层分工是正确形态；断言原语 + 成本门 + 负控设计正确（亲验：参数表/先例/缺口全部属实） |
| 2 | 更小解 | ☑ 隔离钩子**必须新增**（history/log 落点不在 --config 通路）；但实现改为**环境变量**而非 CLI 参数（CLFCODE_TEST_* 先例、用户面零影响、--help 不文档化测试钩子） |
| 3 | 裁剪 | MVP 三例同意；**顺序调整**：成本门（步骤 2）提到 runner 骨架后——"全量搜索烧钱"是用户第一痛点，硬门最早落地 |
| 4 | 疑问 | `--prompt` 与交互路径一致性 → 实机验证项（步骤 0 后跑 dir-nomatch 双模式比对） |
| 5 | 接口风险 | 环境变量钩子 → 零 CLI/用户面影响；落点覆盖三处（main 装配 / CLFLogger / CLFSessionManager）——实施时逐一确认路径源 |
| 6 | 方向有误 | 否 |

---

## 九、实施记录（待 pro 回填）

| 步 | 状态 | 提交 | 实抓问题 / 裁剪说明 |
|----|------|------|---------------------|
| 0 隔离钩子 | ⬜ | | |
| 1 runner 骨架 + 断言库 | ⬜ | | |
| 2 成本门 | ⬜ | | |
| 3 MVP 三例 | ⬜ | | |
| 4 用例扩充 | ⬜ | | |
| 5 负控 | ⬜ | | |
| 6 报告归档 + README | ⬜ | | |
