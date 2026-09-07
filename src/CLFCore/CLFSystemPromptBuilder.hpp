// CLFSystemPromptBuilder.hpp — System Prompt 构建器
// 负责模板加载、动态上下文组装、skill 组装和 token 预算控制
// 最终输出单条 system 消息内容字符串
//
// C5（2026-09-07）收窄：系统信息捕获（OS/Shell/Git）→ CLFSystemInfoProvider、
// 子进程执行 → CLFSubprocessRunner、项目规则读取 → CLFProjectRulesLoader。
// Builder 实例化：宪法/Git 缓存随实例（消文件级静态对象，P0-7）。

#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "CLFCore/CLFSystemInfoProvider.hpp"

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

    // 构建完整 system prompt（单条消息内容）；build() 流程不变（C5 保真）
    std::string build(const Context& ctx);

private:
    static std::string loadTemplate();
    static std::string defaultTemplate();
    std::string loadConstitution();   // 实例方法：mtime 缓存随实例
    static std::string applyTokenBudget(const std::string& prompt,
                                        const Context& ctx,
                                        int usedTokens);

    // C5：git 缓存随 InfoProvider 实例（原 s_gitCache 文件级静态消除）
    CLFSystemInfoProvider m_infoProvider;

    // C5：宪法 mtime 缓存随 Builder 实例（原 s_constitutionCache 消除）
    struct ConstitutionCache {
        std::string content;
        std::filesystem::file_time_type mtime;
    };
    ConstitutionCache m_constitutionCache;
};

} // namespace CLF::CLFCore
