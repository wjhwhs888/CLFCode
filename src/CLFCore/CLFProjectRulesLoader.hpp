// CLFProjectRulesLoader.hpp — 项目规则加载（C5：Builder 拆分，2026-09-07）
// 读取工作区 PROJECTRULES.md（缺失/空 → 降级 CLAUDE.md），
// 超 5000 字符按 UTF-8 边界安全截断并加标记。
//
// example:
//   std::string rules = CLFProjectRulesLoader::loadProjectRules(workspaceRoot);

#pragma once

#include <string>

namespace CLF::CLFCore {

class CLFProjectRulesLoader {
public:
    // 项目规则文本（含 "## 项目规则（来自 <文件>）" 头）；无规则 → 空串
    static std::string loadProjectRules(const std::string& workspaceRoot);
};

} // namespace CLF::CLFCore
