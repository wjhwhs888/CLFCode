// CLFSecurityPolicy.hpp — 安全策略（四模式）
// 控制工具执行权限：读操作永不限制，写/命令按模式放行/阻断/确认
//
// example:
//   CLF::CLFCore::CLFSecurityPolicy policy(
//       CLF::CLFCore::CLFSecurityMode::Edit);
//   bool needConfirm = false;
//   if (policy.isAllowed(CLFToolRisk::Command, needConfirm)) {
//       if (!needConfirm || confirmCallback("execute_command")) { ... }
//   }

#pragma once

#include <string>

#include "CLFTypes/CLFTypes.hpp"

namespace CLF::CLFCore {

// CLFSecurityMode / CLFToolRisk 定义在 CLFTypes.hpp（供 CLFTool 等复用）

class CLFSecurityPolicy {
public:
    explicit CLFSecurityPolicy(CLFSecurityMode mode = CLFSecurityMode::Edit);

    void setMode(CLFSecurityMode mode);
    CLFSecurityMode getMode() const { return m_mode; }

    // 模式名："auto" | "analyze" | "edit" | "manual"
    const char* getModeName() const;

    // 判断某风险等级的工具在当前模式下是否允许执行
    // needConfirm 输出：允许但需用户确认（仅 Edit/Manual 模式的写/命令）
    bool isAllowed(CLFToolRisk risk, bool& needConfirm) const;

    // 字符串 → 模式；未知字符串返回 Edit
    static CLFSecurityMode modeFromString(const std::string& s);

private:
    CLFSecurityMode m_mode;
};

} // namespace CLF::CLFCore
