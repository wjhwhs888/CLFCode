// CLFSystemPromptBuilder.hpp — System Prompt 构建器
// 负责模板加载、动态上下文捕获、项目规则读取、skill 组装和 token 预算控制
// 最终输出单条 system 消息内容字符串

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace CLF::CLFCore {

class CLFSystemPromptBuilder {
public:
    struct Context {
        std::string workspaceRoot;
        std::string interactionLanguage;  // "zh-CN" → "中文"
        std::string modelName;
        std::vector<std::pair<std::string, std::string>> skills; // name → content
        int maxContextWindow = 1048576;
        // S3-1: 会话摘要（空 = 无摘要）——拼入 {{project_context}} 占位符
        // （复用现成占位符，对用户自定义旧模板天然兼容，设计 §S3-1 避坑）
        std::string sessionSummary;
    };

    // 构建完整 system prompt（单条消息内容）
    static std::string build(const Context& ctx);

private:
    static std::string loadTemplate();
    static std::string defaultTemplate();
    static std::string loadConstitution();
    static std::string captureGitStatus(const std::string& workspaceRoot);
    static std::string loadProjectRules(const std::string& workspaceRoot);
    static std::string detectOsInfo();
    static std::string detectShellInfo();
    static std::string applyTokenBudget(const std::string& prompt,
                                        const Context& ctx,
                                        int usedTokens);
};

} // namespace CLF::CLFCore
