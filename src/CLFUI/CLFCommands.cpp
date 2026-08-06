// CLFCommands.cpp — 内置斜杠命令 handler 实现
// 每个命令一个独立函数，通过 registerBuiltinCommands() 批量注册

#include "CLFUI/CLFCommandDispatcher.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"

#include <algorithm>
#include <string>

namespace CLF::CLFUI {
using namespace CLF::CLFCore;
using CLF::CLFTypes::ICLFOutput;

namespace {

// ============================================================================
// 会话管理
// ============================================================================

bool cmdExit(const std::string&, const std::string&,
             CLFAgentLoop& agent, const std::string& historyDir,
             ICLFOutput* output) {
    agent.saveSession(historyDir, false);
    CLFSessionManager::removeAllIncomplete(historyDir);
    if (output) output->emitContent("● 会话已保存。再见 — CLFCode\n");
    return true;
}

bool cmdClear(const std::string&, const std::string&,
              CLFAgentLoop& agent, const std::string& historyDir,
              ICLFOutput* output) {
    agent.saveSession(historyDir, false);
    CLFSessionManager::removeAllIncomplete(historyDir);
    agent.clearContext();
    if (output) output->emitContent("✓ 会话已保存，新会话开始\n");
    return true;
}

// ============================================================================
// 信息查询
// ============================================================================

bool cmdHelp(const std::string&, const std::string&,
             CLFAgentLoop&, const std::string&,
             ICLFOutput* output) {
    if (output) output->emitContent(
        "\n  ⎿ /exit   退出 | /clear 新会话 | /skill 知识库\n"
        "  ⎿ /mode   安全模式 | /history 会话 | /resume <n> 恢复\n"
        "  ⎿ /model  模型 | /config 配置 | /context 用量\n"
        "  ⎿ Shift+Tab 切换模式 | Esc 清空输入\n");
    return true;
}

bool cmdModel(const std::string&, const std::string&,
              CLFAgentLoop& agent, const std::string&,
              ICLFOutput* output) {
    const auto& cfg = agent.getConfig();
    if (output) output->emitContent(
        "\n● 当前模型\n"
        "  ⎿ 主模型: " + cfg.m_modelName + "\n"
        "  ⎿ 副模型: " + cfg.m_subModel + "\n");
    return true;
}

bool cmdConfig(const std::string&, const std::string&,
               CLFAgentLoop& agent, const std::string&,
               ICLFOutput* output) {
    const auto& cfg = agent.getConfig();
    if (output) output->emitContent(
        "\n● 配置信息\n"
        "  ⎿ 连接: " + cfg.m_apiBaseUrl + "\n"
        "  ⎿ 模型: " + cfg.m_modelName + " (副: " + cfg.m_subModel + ")\n"
        "  ⎿ 参数: temperature=" + std::to_string(cfg.m_temperature)
            + " top_p=" + std::to_string(cfg.m_topP)
            + " max_tokens=" + std::to_string(cfg.m_maxTokens) + "\n"
        "  ⎿ 流式: " + (cfg.m_stream ? "开" : "关")
            + " | 安全: " + agent.getSecurityModeName() + "\n"
        "  ⎿ 上下文: " + std::to_string(cfg.m_maxContextWindow) + " tokens\n");
    return true;
}

bool cmdContext(const std::string&, const std::string&,
                CLFAgentLoop& agent, const std::string&,
                ICLFOutput* output) {
    const auto& ctx = agent.getContext();
    const auto& cfg = agent.getConfig();
    int used = ctx.estimateTokens();
    int max  = cfg.m_maxContextWindow;
    int pct  = (max > 0) ? (used * 100 / max) : 0;
    int bars = (max > 0) ? (used * 20 / max) : 0;
    if (bars > 20) bars = 20;
    std::string bar;
    for (int i = 0; i < 20; ++i)
        bar += (i < bars) ? "█" : "░";
    if (output) output->emitContent(
        "\n● 上下文用量\n"
        "  ⎿ 用量: " + std::to_string(used) + " / "
            + std::to_string(max) + " tokens"
            + " (" + std::to_string(pct) + "%)\n"
        "  ⎿ [" + bar + "]\n"
        "  ⎿ 剩余: ~" + std::to_string(max - used) + " tokens"
            + (pct >= 80 ? "  ⚠ 建议 /clear" : "") + "\n");
    return true;
}

// ============================================================================
// 模式切换
// ============================================================================

bool cmdMode(const std::string&, const std::string& args,
             CLFAgentLoop& agent, const std::string&,
             ICLFOutput* output) {
    if (!args.empty()) {
        agent.setSecurityMode(CLFSecurityPolicy::modeFromString(args));
    }
    if (output) output->emitContent("● 模式: " + std::string(agent.getSecurityModeName()) + "\n");
    return true;
}

// ============================================================================
// 知识库
// ============================================================================

bool cmdSkill(const std::string&, const std::string& args,
              CLFAgentLoop& agent, const std::string&,
              ICLFOutput* output) {
    if (args.empty() || args == "list") {
        auto names = CLFSkillLoader::listNames();
        auto loaded = agent.getLoadedSkills();
        std::string out = "\n● 知识库\n";
        for (const auto& n : names) {
            bool isLoaded = std::find(loaded.begin(), loaded.end(), n) != loaded.end();
            out += "  ⎿ " + n + (isLoaded ? "  [已加载]\n" : "  [未加载]\n");
        }
        if (output) output->emitContent(out);
    } else {
        std::string content = CLFSkillLoader::getContent(args);
        if (content.empty()) {
            if (output) output->emitContent("✗ 未找到: " + args + "\n");
        } else {
            agent.injectSkillToContext(args, content);
            if (output) output->emitContent("✓ 已加载: " + args + "\n");
        }
    }
    return true;
}

// ============================================================================
// 会话恢复
// ============================================================================

bool cmdHistory(const std::string&, const std::string&,
                CLFAgentLoop&, const std::string& historyDir,
                ICLFOutput* output) {
    auto sessions = CLFSessionManager::list(historyDir, 10);
    if (sessions.empty()) {
        if (output) output->emitContent("  ⎿ 暂无已保存的会话\n");
    } else {
        std::string out = "\n● 会话列表\n";
        for (const auto& s : sessions)
            out += "  ⎿ " + s.m_savedAt + "  " + s.m_title + "\n";
        if (output) output->emitContent(out);
    }
    return true;
}

bool cmdResume(const std::string&, const std::string& args,
               CLFAgentLoop& agent, const std::string& historyDir,
               ICLFOutput* output) {
    auto sessions = CLFSessionManager::list(historyDir, 10);
    if (sessions.empty()) {
        if (output) output->emitContent("  ⎿ 暂无已保存的会话\n");
        return true;
    }
    if (args.empty()) {
        std::string out = "\n● 会话列表\n";
        for (size_t i = 0; i < sessions.size(); ++i)
            out += "  ⎿ [" + std::to_string(i + 1) + "] "
                + sessions[i].m_savedAt + "  " + sessions[i].m_title + "\n";
        if (output) output->emitContent(out);
    } else {
        int idx = 0;
        try { idx = std::stoi(args); } catch (...) {}
        if (idx >= 1 && idx <= static_cast<int>(sessions.size())) {
            if (agent.restoreSession(sessions[idx - 1].m_path)) {
                if (output) output->emitContent("✓ 会话已恢复: " + sessions[idx - 1].m_title + "\n");
            } else {
                if (output) output->emitContent("✗ 恢复失败\n");
            }
        } else {
            if (output) output->emitContent("✗ 无效序号: " + args + "\n");
        }
    }
    return true;
}

} // anonymous namespace

// ============================================================================
// 注册入口
// ============================================================================

void registerBuiltinCommands(CLFCommandDispatcher& dispatcher) {
    auto reg = [&](std::string name, std::string desc, CLFCommandHandler handler) {
        CLFCommand cmd;
        cmd.m_name        = std::move(name);
        cmd.m_description = std::move(desc);
        cmd.m_handler     = std::move(handler);
        dispatcher.registerCommand(std::move(cmd));
    };
    reg("/exit",    "退出并保存会话",                       cmdExit);
    reg("/help",    "显示帮助信息",                         cmdHelp);
    reg("/clear",   "保存会话并开始新对话",                 cmdClear);
    reg("/model",   "显示当前模型信息",                     cmdModel);
    reg("/mode",    "切换安全模式 /mode <auto|analyze|edit|manual>", cmdMode);
    reg("/config",  "显示当前配置信息",                     cmdConfig);
    reg("/context", "显示上下文用量",                       cmdContext);
    reg("/skill",   "知识库管理 /skill [list|<name>]",     cmdSkill);
    reg("/history", "显示最近保存的会话",                   cmdHistory);
    reg("/resume",  "恢复指定会话 /resume <n>",             cmdResume);
}

} // namespace CLF::CLFUI
