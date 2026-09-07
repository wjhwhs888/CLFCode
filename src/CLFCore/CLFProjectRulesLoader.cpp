// CLFProjectRulesLoader.cpp — 项目规则加载实现
// C5 拆分：逻辑自 CLFSystemPromptBuilder::loadProjectRules 原样搬移

#include "CLFCore/CLFProjectRulesLoader.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "CLFTypes/CLFTextUtil.hpp"

namespace fs = std::filesystem;

namespace CLF::CLFCore {

std::string CLFProjectRulesLoader::loadProjectRules(const std::string& workspaceRoot) {
    constexpr int kMaxChars = 5000;

    auto tryRead = [&](const std::string& filename) -> std::string {
        std::string path = workspaceRoot + "/" + filename;
        std::error_code ec;
        if (!fs::exists(path, ec)) return "";
        if (fs::file_size(path, ec) == 0) return "";  // 空文件不降级
        std::ifstream file(path);
        if (!file.is_open()) return "";
        std::ostringstream oss;
        oss << file.rdbuf();
        std::string content = oss.str();
        if (content.empty()) return "";
        bool truncated = false;
        if (content.size() > static_cast<size_t>(kMaxChars)) {
            // A2：字节级截断 → utf8SafeHead（不劈半多字节；无 ellipsis，截断标记在下方）
            content = CLFTextUtil::utf8SafeHead(content, kMaxChars, "");
            truncated = true;
        }
        std::string header = "## 项目规则（来自 " + filename + "）\n";
        if (truncated) content += "\n[…项目规则超过5000字符，已截断]";
        return header + content;
    };

    std::string result = tryRead("PROJECTRULES.md");
    if (!result.empty()) return result;
    return tryRead("CLAUDE.md");
}

} // namespace CLF::CLFCore
