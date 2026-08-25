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
#include <vector>

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

    // 所有模式的有序列表（供 cycleMode 等遍历使用）
    static constexpr CLFSecurityMode kAllModes[] = {
        CLFSecurityMode::Auto, CLFSecurityMode::Analyze,
        CLFSecurityMode::Edit, CLFSecurityMode::Manual
    };
    static constexpr int kModeCount = 4;

    // 循环到下一个模式
    static CLFSecurityMode nextMode(CLFSecurityMode current);

    //设置危险命令检测的前缀白名单（命中则跳过检测）
    // example:
    //   policy.setCommandAllowlist(config.m_commandAllowlist);
    void setCommandAllowlist(std::vector<std::string> allowlist);

    //判断命令是否命中危险模式（用本实例的白名单）
    // ⚠ 定位：这是**提示层**而非安全边界——模型可用变量拼接、中间命令等方式
    //   绕过，仅用于降低误操作概率，不替代四模式安全策略
    // example:
    //   if (policy.isDangerousCommand(cmd)) needConfirm = true;
    bool isDangerousCommand(const std::string& command) const;

    //同上的静态版本（便于单测，不依赖实例）
    // example:
    //   CLFSecurityPolicy::isDangerousCommand("rm -rf /", {});
    static bool isDangerousCommand(const std::string& command,
                                   const std::vector<std::string>& allowlist);

private:
    CLFSecurityMode m_mode;
    std::vector<std::string> m_commandAllowlist;
};

} // namespace CLF::CLFCore
