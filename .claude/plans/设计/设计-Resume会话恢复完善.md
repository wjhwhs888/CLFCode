# 设计-Resume 会话恢复完善方案

> **状态**：设计中
> **创建**：2026-08-11
> **关联**：用户反馈 `/resume` 恢复后 AI 忘记上下文 + 终端看不到历史

---

## 一、Context（背景）

### 问题

使用 `/resume` 恢复已保存的会话后：
1. **AI 忘记上下文**：恢复时 `if (msg.m_role == "system") continue` 一刀切跳过了 `/skill` 注入的知识库内容
2. **终端看不到历史**：只显示 "✓ 会话已恢复"，屏幕上没有之前的对话内容

### 当前代码缺陷

`CLFAgentLoop::restoreSession()`（`CLFAgentLoop.cpp:378`）：

```cpp
bool CLFAgentLoop::restoreSession(const std::string& filePath) {
    std::vector<CLFMessage> messages;
    if (!CLFSessionManager::load(filePath, messages)) return false;

    m_context.clear();
    for (const auto& msg : messages) {
        if (msg.m_role == "system") continue;  // ← BUG: 跳过了 skill 注入
        m_context.appendMessage(msg);
    }
    injectSystemPrompt();  // 只补回基础提示词 + L1 宪法
    return true;
}
```

正常会话中 system 消息有两类：
- 基础提示词（会被 `injectSystemPrompt()` 重新注入，跳过正确）
- `/skill` 知识库注入（`[Knowledge: xxx]...`，跳过导致丢失）

---

## 二、修复方案

### 概览

| # | 改动 | 文件 |
|---|------|------|
| 1 | 会话保存时记录 `loaded_skills` | `CLFMessageCodec.cpp` + `CLFAgentLoop.cpp` |
| 2 | 恢复时重新注入 skill + 回显历史到终端 | `CLFAgentLoop.cpp` |
| 3 | `/resume` 显示复原结果 | `CLFCommands.cpp` |

### 方案 1：保存 `loaded_skills` 到会话 JSON

**`CLFMessageCodec::serialize()`** — 新增 `skills` 数组字段：

```json
{
  "version": 1,
  "messageCount": 42,
  "saved_at": "2026-08-11_10-20-57",
  "title": "查看一下项目信息",
  "skills": ["architecture", "debug"],   // ← 新增
  "messages": [...]
}
```

**实现方式**：给 `serialize()` 加一个 `const std::vector<std::string>& skills` 参数（默认空，向后兼容）。

**`CLFAgentLoop::saveSession()`** — 传入 `m_loadedSkills`：
```cpp
std::string CLFAgentLoop::saveSession(...) const {
    return CLFSessionManager::save(m_context.getMessages(), dirPath, incomplete, m_loadedSkills);
}
```

### 方案 2：修复 restore + 回显历史

**`CLFAgentLoop::restoreSession()`** — 三个改动：

```cpp
bool CLFAgentLoop::restoreSession(const std::string& filePath) {
    std::vector<CLFMessage> messages;
    std::vector<std::string> skills;
    if (!CLFSessionManager::load(filePath, messages, &skills)) return false;

    m_context.clear();
    
    // ① 回显历史到终端
    if (m_output) {
        m_output->emitContent("\n● 会话已恢复\n\n");
        for (const auto& msg : messages) {
            if (msg.m_role == "user") {
                m_output->emitContent("> " + msg.m_content + "\n\n");
            } else if (msg.m_role == "assistant" && !msg.m_content.empty()) {
                m_output->emitContent(msg.m_content + "\n");
            }
            // tool / system 消息跳过（终端不需要显示）
        }
        m_output->emitContent("──────────────\n");
    }

    // ② 恢复消息（跳过所有 system 消息，由后续步骤重建）
    for (const auto& msg : messages) {
        if (msg.m_role == "system") continue;  // 全部跳过：基础提示词 → injectSystemPrompt 重建
        m_context.appendMessage(msg);           // skill 注入 → 步骤③ 从文件系统重新加载
    }
    injectSystemPrompt();

    // ③ 重新注入知识库（从 data/skills/ 重新加载，保证内容最新）
    m_loadedSkills.clear();  // 清空旧记录，避免与步骤③ push_back 叠加
    for (const auto& name : skills) {
        std::string content = CLFSkillLoader::getContent(name);
        if (!content.empty()) {
            injectSkillToContext(name, content);  // 内部会 push_back 到 m_loadedSkills
        }
    }

    return true;
}
```

**关键逻辑**：
- 回显：遍历 `messages`，user 消息用 `> ` 前缀，assistant 消息原样输出
- 知识库恢复：从 `data/skills/` 重新加载 skill 内容并注入——这样即使 skill 文件更新了，恢复后也是最新版本
- system 消息判断：全部跳过，不依赖前缀匹配。基础提示词由 `injectSystemPrompt()` 重建，skill 注入由步骤③从文件系统重新加载（保证内容为最新版本）
- `m_loadedSkills` 必须在步骤③之前清空，否则会与 `injectSkillToContext` 内部的 `push_back` 叠加导致重复

### 方案 3：更新 `/resume` 显示

**`CLFCommands.cpp`** — 恢复成功时清除输出缓冲区，让历史重放更清晰：

```cpp
if (agent.restoreSession(sessions[idx - 1].m_path)) {
    // 历史已在 restoreSession 中回显，此处只做确认
    // (不再重复显示 "✓ 会话已恢复")
} else {
    if (output) output->emitContent("✗ 恢复失败\n");
}
```

---

## 三、涉及文件与改动量

| 文件 | 改动 | 说明 |
|------|------|------|
| `CLFMessageCodec.hpp` | ~3 行 | `serialize()` 和 `parseFull()` 加 `skills` 参数 |
| `CLFMessageCodec.cpp` | ~8 行 | 序列化/反序列化 `skills` 字段 |
| `CLFSessionManager.hpp` | ~2 行 | `save()`/`load()` 加 `skills` 参数 |
| `CLFSessionManager.cpp` | ~5 行 | 透传 skills 参数 |
| `CLFAgentLoop.cpp` | ~25 行 | `saveSession()` 传 skills；`restoreSession()` 回显+skill恢复 |
| `CLFCommands.cpp` | ~3 行 | 调整 `/resume` 显示文本 |

**总计**：~46 行改动，无新文件，全部在已有结构中扩展。

---

## 四、兼容性

- 旧会话 JSON 无 `skills` 字段 → `parseFull()` 返回空 `skills` 列表 → 恢复行为与之前一致（只是没回显）
- 旧代码调用 `serialize()` 不传 `skills` → 默认空列表 → JSON 中不出现 `skills` 字段

---

## 五、验证标准

1. 新会话 → `/skill architecture` → `/exit`
2. 重启 → `/history` → `/resume 1`
3. 验证：终端显示了之前的对话（`> 查看项目信息` + AI 回复）
4. 验证：AI 记得 architecture 规则（输入"设计一个新模块"，AI 应遵循架构原则）
5. 旧会话（无 skills 字段）仍可正常恢复
