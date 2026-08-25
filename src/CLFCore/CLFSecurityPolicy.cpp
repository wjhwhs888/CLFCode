// CLFSecurityPolicy.cpp — 安全策略实现

#include "CLFCore/CLFSecurityPolicy.hpp"

#include <cctype>

namespace CLF::CLFCore {

namespace {

// S2-2 危险命令模式（均为小写，匹配前先归一化）。
// ⚠ 这是提示层而非安全边界：模型可用变量拼接、管道、中间脚本等方式绕过。
//   目的是降低"手滑级"误操作概率，真正的管控仍由四模式安全策略承担。
const char* const kDangerousPatterns[] = {
    "rm -rf", "rm -fr", "rm --recursive --force",
    "del /s", "del /q", "rd /s", "rmdir /s",
    "remove-item -recurse", "remove-item -r ",
    "format ", "diskpart", "mkfs", "dd if=",
    "git push --force", "git push -f ", "git reset --hard",
    "shutdown",
};

// 归一化：转小写 + 去除引号（防 "r""m" -rf 之类的简单变形）
std::string normalizeCommand(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\'') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

} // anonymous namespace

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
    m_commandAllowlist = std::move(allowlist);
}

bool CLFSecurityPolicy::isDangerousCommand(const std::string& command) const {
    return isDangerousCommand(command, m_commandAllowlist);
}

bool CLFSecurityPolicy::isDangerousCommand(const std::string& command,
                                           const std::vector<std::string>& allowlist) {
    const std::string norm = normalizeCommand(command);

    // 白名单：前缀命中即视为已获授权，跳过检测
    for (const auto& prefix : allowlist) {
        if (prefix.empty()) continue;
        if (norm.rfind(normalizeCommand(prefix), 0) == 0) return false;
    }

    for (const char* pattern : kDangerousPatterns) {
        if (norm.find(pattern) != std::string::npos) return true;
    }
    return false;
}

} // namespace CLF::CLFCore
