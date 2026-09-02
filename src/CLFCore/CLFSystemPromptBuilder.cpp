// CLFSystemPromptBuilder.cpp — System Prompt 构建器实现
// 模板加载 → 动态上下文捕获 → 规则/Skill/L1宪法组装 → Token 预算 → 变量替换

#include "CLFCore/CLFSystemPromptBuilder.hpp"
#include "CLFCore/CLFConfigLoader.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

namespace fs = std::filesystem;

namespace CLF::CLFCore {

namespace {

// ============================================================================
// 工具函数
// ============================================================================

std::string execCommand(const std::string& cmd) {
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
    }
    pclose(pipe);
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

int estimateTokenChars(const std::string& s) {
    int ascii = 0, nonAscii = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) ++ascii;
        else if ((c & 0xC0) == 0xC0) ++nonAscii;
    }
    return (ascii / 4) + (nonAscii * 3 / 2);
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// ============================================================================
// 缓存: L1 宪法
// ============================================================================

struct ConstitutionCache {
    std::string content;
    fs::file_time_type mtime;
};
static ConstitutionCache s_constitutionCache;

// ============================================================================
// 缓存: Git 状态
// ============================================================================

struct GitCache {
    std::string info;
    std::string workspaceRoot;
    std::time_t captureTime = 0;
};
static GitCache s_gitCache;

constexpr int kGitTTLSeconds = 30;

} // anonymous namespace

// ============================================================================
// build() — 主入口
// ============================================================================

std::string CLFSystemPromptBuilder::build(const Context& ctx) {
    // ① 加载模板（文件 → 降级默认）
    std::string tpl = loadTemplate();

    // ② 动态上下文
    std::string osInfo = detectOsInfo();
    std::string shellInfo = detectShellInfo();

    // ③ 项目信息
    std::string gitInfo = captureGitStatus(ctx.workspaceRoot);
    std::string rules = loadProjectRules(ctx.workspaceRoot);
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
    int headerTokens = estimateTokenChars(tpl);
    skillsBlock = applyTokenBudget(skillsBlock, ctx, headerTokens);

    // ⑦ 变量替换
    std::string lang = (ctx.interactionLanguage == "zh-CN") ? "中文" : ctx.interactionLanguage;
    std::string envInfo = osInfo + "\n- 工作目录：" + ctx.workspaceRoot + "\n- Shell：" + shellInfo;

    std::string result = replaceAll(tpl, "{{model_name}}", ctx.modelName);
    result = replaceAll(result, "{{interaction_language}}", lang);
    result = replaceAll(result, "{{os_info}}", envInfo);
    result = replaceAll(result, "{{project_context}}", projectCtx);
    result = replaceAll(result, "{{skills}}", skillsBlock);

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
// L1 宪法（缓存 + mtime 检测）
// ============================================================================

std::string CLFSystemPromptBuilder::loadConstitution() {
    std::string path = CLFConfigLoader::resolvePath("data/skills/constitution.md");
    std::error_code ec;
    if (!fs::exists(path, ec)) return "";

    auto ftime = fs::last_write_time(path, ec);
    if (!ec && ftime == s_constitutionCache.mtime && !s_constitutionCache.content.empty()) {
        return s_constitutionCache.content;  // mtime 未变，复用缓存
    }

    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::ostringstream oss;
    oss << file.rdbuf();

    s_constitutionCache.content = oss.str();
    s_constitutionCache.mtime   = ftime;
    return s_constitutionCache.content;
}

// ============================================================================
// Git 状态（跨平台 popen + TTL 30s 缓存）
// ============================================================================

std::string CLFSystemPromptBuilder::captureGitStatus(const std::string& workspaceRoot) {
    // 检查缓存是否有效
    std::time_t now = std::time(nullptr);
    if (s_gitCache.workspaceRoot == workspaceRoot &&
        s_gitCache.captureTime > 0 &&
        (now - s_gitCache.captureTime) < kGitTTLSeconds) {
        return s_gitCache.info;  // 缓存命中
    }

    // 检查是否为 git 仓库
    std::error_code ec;
    if (!fs::exists(workspaceRoot + "/.git", ec)) {
        s_gitCache = {};
        return "";
    }

    // 保存当前目录，切换到工作区执行 git 命令
    std::string result;
    std::string branch = execCommand("git -C \"" + workspaceRoot + "\" branch --show-current 2>nul");
    if (branch.empty()) {
        s_gitCache = {};
        return "";
    }

    result = "- Git 分支：" + branch + "\n- 最近提交：\n";
    std::string log = execCommand("git -C \"" + workspaceRoot + "\" log --oneline -5 2>nul");
    if (!log.empty()) {
        std::istringstream iss(log);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) result += "  " + line + "\n";
        }
    }

    std::string status = execCommand("git -C \"" + workspaceRoot + "\" status --short 2>nul");
    if (status.empty()) {
        result += "- 工作区状态：干净（无未提交变更）\n";
    } else {
        int count = 0;
        std::istringstream iss(status);
        std::string line;
        while (std::getline(iss, line)) { if (!line.empty()) ++count; }
        result += "- 工作区状态：" + std::to_string(count) + " 个文件有变更\n";
    }

    // 时间戳
    char timeStr[32];
    std::tm* tm = std::localtime(&now);
    std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm);
    result += std::string("（Git 状态捕获于 ") + timeStr + "，如需实时状态请使用 execute_command 查询）\n";

    // 更新缓存
    s_gitCache.info           = result;
    s_gitCache.workspaceRoot  = workspaceRoot;
    s_gitCache.captureTime    = now;
    return result;
}

// ============================================================================
// 项目规则
// ============================================================================

std::string CLFSystemPromptBuilder::loadProjectRules(const std::string& workspaceRoot) {
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
            content = content.substr(0, kMaxChars);
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

// ============================================================================
// OS / Shell 检测
// ============================================================================

std::string CLFSystemPromptBuilder::detectOsInfo() {
#ifdef _WIN32
    std::string ver = execCommand("ver 2>nul");
    if (!ver.empty()) {
        // "Microsoft Windows [Version 10.0.26200]" → "Windows 10.0.26200"
        size_t pos = ver.find("Windows");
        if (pos != std::string::npos) {
            ver = ver.substr(pos);
            // 去掉末尾的 ]
            size_t rb = ver.find(']');
            if (rb != std::string::npos) ver = ver.substr(0, rb);
        }
        return "- 操作系统：" + ver;
    }
    return "- 操作系统：Windows";
#else
    std::string uname = execCommand("uname -a 2>/dev/null");
    if (!uname.empty()) return "- 操作系统：" + uname;
    return "- 操作系统：Linux / macOS";
#endif
}

std::string CLFSystemPromptBuilder::detectShellInfo() {
#ifdef _WIN32
    const char* comspec = std::getenv("COMSPEC");
    if (comspec) {
        std::string s(comspec);
        // 判断是 cmd 还是 bash
        if (s.find("bash") != std::string::npos) return "bash (Git Bash)";
        if (s.find("powershell") != std::string::npos || s.find("pwsh") != std::string::npos)
            return "PowerShell";
        return "cmd.exe";
    }
    return "cmd.exe";
#else
    const char* shell = std::getenv("SHELL");
    if (shell) {
        std::string s(shell);
        size_t pos = s.rfind('/');
        return (pos != std::string::npos) ? s.substr(pos + 1) : s;
    }
    return "sh";
#endif
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

    int skillsTokens = estimateTokenChars(skillsBlock);
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
        int secTokens = estimateTokenChars(sections[i]);
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
