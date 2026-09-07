// CLFSystemPromptBuilder.cpp — System Prompt 构建器实现
// 模板加载 → 动态上下文组装 → 规则/Skill/L1宪法组装 → Token 预算 → 变量替换
// C5（2026-09-07）收窄：OS/Shell/Git 捕获 → CLFSystemInfoProvider、
// 子进程执行 → CLFSubprocessRunner、项目规则 → CLFProjectRulesLoader

#include "CLFCore/CLFSystemPromptBuilder.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFProjectRulesLoader.hpp"
#include "CLFCore/CLFSubprocessRunner.hpp"
#include "CLFTypes/CLFTextUtil.hpp"   // A2：估算/截断/替换/时间戳归位

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace CLF::CLFCore {

// ============================================================================
// build() — 主入口（流程不变，C5 保真）
// ============================================================================

std::string CLFSystemPromptBuilder::build(const Context& ctx) {
    // ① 加载模板（文件 → 降级默认）
    std::string tpl = loadTemplate();

    // ② 动态上下文（C5：OS/Shell 检测经 CLFSystemInfoProvider）
    std::string osInfo    = CLFSystemInfoProvider::detectOsInfo();
    std::string shellInfo = CLFSystemInfoProvider::detectShellInfo();

    // ③ 项目信息（C5：Git 捕获经 InfoProvider 实例缓存；规则经 CLFProjectRulesLoader）
    std::string gitInfo = m_infoProvider.captureGitStatus(ctx.workspaceRoot);
    std::string rules = CLFProjectRulesLoader::loadProjectRules(ctx.workspaceRoot);
    std::string projectCtx;
    if (!gitInfo.empty()) projectCtx += gitInfo;
    if (!rules.empty()) {
        if (!projectCtx.empty()) projectCtx += "\n";
        projectCtx += rules;
    }
    // S3-1: 会话摘要拼入 {{project_context}}（system 永不截断 + 老模板兼容）
    if (!ctx.sessionSummary.empty()) {
        if (!projectCtx.empty()) projectCtx += "\n\n";
        projectCtx += "## 会话摘要\n" + ctx.sessionSummary;
    }

    // ④ L1 宪法
    std::string constitution = loadConstitution();

    // ⑤ 组装 skills 区
    std::string skillsBlock;
    if (!constitution.empty()) {
        skillsBlock += "## 行为准则（L1 编码宪法）\n\n" + constitution;
    }
    for (const auto& [name, content] : ctx.skills) {
        if (!skillsBlock.empty()) skillsBlock += "\n\n---\n";
        skillsBlock += "## 行为准则（" + name + "）\n\n" + content;
    }

    // ⑥ Token 预算（skillsBlock 可能被截断）
    int headerTokens = CLFTextUtil::estimateTokenChars(tpl);
    skillsBlock = applyTokenBudget(skillsBlock, ctx, headerTokens);

    // ⑦ 变量替换
    std::string lang = (ctx.interactionLanguage == "zh-CN") ? "中文" : ctx.interactionLanguage;
    std::string envInfo = osInfo + "\n- 工作目录：" + ctx.workspaceRoot + "\n- Shell：" + shellInfo;

    std::string result = CLFTextUtil::replaceAll(tpl, "{{model_name}}", ctx.modelName);
    result = CLFTextUtil::replaceAll(result, "{{interaction_language}}", lang);
    result = CLFTextUtil::replaceAll(result, "{{os_info}}", envInfo);
    result = CLFTextUtil::replaceAll(result, "{{project_context}}", projectCtx);
    result = CLFTextUtil::replaceAll(result, "{{skills}}", skillsBlock);

    return result;
}

// ============================================================================
// 模板
// ============================================================================

std::string CLFSystemPromptBuilder::loadTemplate() {
    std::string templatePath = CLFConfigLoader::resolvePath("config/system_prompt_template.md");
    std::error_code ec;
    if (fs::exists(templatePath, ec)) {
        std::ifstream file(templatePath);
        if (file.is_open()) {
            std::ostringstream oss;
            oss << file.rdbuf();
            std::string content = oss.str();
            if (!content.empty()) return content;
        }
    }
    return defaultTemplate();
}

std::string CLFSystemPromptBuilder::defaultTemplate() {
    // 与当前 injectSystemPrompt() 行为一致，作为模板文件缺失时的降级
    return
        "你是 CLFCode，一个本地运行的 AI Coding Agent。\n"
        "当前模型：{{model_name}}。\n"
        "你运行在用户本地机器上，具备文件读写、命令执行、网络调用等工具能力。\n"
        "你的后端 API 由 DeepSeek 提供，但你是独立的 Agent 产品。\n"
        "你永远不应自称 Claude、OpenAI、Anthropic 或其他 AI 品牌。\n"
        "请始终使用 {{interaction_language}} 与用户交流。\n"
        "\n"
        "## 运行环境\n"
        "{{os_info}}\n"
        "\n"
        "## 项目信息\n"
        "{{project_context}}\n"
        "\n"
        "## 文件管理规则\n"
        "- 任务中创建的临时文件（备份、中间输出等），任务结束前必须清理\n"
        "- 优先复用已有文件，避免重复创建备份\n"
        "- 尽量用重定向/管道而非落盘中间文件\n"
        "\n"
        "## 行为准则\n"
        "{{skills}}\n";
}

// ============================================================================
// L1 宪法（mtime 缓存随实例——C5 消文件级静态对象）
// ============================================================================

std::string CLFSystemPromptBuilder::loadConstitution() {
    std::string path = CLFConfigLoader::resolvePath("data/skills/constitution.md");
    std::error_code ec;
    if (!fs::exists(path, ec)) return "";

    auto ftime = fs::last_write_time(path, ec);
    if (!ec && ftime == m_constitutionCache.mtime && !m_constitutionCache.content.empty()) {
        return m_constitutionCache.content;  // mtime 未变，复用缓存
    }

    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::ostringstream oss;
    oss << file.rdbuf();

    m_constitutionCache.content = oss.str();
    m_constitutionCache.mtime   = ftime;
    return m_constitutionCache.content;
}

// ============================================================================
// Token 预算
// ============================================================================

std::string CLFSystemPromptBuilder::applyTokenBudget(const std::string& skillsBlock,
                                                      const Context& ctx,
                                                      int headerTokens) {
    if (skillsBlock.empty()) return "";
    constexpr double kDefaultSystemRatio = 0.3;
    int systemBudget = static_cast<int>(ctx.maxContextWindow * kDefaultSystemRatio);

    int skillsTokens = CLFTextUtil::estimateTokenChars(skillsBlock);
    int total = headerTokens + skillsTokens;

    if (total <= systemBudget) return skillsBlock;  // 未超预算

    // 超出预算：按 skill 段（以 "---" 分隔）从后往前丢弃
    // 第一个段是 L1 宪法，永不丢弃；后续段是 L2/L3 skill
    std::vector<std::string> sections;
    size_t start = 0;
    size_t pos = 0;
    while ((pos = skillsBlock.find("\n\n---\n", start)) != std::string::npos) {
        sections.push_back(skillsBlock.substr(start, pos - start));
        start = pos + 6;  // 跳过 "\n\n---\n"
    }
    sections.push_back(skillsBlock.substr(start));

    // 至少保留 L1 宪法（第一个段）
    int dropped = 0;
    std::vector<std::string> droppedNames;
    // 从 sections 中提取 skill 名称（"## 行为准则（name）"）
    auto extractName = [](const std::string& s) -> std::string {
        size_t a = s.find("## 行为准则（");
        if (a == std::string::npos) return "";
        a += std::strlen("## 行为准则（");
        size_t b = s.find("）", a);
        if (b == std::string::npos) return "";
        return s.substr(a, b - a);
    };

    int accumulated = headerTokens;
    std::string kept;
    for (size_t i = 0; i < sections.size(); ++i) {
        int secTokens = CLFTextUtil::estimateTokenChars(sections[i]);
        std::string sep = kept.empty() ? "" : "\n\n---\n";
        if (accumulated + secTokens <= systemBudget || i == 0) {
            // L1 宪法（i==0）必保留；其他段在预算内才保留
            kept += sep + sections[i];
            accumulated += secTokens;
        } else {
            ++dropped;
            std::string name = extractName(sections[i]);
            if (!name.empty() && name != "L1 编码宪法") droppedNames.push_back(name);
        }
    }

    if (dropped > 0) {
        std::string note = "\n\n[system prompt 超出 token 预算（限制 "
                         + std::to_string(systemBudget) + "），以下 "
                         + std::to_string(dropped) + " 条 skill 规则未注入：";
        for (size_t i = 0; i < droppedNames.size(); ++i) {
            if (i > 0) note += ", ";
            note += droppedNames[i];
        }
        note += "]";
        kept += note;
    }

    return kept;
}

} // namespace CLF::CLFCore
