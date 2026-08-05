// CLFCommandDispatcher.cpp — REPL 命令分发实现

#include "CLFUI/CLFCommandDispatcher.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"

#include <algorithm>

namespace CLF::CLFUI {
using namespace CLF::CLFCore;

CLFCommandDispatcher::CLFCommandDispatcher(CLF::CLFCore::CLFAgentLoop& agent,
                                           const std::string& historyDir,
                                           CLF::CLFTypes::ICLFOutput* output,
                                           std::function<void()> onExit)
    : m_agent(agent)
    , m_historyDir(historyDir)
    , m_modeName(agent.getSecurityModeName())
    , m_output(output)
    , m_onExit(std::move(onExit)) {
}

bool CLFCommandDispatcher::handle(const std::string& input) {

    if (input == "/exit") {
        m_agent.saveSession(m_historyDir, false);
        CLFSessionManager::removeAllIncomplete(m_historyDir);
        if (m_output) m_output->emitContent("● 会话已保存。再见 — CLFCode\n");
        if (m_onExit) m_onExit();  // → screen.ExitLoopClosure()
        return true;
    }
    if (input == "/help") {
        if (m_output) m_output->emitContent(
            "\n  ⎿ /exit   退出 | /clear 新会话 | /skill 知识库\n"
            "  ⎿ /mode   安全模式 | /history 会话 | /resume <n> 恢复\n"
            "  ⎿ /model  模型 | /config 配置 | /context 用量\n"
            "  ⎿ Shift+Tab 切换模式 | Esc 清空输入\n");
        return true;
    }
    if (input == "/clear") {
        m_agent.saveSession(m_historyDir, false);
        CLFSessionManager::removeAllIncomplete(m_historyDir);
        m_agent.clearContext();
        if (m_output) m_output->emitContent("✓ 会话已保存，新会话开始\n");
        return true;
    }
    if (input == "/model") {
        const auto& cfg = m_agent.getConfig();
        if (m_output) m_output->emitContent(
            "\n● 当前模型\n"
            "  ⎿ 主模型: " + cfg.m_modelName + "\n"
            "  ⎿ 副模型: " + cfg.m_subModel + "\n");
        return true;
    }
    if (input.rfind("/mode", 0) == 0) {
        std::string arg = input.size() > 6 ? input.substr(6) : "";
        while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);
        if (!arg.empty()) {
            m_modeName = arg;
            m_agent.setSecurityMode(CLFSecurityPolicy::modeFromString(arg));
        }
        if (m_output) m_output->emitContent("● 模式: " + m_modeName + "\n");
        return true;
    }
    if (input == "/config") {
        const auto& cfg = m_agent.getConfig();
        if (m_output) m_output->emitContent(
            "\n● 配置信息\n"
            "  ⎿ 连接: " + cfg.m_apiBaseUrl + "\n"
            "  ⎿ 模型: " + cfg.m_modelName + " (副: " + cfg.m_subModel + ")\n"
            "  ⎿ 参数: temperature=" + std::to_string(cfg.m_temperature)
                + " top_p=" + std::to_string(cfg.m_topP)
                + " max_tokens=" + std::to_string(cfg.m_maxTokens) + "\n"
            "  ⎿ 流式: " + (cfg.m_stream ? "开" : "关")
                + " | 安全: " + m_modeName + "\n"
            "  ⎿ 上下文: " + std::to_string(cfg.m_maxContextWindow) + " tokens\n");
        return true;
    }
    if (input == "/context") {
        const auto& ctx = m_agent.getContext();
        const auto& cfg = m_agent.getConfig();
        int used = ctx.estimateTokens();
        int max  = cfg.m_maxContextWindow;
        int pct  = (max > 0) ? (used * 100 / max) : 0;
        int bars = (max > 0) ? (used * 20 / max) : 0;
        if (bars > 20) bars = 20;
        std::string bar;
        for (int i = 0; i < 20; ++i)
            bar += (i < bars) ? "█" : "░";
        if (m_output) m_output->emitContent(
            "\n● 上下文用量\n"
            "  ⎿ 用量: " + std::to_string(used) + " / "
                + std::to_string(max) + " tokens"
                + " (" + std::to_string(pct) + "%)\n"
            "  ⎿ [" + bar + "]\n"
            "  ⎿ 剩余: ~" + std::to_string(max - used) + " tokens"
                + (pct >= 80 ? "  ⚠ 建议 /clear" : "") + "\n");
        return true;
    }
    if (input.rfind("/skill", 0) == 0) {
        std::string arg = input.size() > 7 ? input.substr(7) : "";
        while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);
        if (arg.empty() || arg == "list") {
            auto names = CLFSkillLoader::listNames();
            auto loaded = m_agent.getLoadedSkills();
            std::string out = "\n● 知识库\n";
            for (const auto& n : names) {
                bool isLoaded = std::find(loaded.begin(), loaded.end(), n) != loaded.end();
                out += "  ⎿ " + n + (isLoaded ? "  [已加载]\n" : "  [未加载]\n");
            }
            if (m_output) m_output->emitContent(out);
        } else {
            std::string content = CLFSkillLoader::getContent(arg);
            if (content.empty()) {
                if (m_output) m_output->emitContent("✗ 未找到: " + arg + "\n");
            } else {
                m_agent.injectSkillToContext(arg, content);
                if (m_output) m_output->emitContent("✓ 已加载: " + arg + "\n");
            }
        }
        return true;
    }
    if (input == "/history") {
        auto sessions = CLFSessionManager::list(m_historyDir, 10);
        if (sessions.empty()) {
            if (m_output) m_output->emitContent("  ⎿ 暂无已保存的会话\n");
        } else {
            std::string out = "\n● 会话列表\n";
            for (const auto& s : sessions)
                out += "  ⎿ " + s.m_savedAt + "  " + s.m_title + "\n";
            if (m_output) m_output->emitContent(out);
        }
        return true;
    }
    if (input.rfind("/resume", 0) == 0) {
        auto sessions = CLFSessionManager::list(m_historyDir, 10);
        if (sessions.empty()) {
            if (m_output) m_output->emitContent("  ⎿ 暂无已保存的会话\n");
            return true;
        }
        std::string arg = input.size() > 8 ? input.substr(8) : "";
        while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);
        int idx = 0;
        try { idx = std::stoi(arg); } catch (...) {}
        if (arg.empty()) {
            std::string out = "\n● 会话列表\n";
            for (size_t i = 0; i < sessions.size(); ++i)
                out += "  ⎿ [" + std::to_string(i + 1) + "] "
                    + sessions[i].m_savedAt + "  " + sessions[i].m_title + "\n";
            if (m_output) m_output->emitContent(out);
        } else if (idx >= 1 && idx <= static_cast<int>(sessions.size())) {
            if (m_agent.restoreSession(sessions[idx - 1].m_path)) {
                if (m_output) m_output->emitContent("✓ 会话已恢复: " + sessions[idx - 1].m_title + "\n");
            } else {
                if (m_output) m_output->emitContent("✗ 恢复失败\n");
            }
        } else {
            if (m_output) m_output->emitContent("✗ 无效序号: " + arg + "\n");
        }
        return true;
    }
    return false;
}

} // namespace CLF::CLFUI
