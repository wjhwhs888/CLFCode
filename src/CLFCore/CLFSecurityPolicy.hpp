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

namespace CLF::CLFCore {

enum class CLFSecurityMode {
    Auto    = 0,  // L1 自动：全放行
    Analyze = 1,  // L2 分析：读放行，写/命令阻断
    Edit    = 2,  // L3 编辑：读放行，写/命令需确认
    Manual  = 3   // L4 手动：读放行，写/命令需确认
};

// 工具风险等级
enum class CLFToolRisk {
    Read    = 0,  // 读操作：永不限制
    Write   = 1,  // 写操作（文件覆盖等）
    Command = 2   // 命令执行（不可控副作用）
};

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
