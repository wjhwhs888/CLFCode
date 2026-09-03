// CLFDangerousCommandDetector.hpp — 危险命令检测器（批次 B6，2026-09-03）
// 自 CLFSecurityPolicy 拆出（P1-16 双簇职责）：模式安全策略（静态风险级判定）
// 与命令文本检测（提示层）性质不同，独立成类便于单独测试与扩展。
//
// ⚠ 定位不变：提示层而非安全边界——模型可用变量拼接、管道、中间脚本绕过。
//   目的是降低"手滑级"误操作概率，真正的管控由四模式安全策略承担。
//
// example:
//   CLFDangerousCommandDetector detector;
//   detector.setAllowlist({"git", "cmake"});
//   if (detector.isDangerous("rm -rf build")) warn();

#pragma once

#include <string>
#include <vector>

namespace CLF::CLFCore {

class CLFDangerousCommandDetector {
public:
    // 白名单（前缀命中即视为已获授权，跳过检测）
    void setAllowlist(std::vector<std::string> allowlist) {
        m_allowlist = std::move(allowlist);
    }

    // 用实例 allowlist 检测
    bool isDangerous(const std::string& command) const;

    // 显式 allowlist（静态纯函数——qa 与无状态场景）
    static bool isDangerous(const std::string& command,
                            const std::vector<std::string>& allowlist);

private:
    std::vector<std::string> m_allowlist;
};

} // namespace CLF::CLFCore
