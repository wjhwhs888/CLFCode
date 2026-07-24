# Agent 知识库文件索引

| 级别 | 文件 | 说明 | 加载方式 |
|------|------|------|----------|
| L1 | `constitution.md` | 编码宪法 | 始终加载 |
| L2 | `architecture.md` | 架构设计原则 | 按需加载 |
| L3 | `debug.md` | Debug 标准工作流 | 按需加载 |
| L3 | `code_review.md` | 代码审查清单 | 按需加载 |
| L3 | `unittest.md` | 单元测试生成规则 | 按需加载 |

## 使用方式

Agent 根据任务类型动态加载对应 Skill：
- 开发新功能 → `constitution.md` + `architecture.md`
- 修复 Bug → `constitution.md` + `debug.md`
- 代码审查 → `code_review.md`
- 写测试 → `unittest.md`
