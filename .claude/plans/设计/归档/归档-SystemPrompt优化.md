# 设计-System Prompt 优化

> **状态**：设计中
> **创建**：2026-08-12

---

## 一、Context（背景）

CLFCode 的 system prompt 是模型获取身份、环境、行为规则的核心通道。当前实现在 v0.1.0 阶段完成基础框架，之后仅在 skill 加载和 Resume 恢复上做了增量。经过 5 个小版本的迭代，system prompt 的结构和注入方式已经稳定，但也暴露了一些可以提升的地方。

---

## 二、现状分析

### 2.1 当前架构

```
CLFAgentLoop 构造 / clearContext() / restoreSession()
  │
  ▼
injectSystemPrompt()
  │
  ├── ① 硬编码身份提示（~15 行 C++ 字符串）
  │      - "你是 CLFCode，一个本地运行的 AI Coding Agent"
  │      - 后端 API 品牌声明
  │      - 语言指令（使用中文）
  │
  ├── ② 硬编码运行环境规则（~8 行）
  │      - Windows 命令约束
  │      - GBK 编码提示
  │
  ├── ③ 硬编码文件管理规则（~5 行）
  │      - 临时文件清理
  │      - 复用已有文件
  │
  └── ④ L1 编码宪法（从 data/skills/constitution.md 文件加载）
         - 最小修改原则
         - 安全边界
         - 代码质量标准
         - 输出规范
  │
  ▼
m_context.addMessage("system", prompt)   ← 第 1 条 system 消息

/skill <name> 命令
  │
  ▼
injectSkillToContext(name, content)
  │
  ▼
m_context.addMessage("system", skillContent)  ← 第 2~N 条 system 消息
```

### 2.2 已有保护机制（✅ 已完成）

| 机制 | 实现位置 | 说明 |
|------|----------|------|
| System 永不截断 | `CLFContext::getMessages()` | system 消息优先保留，非 system 从新到旧截断 |
| Resume 时重建 system | `CLFAgentLoop::restoreSession()` | 跳过旧 system，从文件重新注入（保证 skill 更新生效） |
| L1 编码宪法常驻 | `injectSystemPrompt()` | 始终追加到第一条 system 消息末尾 |
| L2/L3 按需注入 | `injectSkillToContext()` | `/skill <name>` 命令触发 |

### 2.3 当前缺失

| 缺失项 | 影响 |
|--------|------|
| **无动态上下文** | 模型不知道当前 git 分支、工作目录内容、OS 详细信息，第一轮对话必须先跑工具才能了解环境 |
| **无项目级规则** | 只有全局 `constitution.md`，无法针对不同项目定制行为（如 A 项目 C++17，B 项目 Python） |
| **身份提示硬编码** | 修改身份描述、添加规则需要改 C++ 代码重编译 |
| **多条 system 消息分散** | 基础身份 + 每个 skill 各自一条 system 消息，数量增长无控制 |
| **无 system 区 token 预算** | 虽然当前窗口 1M 足够大，但缺乏防护机制，未来可能膨胀 |

---

## 三、设计目标

1. **动态上下文注入** — 每个 turn 自动注入 git 状态 / 工作区关键信息，消除"先问后知"的浪费
2. **项目级规则支持** — 检测工作目录下的 `PROJECTRULES.md`（或复用 `CLAUDE.md`），作为项目专属 system prompt
3. **系统提示词模板化** — 身份 / 环境 / 规则从文件加载，用户可编辑，不需要重编译
4. **System 消息合并** — 所有 system 内容合并为一条消息，避免语义分散
5. **Token 预算保护** — 设置 system 区最大占比，超出时对低优先级内容截断

---

## 四、设计方案

### 4.1 整体架构变更

```
CLFAgentLoop 构造 / clearContext() / restoreSession()
  │
  ▼
CLFSystemPromptBuilder::build(config, workspaceRoot)
  │
  ├── ① 身份模板（从文件加载，降级到硬编码）
  ├── ② 动态上下文（git 状态 + 工作区快照）
  ├── ③ 项目规则（检测 PROJECTRULES.md / CLAUDE.md）
  ├── ④ L1 编码宪法（constitution.md）
  ├── ⑤ 已注入的 L2/L3 skill 内容（如有）
  └── ⑥ Token 预算检查 + 截断
  │
  ▼
单条 system 消息  →  m_context
```

### 4.2 新增文件

```
src/CLFCore/CLFSystemPromptBuilder.hpp   — System prompt 构建器接口
src/CLFCore/CLFSystemPromptBuilder.cpp   — 实现
config/system_prompt_template.md          — 可编辑的身份/环境/规则模板
```

#### `config/system_prompt_template.md` 结构

```markdown
## 身份
你是 CLFCode，一个本地运行的 AI Coding Agent。
当前模型：{{model_name}}。
你的后端 API 由 DeepSeek 提供，但你是独立的 Agent 产品。
你永远不应自称 Claude、OpenAI、Anthropic 或其他 AI 品牌。

## 语言
请始终使用 {{interaction_language}} 与用户交流。

## 运行环境
- 操作系统：{{os_info}}
- 工作目录：{{workspace_root}}
- Shell：{{shell_info}}
- 禁止使用 Linux 命令（ls/pwd/cat/grep/find/head/tail/iconv），除非明确告知当前是 Linux 环境
- 中文 Windows 的命令输出可能是 GBK 编码，遇到乱码先执行 `chcp 65001`

## 项目信息
{{project_context}}

## 文件管理规则
- 任务中创建的临时文件，任务结束前必须清理
- 优先复用已有文件，避免重复创建备份
- 尽量用重定向/管道而非落盘中间文件
```

模板中 `{{变量}}` 在构建时替换为实际值。

### 4.3 动态上下文：Git 状态快照

**注入时机**：`CLFSystemPromptBuilder::build()` 调用时（构造 + `/clear` + `/skill` + resume）
**内容**：分支名、最近 5 条提交摘要、工作区变更文件列表、捕获时间戳

**惰性刷新策略**（避免陈旧 + 避免频繁执行 git 命令）：

| 触发条件 | 行为 |
|----------|------|
| 首次调用 | 执行 git 命令，缓存结果 + 时间戳 |
| 距上次捕获 < 30s | 复用缓存 |
| 距上次捕获 ≥ 30s | 重新执行 git 命令，更新缓存 |
| `clearContext()` | 强制刷新（下次 build 时重新捕获） |
| `/skill` 命令 | 触发 rebuildSystemMessage()，Git 状态由 build() 内部 TTL 决定是否刷新 |

**格式**（注入到 `{{project_context}}` 变量）：

```
- Git 分支：master
- 最近提交：
75e18d3 docs: README 顶部添加一键安装命令
6bd58f6 feat: 模式切换提示 + v0.1.4 release
- 工作区状态：干净（无未提交变更）
（Git 状态捕获于 14:32:01，如需实时状态请使用 execute_command 查询）
```

末尾的时间戳免责声明让模型知道信息可能存在延迟，避免误判。

**实现方式**：
- 新增 `CLFSystemPromptBuilder::captureGitStatus(workspaceRoot)` 私有方法
- 执行 `git log --oneline -5` + `git status --short`
- **跨平台 `popen` 处理**：`#ifdef _WIN32` → `_popen()` / `_pclose()`，否则 → `popen()` / `pclose()`
- 超时 3 秒，失败时静默返回空（不影响 Agent 启动）
- Builder 内部维护静态缓存：`{workspaceRoot, gitInfo, captureTime}`，TTL 30 秒
- **`build()` 内部自动检查 TTL**：无论是 `injectSystemPrompt()` 还是 `rebuildSystemMessage()` 调用，`build()` 内部都自动检查缓存是否过期，**调用者无需传递"强制刷新"参数**

**`{{project_context}}` 变量的完整组成**：
```
{{project_context}} =
    [Git 状态块]          ← captureGitStatus() 生成，可能为空（非 git 仓库）
  + [项目规则块]          ← loadProjectRules() 生成，可能为空（无规则文件）
```

两个子块由 `build()` 内部拼接，如果两者都为空，`{{project_context}}` 替换为空字符串（模板中的 "## 项目信息" 标题仍然保留，下方无内容）。

### 4.4 项目级规则

**加载优先级**（先找到的生效）：
1. `$WORKSPACE_ROOT/PROJECTRULES.md` — CLFCode 专属项目规则
2. `$WORKSPACE_ROOT/CLAUDE.md` — 兼容 Claude Code 项目规范

**边缘情况处理**：

| 情况 | 行为 |
|------|------|
| PROJECTRULES.md 存在且非空 | 读取内容（上限 5000 字符，超出截断加标记） |
| PROJECTRULES.md 存在但为空 | **不降级**到 CLAUDE.md（空文件是用户有意为之） |
| PROJECTRULES.md 不存在，CLAUDE.md 存在 | 读取 CLAUDE.md |
| 两者都不存在 | 跳过，不产生空段 |
| 文件 > 5000 字符 | 截断 + `[…项目规则超过5000字符，已截断]` 标记 |

> **命名说明**：`PROJECTRULES.md` 是 CLFCode 推荐的项目规则文件名。`CLAUDE.md` 仅作为兼容性 fallback，方便已有 Claude Code 项目规则的用户无缝迁移。
>
> **篇幅建议**：文件内容随每轮对话注入 system prompt，建议控制在 **128 行**以内（约 5000 字符）。超出部分会被截断并标记，模型无法感知。

**注入位置**：system prompt 中 `{{project_context}}` 变量的下半部分（git 信息之后）

**内容格式**：
```
## 项目规则（来自 PROJECTRULES.md）
<文件内容>
```

### 4.5 模板降级策略

```
尝试读取 config/system_prompt_template.md
  │
┌───┴───┐
▼       ▼
存在    不存在 / 读取失败
│       │
▼       ▼
使用模板   使用硬编码默认模板（当前 injectSystemPrompt 中的内容）
│
▼
变量替换（os_info, workspace_root, project_context 等）
```

**默认模板**内置于 `CLFSystemPromptBuilder.cpp`，与当前 `injectSystemPrompt()` 输出完全一致，保证向后兼容。

### 4.6 System 消息合并

**当前**：基础身份 + 每个 skill 各一条 system 消息，共 1+N 条
**改为**：`CLFSystemPromptBuilder` 将所有内容拼接为一条 system 消息

**变更点**：
- `injectSkillToContext()` 不再直接 `addMessage("system", ...)`，而是将 skill 内容存入 `m_injectedSkills` map
- 下次 `injectSystemPrompt()` 时将所有已注入 skill 一起合并到 system 消息中
- 如果 skill 在运行时动态注入（非启动阶段），需要重新构建 system 消息（替换而非追加）

**System 消息生命周期**：
- 永远只有 **1 条** system 消息，位于 `m_messages[0]`
- `/skill <name>` 时，更新 `m_injectedSkills`，重新构建 system 消息，替换 `m_messages[0]`
- `/clear` 时，清空 `m_injectedSkills` 但保留 L1 常驻部分

### 4.7 Token 预算保护

```
system 区最大 token 数 = maxContextWindow × systemTokenRatio
                       = 1M × 0.3 = 300K token（默认）

systemTokenRatio 在 agent_settings.json 中可配置（agent.system_token_ratio，默认 0.3）
```

**构建顺序**（优先级从高到低，不可变）：

| 优先级 | 内容 | 截断策略 |
|:------:|------|----------|
| ① | 身份模板 | 永不截断（体积固定 ~500 chars） |
| ② | 动态上下文 | 永不截断（体积固定 ~200 chars） |
| ③ | 项目规则（PROJECTRULES.md / CLAUDE.md） | 永不截断（上限 5000 chars） |
| ④ | L1 编码宪法（constitution.md，强制加载） | 永不截断（体积固定 ~1K） |
| ⑤ | L2 skill 1 | 超出预算时从此处开始丢弃 |
| ⑥ | L2 skill 2 | … |
| ⑦ | L3 skill N | 最先被丢弃 |

**截断粒度**：**按完整 skill 丢弃，不拦腰砍内容**。原因是截断一半的 Markdown 会导致模型解析失败。

丢弃策略：
1. 累计 token（①→④ 必保留）
2. 按加载顺序逐一累加 skill，超过预算时停止
3. 被丢弃的 skill 在末尾生成标记

截断标记格式：
```
[system prompt 超出 token 预算（限制 300K），以下 2 条 skill 规则未注入：debug, unittest]
```

**可配置项**（`config/agent_settings.json` 的 `agent` 块新增）：

```json
"system_token_ratio": 0.3   // system 区占上下文窗口的最大比例，范围 0.1~0.5
```

### 4.8 性能优化：资源缓存策略

两个高频率访问的资源需要缓存，避免重复磁盘 I/O：

#### 4.8.1 L1 宪法缓存（constitution.md）

```
CLFSystemPromptBuilder 内部维护静态缓存:
  struct { std::string content; std::filesystem::file_time_type mtime; }

build() 调用时:
  ① 检查文件 mtime
  ② 若未变化 → 复用缓存的 content（O(1) 返回）
  ③ 若已变化 → 重新读取文件，更新缓存

首次调用 → 读文件（~1KB，冷启动一次）
后续调用 → 仅 stat() 检查 mtime（~μs 级）
```

constitution.md 虽然只有 ~1KB，但在 `/skill` 触发 `rebuildSystemMessage()` 时可能频繁调用。加缓存后只需一次 stat() 系统调用。

#### 4.8.2 System Prompt 内容去重（setSystemPrompt）

```cpp
void CLFContext::setSystemPrompt(const std::string& content) {
    auto it = std::find_if(m_messages.begin(), m_messages.end(),
        [](const CLFMessage& m) { return m.m_role == "system"; });
    if (it != m_messages.end()) {
        if (it->m_content == content) return;  // 内容未变，跳过
        it->m_content = content;
    } else {
        m_messages.insert(m_messages.begin(), {"system", content});
    }
}
```

**去重的意义**：Git 时间戳变化 ≠ system prompt 内容变化。如果 Git 状态在 TTL 内（缓存命中），每次 `build()` 输出完全相同。去重避免无意义的 `m_content` 赋值和内存分配。

## 五、详细实施步骤

### Step 1：创建模板文件 `config/system_prompt_template.md`

**操作**：新建文件，内容为当前 `injectSystemPrompt()` 中硬编码文本 + 变量占位符。

**变量列表**：

| 变量 | 来源 | 示例值 |
|------|------|--------|
| `{{interaction_language}}` | `m_config.m_interactionLanguage` | `zh-CN` → "中文" |
| `{{os_info}}` | 运行时检测 | `Windows 11 Pro 10.0.26200` |
| `{{workspace_root}}` | `CLFConfigLoader::findProjectRoot()` | `E:/project/CLFCode` |
| `{{shell_info}}` | 运行时检测 | `Git Bash (bash)` / `cmd.exe` |
| `{{project_context}}` | git 状态 + 项目规则文件 | 动态生成 |
| `{{skills}}` | `CLFSystemPromptBuilder` 内部，`applyTokenBudget()` 的输出 | L1 宪法 + 已注入的 L2/L3 skill 内容（token 预算内） |
| `{{model_name}}` | `m_config.m_modelName` | 用于身份段，告知模型当前使用的后端模型 |

### Step 2：实现 `CLFSystemPromptBuilder`

**头文件** `src/CLFCore/CLFSystemPromptBuilder.hpp`：

```cpp
namespace CLF::CLFCore {

class CLFSystemPromptBuilder {
public:
struct Context {
  std::string workspaceRoot;
  std::string interactionLanguage;
  std::string modelName;
  std::vector<std::pair<std::string, std::string>> skills; // name → content
  int maxContextWindow = 1048576;
};

// 构建完整 system prompt（单条消息内容）
static std::string build(const Context& ctx);

private:
    static std::string loadTemplate();
    static std::string defaultTemplate();
    static std::string loadConstitution();
    static std::string captureGitStatus(const std::string& workspaceRoot);
    static std::string loadProjectRules(const std::string& workspaceRoot);
    static std::string detectOsInfo();
    static std::string detectShellInfo();
    static std::string applyTokenBudget(const std::string& prompt, int maxTokens);
};
```

**实现要点**：

| 方法 | 实现 |
|------|------|
| `loadTemplate()` | 读取 `config/system_prompt_template.md`，失败返回 `defaultTemplate()` |
| `defaultTemplate()` | 硬编码当前 `injectSystemPrompt()` 的输出（向后兼容） |
| `loadConstitution()` | 从 `data/skills/constitution.md` 加载，维护静态缓存 + mtime 检测，文件未变则直接返回缓存 |
| `captureGitStatus()` | `#ifdef _WIN32` → `_popen()` / `_pclose()`，否则 → `popen()` / `pclose()`。执行 `git log --oneline -5` + `git status --short`，超时 3s，失败返回空。`build()` 内部自动检查 TTL，调用者无需传参数 |
| `loadProjectRules()` | 依次检测 `PROJECTRULES.md` → `CLAUDE.md`，读取第一个存在的（空文件不降级）。限制 5000 字符，超出截断 + `[…已截断]` 标记 |
| `detectOsInfo()` | Windows：读注册表或 `ver` 命令输出；Linux/Mac：`uname -a` |
| `detectShellInfo()` | 读环境变量 `SHELL` / `COMSPEC` |
| `applyTokenBudget()` | 按完整 skill 丢弃（L3 优先），不截断身份/环境/项目规则/L1 宪法 |

### Step 3：改造 `CLFAgentLoop`

**修改点**：

1. **`injectSystemPrompt()` → 改为调用 `CLFSystemPromptBuilder::build()`**

```cpp
// 修改前
void CLFAgentLoop::injectSystemPrompt() {
 std::string prompt = "你是 CLFCode…"; // 硬编码 30+ 行
 // ... 加载 constitution.md 追加
 m_context.addMessage("system", prompt);
}

// 修改后
void CLFAgentLoop::injectSystemPrompt() {
    auto ctx = buildSystemPromptContext();
    m_context.setSystemPrompt(CLFSystemPromptBuilder::build(ctx));
}
```

2. **`injectSkillToContext()` → 改为记录到 `m_loadedSkills` 后重建 system 消息**

```cpp
// 修改前
void CLFAgentLoop::injectSkillToContext(const std::string& name, const std::string& content) {
 m_context.addMessage("system", "[Knowledge: " + name + "]\n\n" + content);
 m_loadedSkills.push_back(name);
}

// 修改后
void CLFAgentLoop::injectSkillToContext(const std::string& name, const std::string& content) {
 if (std::find(m_loadedSkills.begin(), m_loadedSkills.end(), name) != m_loadedSkills.end()) {
     return; // 已注入，跳过
 }
 m_loadedSkills.push_back(name);
 // 重建 system 消息（替换 m_messages[0]）
 rebuildSystemMessage();
}
```

3. **新增 `rebuildSystemMessage()` 辅助方法**

```cpp
void CLFAgentLoop::rebuildSystemMessage() {
    // 通过 CLFContext 提供的专用接口替换 system 消息，不直接操作内部 vector
    // 复用 injectSystemPrompt 中的 ctx 构建逻辑
    auto ctx = buildSystemPromptContext();
    m_context.setSystemPrompt(CLFSystemPromptBuilder::build(ctx));
}
```

`setSystemPrompt()` 是 CLFContext 提供的封装接口（见 Step 4），替换 `m_messages` 中第一条 system 消息并刷新内部 token 计数，不暴露内部数据结构。

4. ~~**新增成员变量 `m_systemPromptContent`**~~ — 已移除。`setSystemPrompt()` 内部已做字符串比较去重（`if (it->m_content == content) return;`），AgentLoop 层不需要再维护一份缓存，避免双份状态不一致。

### Step 4：更新 `CLFContext`

**新增接口**（不暴露内部 vector，保持封装性）：

```cpp
// CLFContext.hpp 新增

// 设置/替换 system 消息为单条（合并模式下 system 区始终只有 1 条）
// - 若 m_messages 中已有 system 消息，替换第一条的 content
// - 若没有 system 消息，在 m_messages 头部插入
// - 同时刷新内部 m_systemTokenCount
void setSystemPrompt(const std::string& content);

// 移除所有 system 消息（/clear 时使用，非 system 消息保留）
void removeSystemMessages();
```

**实现要点**：

```cpp
// CLFContext.cpp
void CLFContext::setSystemPrompt(const std::string& content) {
    // 查找第一条 system 消息
    auto it = std::find_if(m_messages.begin(), m_messages.end(),
        [](const CLFMessage& m) { return m.m_role == "system"; });
    if (it != m_messages.end()) {
        if (it->m_content == content) return;  // 内容未变，跳过（避免无意义的内存分配）
        it->m_content = content;
    } else {
        m_messages.insert(m_messages.begin(), {"system", content});
    }
}

void CLFContext::removeSystemMessages() {
    m_messages.erase(
        std::remove_if(m_messages.begin(), m_messages.end(),
            [](const CLFMessage& m) { return m.m_role == "system"; }),
        m_messages.end());
}
```

**现有 `getMessages()`** — system 收集逻辑不变（已有 "system 优先保留" 的正确实现），合并为单条后遍历更简单。

### Step 5：模板文件默认内容

**默认 `config/system_prompt_template.md`**（与当前行为完全一致，只是变量化）：

```markdown
## 身份
你是 CLFCode，一个本地运行的 AI Coding Agent。
当前模型：{{model_name}}。
你运行在用户本地机器上，具备文件读写、命令执行、网络调用等工具能力。
你的后端 API 由 DeepSeek 提供，但你是独立的 Agent 产品。
你永远不应自称 Claude、OpenAI、Anthropic 或其他 AI 品牌。

## 语言
请始终使用 {{interaction_language}} 与用户交流。

## 运行环境
{{os_info}}

## 工作区
{{project_context}}

## 文件管理规则
- 任务中创建的临时文件（备份、中间输出等），任务结束前必须清理
- 优先复用已有文件，避免重复创建备份
- 尽量用重定向/管道而非落盘中间文件

## 行为准则
{{skills}}
```

### Step 6：更新 CMakeLists.txt

在 `src/CLFCore/CMakeLists.txt`（或 `src/CMakeLists.txt`）中加入新文件：

```cmake
src/CLFCore/CLFSystemPromptBuilder.cpp
src/CLFCore/CLFSystemPromptBuilder.hpp
```

### Step 7：更新配置文档

`config/README.md` 中新增对 `system_prompt_template.md` 的说明，告知用户可以编辑该文件自定义 Agent 行为。

---

## 六、文件变更清单

| 操作 | 文件 | 说明 |
|------|------|------|
| **新增** | `src/CLFCore/CLFSystemPromptBuilder.hpp` | Builder 接口 |
| **新增** | `src/CLFCore/CLFSystemPromptBuilder.cpp` | Builder 实现 |
| **新增** | `config/system_prompt_template.md` | 可编辑模板 |
| **修改** | `src/CLFCore/CLFAgentLoop.hpp` | 新增 `buildSystemPromptContext()` + `rebuildSystemMessage()` |
| **修改** | `src/CLFCore/CLFAgentLoop.cpp` | `injectSystemPrompt()` 改为调用 Builder；`injectSkillToContext()` 改为重建模式 |
| **修改** | `src/CLFCore/CLFContext.hpp` | 新增 `setSystemPrompt()` + `removeSystemMessages()` 接口（不暴露内部 vector） |
| **修改** | `src/CLFCore/CLFContext.cpp` | 实现 `setSystemPrompt()`（含去重判断）/ `removeSystemMessages()` |
| **修改** | `src/CMakeLists.txt` | 加入新源文件 |
| **修改** | `config/README.md` | 添加模板文件说明 + PROJECTRULES.md 命名说明（推荐 vs CLAUDE.md 兼容） |
| **可选** | `doc/architecture_design.md` | 更新架构图 |

---

## 七、兼容性保证

| 项目 | 策略 |
|------|------|
| 模板文件缺失 | 降级到硬编码默认模板，行为与当前完全一致 |
| Git 未安装 / 非 git 仓库 | `captureGitStatus()` 返回空，不影响启动 |
| 项目规则文件不存在 | 跳过，不产生空段 |
| 旧版 session 文件 Resume | `restore()` 跳过旧 system，`injectSystemPrompt()` 重建新版格式 |
| API 兼容 | 输出仍是标准 `system` role 消息，协议适配器无需修改 |

---

## 八、验证标准

1. **模板加载**：删除 `config/system_prompt_template.md` → 启动 → Agent 行为与当前版本一致（降级生效）
2. **模板编辑**：修改模板文件中的身份描述 → 启动 → 模型自我介绍反映新描述
3. **Git 上下文**：在 git 仓库中启动 → system prompt 包含分支名和最近提交
4. **非 git 环境**：在非 git 目录启动 → system prompt 无 git 信息但不报错
5. **项目规则**：工作区放 `PROJECTRULES.md` → system prompt 包含其内容
6. **项目规则缺失**：不产生空段
7. **Skill 动态注入**：`/skill architecture` → system 消息更新，模型行为反映新规则
8. **System 消息合并**：任意操作后 `getMessages()` 中 system 消息始终为 1 条
9. **Resume 恢复**：旧会话恢复后，system prompt 为新版格式 + 已注入 skill 重建
10. **Token 预算**：注入大量 skill → system 区不超过 30% 窗口，超出的 skill 被截断
11. **已有测试通过**：`qa_CLFContext`、`qa_CLFSessionManager` 全部通过

---

## 九、实施路线

| 步骤 | 内容 | 预计影响范围 | 说明 |
|------|------|:---:|------|
| Step 1 | 创建模板文件 | 新增 1 个文件 | 独立，可最先做 |
| Step 2 | 实现 `CLFSystemPromptBuilder` | 新增 2 个文件 | Builder 独立模块 |
| Step 4 | `CLFContext` 接口增强 | 修改 2 个文件 | **必须在 Step 3 之前**（提供 `setSystemPrompt()` 接口） |
| Step 3 | 改造 `CLFAgentLoop` | 修改 2 个文件 | 依赖 Step 2 + Step 4 |
| Step 5 | CMakeLists 更新 | 修改 1 个文件 | |
| Step 6 | 文档更新 | 修改 1 个文件 | |
| Step 7 | 测试验证 | 运行全量测试 | `qa_CLFContext` + `qa_CLFSessionManager` |

> **实施顺序**：Step 1 → Step 2 → **Step 4（先做 Context 接口）** → Step 3（依赖 2+4） → Step 5 → Step 6 → Step 7
>
> 如果不先做 Step 4（`setSystemPrompt()` 接口），Step 3 的 `rebuildSystemMessage()` 无法安全落地。