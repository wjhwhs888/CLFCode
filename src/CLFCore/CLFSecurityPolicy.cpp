// CLFSecurityPolicy.cpp — 安全策略实现（四模式；危险命令检测已拆出
// CLFDangerousCommandDetector，B6——本类保留 API 转发）

#include "CLFCore/CLFSecurityPolicy.hpp"

namespace CLF::CLFCore {

CLFSecurityPolicy::CLFSecurityPolicy(CLFSecurityMode mode)
    : m_mode(mode) {
}

void CLFSecurityPolicy::setMode(CLFSecurityMode mode) {
    m_mode = mode;
}

const char* CLFSecurityPolicy::getModeName() const {
    switch (m_mode) {
        case CLFSecurityMode::Auto:    return "auto";
        case CLFSecurityMode::Analyze: return "analyze";
        case CLFSecurityMode::Edit:    return "edit";
        case CLFSecurityMode::Manual:  return "manual";
    }
    return "edit";
}

bool CLFSecurityPolicy::isAllowed(CLFToolRisk risk, bool& needConfirm) const {
    needConfirm = false;

    // 读操作永不限制
    if (risk == CLFToolRisk::Read) {
        return true;
    }

    // 写/命令：按模式决定
    switch (m_mode) {
        case CLFSecurityMode::Auto:
            return true; // 全放行

        case CLFSecurityMode::Analyze:
            return false; // 直接阻断

        case CLFSecurityMode::Edit:
        case CLFSecurityMode::Manual:
            needConfirm = true; // 允许但需确认
            return true;

        default:
            return false;
    }
}

CLFSecurityMode CLFSecurityPolicy::modeFromString(const std::string& s) {
    if (s == "auto")    return CLFSecurityMode::Auto;
    if (s == "analyze") return CLFSecurityMode::Analyze;
    if (s == "manual")  return CLFSecurityMode::Manual;
    return CLFSecurityMode::Edit; // 默认
}

CLFSecurityMode CLFSecurityPolicy::nextMode(CLFSecurityMode current) {
    for (int i = 0; i < kModeCount; ++i) {
        if (kAllModes[i] == current)
            return kAllModes[(i + 1) % kModeCount];
    }
    return CLFSecurityMode::Edit;
}

void CLFSecurityPolicy::setCommandAllowlist(std::vector<std::string> allowlist) {
    m_dangerousDetector.setAllowlist(std::move(allowlist));   // B6：转发
}

bool CLFSecurityPolicy::isDangerousCommand(const std::string& command) const {
    return m_dangerousDetector.isDangerous(command);          // B6：转发
}

bool CLFSecurityPolicy::isDangerousCommand(const std::string& command,
                                           const std::vector<std::string>& allowlist) {
    return CLFDangerousCommandDetector::isDangerous(command, allowlist);  // B6：转发
}

} // namespace CLF::CLFCore
