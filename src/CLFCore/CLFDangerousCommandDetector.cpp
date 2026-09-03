// CLFDangerousCommandDetector.cpp — 危险命令检测器实现（批次 B6）
// ⚠ 纯搬移批次纪律：检测逻辑自 CLFSecurityPolicy.cpp 逐行搬移，行为零变化

#include "CLFCore/CLFDangerousCommandDetector.hpp"

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

bool CLFDangerousCommandDetector::isDangerous(const std::string& command) const {
    return isDangerous(command, m_allowlist);
}

bool CLFDangerousCommandDetector::isDangerous(
    const std::string& command,
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
