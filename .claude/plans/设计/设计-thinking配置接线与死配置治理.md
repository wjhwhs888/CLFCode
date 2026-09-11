# 设计-thinking 配置接线与"死配置"治理

> **状态**：✅ **已定案**（2026-09-11 用户拍板，定案记录见 §八）——按 §四 实施顺序 A→B→C 落码；sub_model 保留标注（方案 D 选 c）
> **创建**：2026-09-09
> **触发**：用户配置 `thinking_level: "max"` 后追问"flash 没有 max 模式吗"——实读源码发现该配置**从未生效**
> **取证方式**：源码实读（引用 `文件:行号`，基于 2026-09-09 工作区状态；2026-09-11 补充取证）+ DeepSeek 官方 API 文档 + dsh 官方 harness 源码（`llm-deepseek`）
> **关联**：P2-6（注释/实现不符）、P2-8（isUsageStreamSupported 硬编码）、C6（ConfigLoader 表驱动，已实施）
> **注意**：行号基于实读时点，实施时以最新代码为准（2026-09-11 补充取证基于 v0.7.4 工作区；缓存命中率功能已上线，见 §七 风险表）
> **2026-09-11 二轮全量核实**：§二/§三 全部代码引用按当前工作区实证通过（`m_thinkingLevel` 三处、§2.1 表 11 行行号、buildChatRequest 字段全集与 2 调用点、wrapUp 副本配置写法、serializeMessage :184-218 无 reasoning、CLFMessage 无 reasoning 字段、StreamAccumulator :87-88、CLFCommands :110/:123、CLFConfigLoader.hpp:34 注释、AgentLoop.cpp:324 注释）；新增发现已入正文——README 双重误导（§3.1）、sub_model 意图证据（§3.2）
> **2026-09-11 三轮设计审查（人审）**：方案 C 补重放空占位规则（§四/§六）、方案 B 补 top_p clamp WARN、新增实施顺序建议（§四）、默认值建议空串（§八.2）、V4 Pro 下线事实已核实（§八.6）
> **2026-09-11 四轮 pro 拍板**：三轮 5 项修改**全部采纳**，其中默认值**定案空串**（§八.2 落定）。补齐 2 处一致性：空占位规则补边界（仅 thinking 开启时需要 + serializeMessage 无 config 的实现注意，§四 步骤 4）、§七 风险表"先验证"残留修正（方案 C 已升级为直接实现）

---

## 一、问题概述

用户配置 `"thinking_level": "max"`，意图让模型以最高思考强度运行。实读源码结论：

**该配置是"死配置"——被解析、被存储、被测试断言，但从未发往 API。** 实际运行行为 = DeepSeek 服务端默认（thinking 开启，effort = **high**），与配置写的 `max` 无关。

三处冲突叠加：
1. **键名冲突**：CLFCode 读 `thinking_level`；官方参数名是 `reasoning_effort`（顶层）/ `thinking.type`（extra_body）——名字根本对不上
2. **值域无校验**：`ConfigFieldType::String` 直接赋值（`CLFConfigLoader.cpp:93-95`），写 `"abc"` 也接受
3. **零消费者**：全仓无任何代码读 `m_thinkingLevel` 去构造请求

---

## 二、取证：配置项 → 实际消费盘点

### 2.1 chat_completions 段

| 配置键 | 默认值（CLFTypes.hpp） | 注册（ConfigLoader.cpp） | 实际消费点 | 结论 |
|---|---|---|---|---|
| `model` | `deepseek-v4-flash` (:83) | :56 | `body["model"]`（ProtocolAdapter.cpp:43） | ✅ 已接线 |
| `sub_model` | `deepseek-v4-pro` (:84) | :57 | **仅 CLFCommands.cpp:110/123 显示** | ⚠️ **死配置（仅显示）** |
| `max_tokens` | `8192` (:85) | :59 | `body["max_tokens"]`（:44）+ S3-2 表覆盖 | ✅ 已接线 |
| `temperature` | `0.0` (:86) | :58 | `body["temperature"]`（:45） | 🟡 已发送但 thinking 模式下官方无效（§三.3） |
| `top_p` | `1.0` (:87) | :60 | `body["top_p"]`（:46） | 🟡 thinking 模式有 0.95 下限（§三.4） |
| `frequency_penalty` | `0.0` (:88) | :62 | 条件发送（:56-58） | 🟡 thinking 模式无效（§三.3） |
| `presence_penalty` | `0.0` (:89) | :63 | 条件发送（:59-61） | 🟡 thinking 模式无效（§三.3） |
| `response_format` | `text` (:90) | :64 | 非 text 时发送（:62-64） | ✅ 已接线 |
| `stop` | `[]` (:91) | :65 | 非空时发送（:65-67） | ✅ 已接线 |
| `stream` | `false` (:92) | :61 | `body["stream"]`（:47）+ include_usage 判定 | ✅ 已接线 |
| `thinking_level` | `"max"` (:93) | :66 | **无** | ❌ **死配置（零消费者）** |

### 2.2 死配置证据链（thinking_level）

```
定义：src/CLFTypes/CLFTypes.hpp:93      std::string m_thinkingLevel = "max";
注册：src/CLFCore/CLFConfigLoader.cpp:66 {"chat_completions", "thinking_level", String, &CLFAgentConfig::m_thinkingLevel},
消费：全仓 grep m_thinkingLevel → 仅上述两处 + 测试断言（qa_CLFConfigLoader.cpp:85）
请求体：src/CLFCore/CLFProtocolAdapter.cpp:37-89 buildChatRequest 字段全集
        = model / max_tokens / temperature / top_p / stream / stream_options.include_usage
        / frequency_penalty / presence_penalty / response_format / stop / messages / tools / tool_choice
        → 无 thinking / reasoning_effort / enable_thinking 任何一项
旁证：src/CLFCore/CLFConfigLoader.hpp:34 注释写着 "enable_thinking": true 示例——设计意图有，实现未接线
```

**结论**：配置 `max` / `low` / 删除，三者在运行期完全等价；实际 effort = 官方默认 **high**。

### 2.3 请求构造调用点盘点（2026-09-11 补充取证）

`buildChatRequest` 生产调用点全仓仅 **2 处**（同一实现、配置同源）：

| 调用点 | 位置 | 说明 |
|---|---|---|
| 主循环请求（流式/同步） | `CLFAgentLoop.cpp:162` | 回合主体，双传输路径复用同一 body |
| wrapUp 触顶收尾请求 | `CLFAgentLoop.cpp:419` | 用 `m_stream=false` 副本配置（A5 教训），同样走 buildChatRequest |

**摘要生成不在此列**——`CLFSessionSummarizer.cpp:97-101` 独立构造请求体（model / max_tokens=1024 / temperature=0.0 / stream=false，无 thinking 相关字段）：

- 方案 A 接线**不影响摘要请求**（不覆盖服务端默认档位）
- 但摘要请求的 `temperature=0.0` **同样落在官方"thinking 模式下无效"规则内**（§三.3）——摘要的确定性意图当前无法通过 temperature 实现；若需确定性摘要，A3 开关可为摘要请求单独下发 thinking 关闭（§四 方案 B 扩展）

---

## 三、硬编码与配置冲突清单（重点）

### 3.1 【严重】请求构件与配置脱节（thinking_level 家族）
- 配置项存在 → 用户以为生效 → 实际无任何效果（静默失效，无日志无告警）
- **README 错误描述叠加（2026-09-11 实读）**：`config/README.md:61` 声称值域 `off/low/medium/high/max`"思考深度递增"且"仅 deepseek-v4-pro 支持"——**双重误导**：① 值域与官方 `reasoning_effort` 不对应（`off` 档不存在，等级语义也是 README 自创）② 配置从未接线（§2.2），README 描述了一个不存在的行为；"仅 pro 支持"随 V4 Pro 9/14 下线彻底失效。用户最初追问"flash 没有 max 模式吗"即由此行误导
- 与 P2-6（BuiltinTools 过时注释）、P2-8（isUsageStreamSupported 硬编码 deepseek.com）同族：
  **"配置/注释/实现三方不一致"**——建议统一纳入一个治理批次

### 3.2 【严重】`sub_model` 死配置（仅显示）
- 定义 `CLFTypes.hpp:84` 默认 `deepseek-v4-pro`；注册 `ConfigLoader.cpp:57`
- 唯一消费：`CLFCommands.cpp:110`（/config 显示"副模型"）、`:123`（/model 提示）
- **设计意图的权威来源（README，2026-09-11 实读）**：`config/README.md:42`"副模型（CLF 自定义）。用于轻量任务"；`:73`"context_compression … 调用 `sub_model` 对历史消息进行摘要压缩（功能待实现）"——即意图 = 轻量任务 + 摘要压缩
- **意图与实现的偏离**：S3-1 摘要已实现（2026-09-02），但 `CLFSessionSummarizer.cpp:98` 用的是**主模型** `m_config.m_modelName`——设计意图从未被实现路径采用；sub_model 至今零调度逻辑
- 影响：用户以为配了副模型会生效，实际永远是摆设

### 3.3 【中】thinking 模式下无效参数（官方明示）
DeepSeek [Thinking Mode 文档](https://api-docs.deepseek.com/guides/thinking_mode/)原文：

> Thinking mode does not support the `temperature`, `presence_penalty`, or `frequency_penalty` parameters. ... setting these parameters will not trigger an error but will also have no effect.

- CLFCode 现状：`temperature` **无条件发送**（`ProtocolAdapter.cpp:45`）；`frequency_penalty`/`presence_penalty` 非 0 才发送（:56-61）
- 因 thinking 默认开启，**用户配置的 `temperature: 0.0` 目前完全无效**（不报错、无效果、无提示）
- 与用户认知冲突：用户以为 temperature=0 保证了确定性
- **摘要请求同样中招**：`CLFSessionSummarizer.cpp:100` 独立构造的 `temperature=0.0` 在 thinking 默认开启下同样无效——摘要输出的确定性意图当前落空（见 §2.3）

### 3.4 【中】top_p 下限差异
同文档：
> `top_p` takes effect in thinking mode, but with a lower bound of `0.95`... In non-thinking mode it is fixed at `1.0` and your value is ignored.

- CLFCode 无条件发送 `top_p`（:46）；用户配 1.0 无碍，但若配 0.8 会被服务端抬到 0.95（无提示）

### 3.5 【高·潜在】`reasoning_content` 回传缺失（400 风险）
同文档（Tool Calls 段）：
> for requests carrying the `tools` parameter, the `reasoning_content` must be fully passed back to the API in all subsequent requests — even for turns where the model did not perform a tool call. If your code does not correctly pass back `reasoning_content`, the API will return a 400 error.

- CLFCode 现状：
  - 流式解析 reasoning_content 仅用于 UI 折叠（`CLFStreamAccumulator.hpp:87-88`）
  - `CLFMessage` 无 reasoning 字段；`serializeMessage`（`ProtocolAdapter.cpp:184-218`）只输出 role/content/tool_call_id/name/tool_calls → **不回传**
  - 而 CLFCode **始终携带 tools**（`ProtocolAdapter.cpp:77-84`，有注册工具时）
- 实机未报 400（用户日常多轮工具调用正常）→ 推测实际 API 容忍度高于文档，或规则适用范围有限
- **dsh 官方实现取证（2026-09-11，决定性证据）**——官方 harness 将回传作为规则实现：
  - `llm-deepseek/src/serialize.ts:233`：序列化时回传 `reasoning_content`
  - `llm-deepseek/tests/serialize.spec.ts:102/119`：测试名直书 `passes reasoning_content back on tool-call-free turns` / `(official passback rule)`——**无工具调用的轮次也回传**
  - `docs/config-catalog.md:1302`：重放消息在 thinking 开启时需要空 `reasoning_content`
- **结论升级**：官方客户端以规则级实现回传 → CLFCode 不回传是**真实风险缺口**，不再只是"文档要求"。实机未报 400 仅说明当前 API 容忍，不可依赖
- **建议改为：直接实现回传**（原方案 C 的"先验证再决定"降级为可选实机确认）
- 代码注释佐证已知思考通道：`CLFAgentLoop.cpp:324` "思考模型可能在 reasoning_content 中耗尽 token"

### 3.6 【低】硬编码默认值与官方默认不一致
- `m_thinkingLevel = "max"`（:93）——代码默认写 max，官方默认 high；因未接线，二者都不影响实际
- `m_stream = false`（:92）与用户配置 true——正常覆盖，无冲突

---

## 四、修改方案（分步，可独立排期）

### 方案 A：thinking 接线（核心，最小改动）

**A1. 参数下发**（`CLFProtocolAdapter.cpp` buildChatRequest）
```cpp
// 思考强度（官方：reasoning_effort = low/high/max；默认 high）
// 值域校验后下发；非法值 → 回落默认并 WARN（不静默）
if (!config.m_thinkingLevel.empty() && isValidEffort(config.m_thinkingLevel)) {
    body["reasoning_effort"] = config.m_thinkingLevel;
}
```

**A1 下发范围（2026-09-11 取证定案）**：`buildChatRequest` 生产调用点仅 2 处（主循环 :162 + wrapUp :419），同一实现——接线后 effort 随配置进入两处，无需逐点选择。摘要请求独立构造、不受影响（§2.3）。

**默认值定案（接线前置决策，见 §八 待确认 2）**：当前默认 `"max"`（`CLFTypes.hpp:93`）非空且合法，接线后会让**所有未配置用户**从服务端默认 high 升到 max（成本/延迟上升）。建议默认改为空串（不覆盖服务端默认）或 `"high"`（显式对齐官方），**不建议保留 `"max"`**。

**A2. 值域与别名**（建议放 CLFTypes 或 adapter 私有静态）
- 官方映射表：`minimal→low, low→low, medium→high, high→high, xhigh→high, max→max, ultra→max`
- 建议：接受官方全别名并归一化（用户可写 `ultra`）；非法值 → 警告日志 + 用默认 high

**A3. 开关支持**（可选，建议同批）
- 现状：无法关闭思考（官方默认开启）
- 方案：新增 `thinking_enabled`（bool，默认 true）→ `body["thinking"] = {"type": "enabled"/"disabled"}`
- 或：`thinking_level: "disabled"` 特例值（不加新键，但语义混用不推荐）

**A4. 配置键名兼容**
- 现键 `thinking_level` 与官方 `reasoning_effort` 不同名——保留 `thinking_level` 为主键（用户已在用）并在 `config/README.md` 注明对应关系；或新增 `reasoning_effort` 别名（新键优先）
- ❌ 不建议直接改名（破坏已有配置）

### 方案 B：参数条件下发（消除无效/误导参数）

- 需要"thinking 是否开启"的判定输入（来自 A3 或默认 true）
- 规则（照官方语义）：
  - thinking 开启时：**省略** `temperature`/`frequency_penalty`/`presence_penalty`；`top_p` clamp 到 `>=0.95`（**审查补充 2026-09-11**：clamp 实际生效时打 WARN——静默改写用户配置值不友好）
  - thinking 关闭时：按现有逻辑发送全部
- 收益：请求体不再携带无效参数；`/config` 可提示"思考模式下 temperature 不生效"
- 风险：改动请求体构造需回归（T10/T11 系列 + 协议适配器测试）

### 方案 C：reasoning_content 回传（dsh 取证后：直接实现）

**dsh 官方实现已取证确认回传是规则**（§三.5：`serialize.ts:233` 回传 + spec 名 "official passback rule"，含无工具调用轮）——不再需要"先验证是否真需要"，剩余可选动作仅是实机确认当前 API 容忍度（1 小时内可做）。
- **实现步骤**（原"确认需要回传"分支）：
  1. `CLFMessage` 加 `m_reasoningContent`（注意：公共结构改动 → 干净重建 + 主程序冒烟，A2 教训）
  2. `CLFStreamAccumulator` 已有解析（:87-88）→ AgentLoop 构造 assistant 消息时带上
  3. `serializeMessage` 在 assistant 分支输出 `reasoning_content`
  4. **【审查补充 2026-09-11】重放/恢复场景的空占位规则**：dsh 取证（§三.5 `docs/config-catalog.md:1302`）
     表明重放消息在 thinking 开启时需携带**空 `reasoning_content`**——序列化时对"无 reasoning 的历史
     assistant 消息"（会话恢复 / jsonl 未落盘 reasoning 的场景）输出 `reasoning_content: ""` **空串占位**，
     而非省略字段；否则恢复会话后仍可能触发 400
     **【pro 拍板补充】边界与实现注意**：① 空占位仅当**请求 thinking 开启**时需要（dsh 规则原文
     "while reasoning is on"）——现状 thinking 恒开；若 A3 开关同批，占位须与开关联动；
     ② `serializeMessage` 当前无 config 参数（§五 已注"无签名改动"），实现时需让其感知 thinking
     状态（加参数或由 buildChatRequest 分层处理），实施期定
  5. 需评估：上下文体积增大（reasoning 进上下文）、jsonl 是否持久化（建议**不落盘**，仅请求期携带 + 第 4 条空占位兜底）、与摘要/压缩的交互
- **降级路径（仅当用户选择暂不实现时）**：在协议适配器加注释记录"官方实现回传 + 文档要求回传，CLFCode 暂未实现；若未来 400 优先查此处"——比原"待验证"表述更准确

### 方案 D：死配置治理（sub_model + thinking_level 归属）

**✅ 定案（2026-09-11 用户拍板）**：`sub_model` 选 **c) 保留并标注**——用户判断后续可能有用（轻量任务分流），保留配置键与显示，但消除误导：
- `/config` 显示改为"副模型（预留，未接线）"（`CLFCommands.cpp:110/:123`）
- README 注明"预留，未接线"（`config/README.md:42`）
- 默认值 `deepseek-v4-pro` 过时问题随"模型名同步小批"处理

`thinking_level`：由方案 A 接线后不再是死配置。

### 实施顺序建议（设计审查 2026-09-11）

```
A（接线 + 值域校验）→ B（参数条件下发 + 摘要请求温度处理）→ C（reasoning 回传，独立批次）→ D（sub_model 裁决）
```
- A 消除静默失效，收益即时、改动最小
- B 紧随 A（依赖"thinking 是否开启"的判定输入）
- C 改动面最大（公共结构 + jsonl 交互 + 恢复场景），独立批次、干净重建
- D 待用户定义意图 + 模型下线事实明确后一次性裁决

---

## 五、改动点清单（引用源码）

| 文件 | 位置 | 改动 |
|---|---|---|
| `src/CLFCore/CLFProtocolAdapter.cpp` | `buildChatRequest` :37-89 | A1 加 `reasoning_effort`；A3 加 `thinking`；B 条件省略无效参数 + top_p clamp |
| `src/CLFCore/CLFProtocolAdapter.hpp` | :41-45 | 无签名改动（config 已传入） |
| `src/CLFTypes/CLFTypes.hpp` | :93 | 默认值语义修订（"max" → 与官方默认对齐或明确"未接线时无效果"注释）；A3 加 `m_thinkingEnabled` |
| `src/CLFCore/CLFConfigLoader.cpp` | :66（+ 新键注册） | A4 别名键/新键；值域校验可放此层（或 adapter） |
| `src/CLFTypes/CLFTypes.hpp` + `CLFProtocolAdapter.cpp:184-218` | CLFMessage / serializeMessage | 方案 C（dsh 已证实回传为规则） |
| `src/CLFCore/CLFSessionSummarizer.cpp` | :97-101 | 方案 B 扩展：摘要请求 `temperature=0.0` 在 thinking 模式下无效——省略并注释，或（若 A3 同批）显式关闭 thinking 恢复确定性语义 |
| `src/CLFUI/CLFCommands.cpp` | :110/:123 | 方案 D（定案 c）：显示措辞改"副模型（预留，未接线）"；B 可加"思考模式下 temperature 无效"提示 |
| `config/README.md` | :61（:41-42/:98/:111-112 属"模型名同步小批"范围） | **修正错误描述**：thinking_level 值域改为官方对应档位、移除"仅 pro 支持"与 `off` 档；补"思考模式下 temperature 等参数无效"说明；sub_model 标注"预留，未接线"（定案 c） |
| `src/test/qa_CLFProtocolAdapter.cpp` | 现有 T10/S3-2 系列 | 新增断言 |

---

## 六、测试计划

- **qa_CLFProtocolAdapter**（+4）：
  1. `thinking_level: "max"` → body 含 `"reasoning_effort": "max"`
  2. 别名 `"ultra"` → 归一化为 `"max"`；非法 `"abc"` → 不下发 + 日志 WARN
  3. `thinking_enabled: false` → body 含 `"thinking": {"type":"disabled"}`（且按 B 规则恢复 temperature 下发）
  4. thinking 开启时 body **不含** temperature/frequency_penalty/presence_penalty；`top_p: 0.8` → clamp 0.95
- **qa_CLFConfigLoader**（+2）：新键/别名解析；非法值不被静默接受（若校验放配置层）
- **方案 C 专项（审查补充 2026-09-11）**：qa_CLFProtocolAdapter +3——① assistant 带 reasoning → 序列化含 `reasoning_content` 原文；② assistant 无 reasoning（恢复场景）→ 序列化含**空串占位**（不是缺字段）；③ 无 tools 请求不受影响（规则边界确认，可选）
- **回归**：T10/T11 系列、S3-2 include_usage 三态、缓存命中率用例（同文件已上线，改动时注意不破坏既有断言）
- **实机**：`/config` 核对；一次真实请求抓 body（或看日志）确认参数下发；观察 reasoning 长度/耗时变化（max vs high）

---

## 七、风险与取舍

| 风险 | 说明 | 缓解 |
|---|---|---|
| effort=max 成本/延迟上升 | max 档思考更长 → 输出 token 变多、响应变慢 | 用户自行选择；建议 `/config` 显示当前档位 |
| 请求体改动回归 | buildChatRequest 被流式/同步/收尾多路径复用 | 全量 ctest + 双配置（stream true/false）冒烟（A5 教训） |
| 方案 C 公共结构改动 | CLFMessage 改动波及面大（含 jsonl/摘要/测试） | dsh 已证实回传为规则（§三.5）——独立批次 + 干净重建 + 主程序冒烟（A2 教训） |
| 与近期改动共享文件 | 缓存命中率功能已上线（v0.7.1，`m_usageCacheHit`/`m_hasCacheField` 在 `CLFAssistantResponse`）——本文档初稿取证基于其实施期 | 改动同一文件（ProtocolAdapter / StreamAccumulator）时以最新代码为准；当前测试基线 31 套件全绿 |

---

## 八、定案记录（2026-09-11 用户拍板）

| # | 事项 | 定案 |
|---|---|---|
| 1 | 方案 A（接线 + 值域校验 + 别名归一化） | ✅ **做**——A1/A2/A4 按 §四 实施；A4 选"保留 `thinking_level` 为主键 + README 注明对应关系"（不新增别名键，最小改动） |
| 2 | 默认值 | ✅ **空串**——不干预服务端默认，官方默认演进时自动跟随（§四 A1 注） |
| 3 | A3 开关（`thinking_enabled`） | ✅ **与 A 同批**——摘要确定性需求（§2.3）为第二消费方 |
| 4 | 方案 B（参数条件下发 + top_p clamp WARN + 摘要 temperature 处理） | ✅ **紧随 A**（依赖 A3 的开关判定输入） |
| 5 | 方案 C（reasoning_content 回传） | ✅ **直接实现**（dsh 已证实回传为规则，§三.5）；**独立批次**（公共结构改动 + jsonl 交互 + 空占位规则） |
| 6 | 方案 D（sub_model） | ✅ **c) 保留并标注**——用户判断后续可能有用；/config 显示"副模型（预留，未接线）" + README 注明；默认值过时问题随"模型名同步小批"。**附带待查**：主模型 `deepseek-v4-flash` 的生命周期（是否同批下线 → 配置需改 `v4.1-flash`） |
| 7 | P2-6/P2-8 治理合并 | ✅ 剩余 P2-8 项随本批顺手处理（实施时核对状态；P2-6 已修、include_usage host 判定已由 S3-2 落地） |

**实施顺序**（§四 定案）：`A（含 A3）→ B → C（独立批次）→ D 随批`。实机验证：V4 Pro 下线前后各观察一轮 reasoning 长度/耗时（max vs 默认档）。
