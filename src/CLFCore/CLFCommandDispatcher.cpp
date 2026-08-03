// CLFCommandDispatcher.cpp — REPL 命令分发实现

#include "CLFCore/CLFCommandDispatcher.hpp"
#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"
#include "CLFCore/CLFTerminal.hpp"

#include <algorithm>
#include <iostream>

namespace CLF::CLFCore {

CLFCommandDispatcher::CLFCommandDispatcher(CLFAgentLoop& agent,
                                           const std::string& historyDir)
    : m_agent(agent)
    , m_historyDir(historyDir)
    , m_modeName(agent.getSecurityModeName()) {
}

bool CLFCommandDispatcher::handle(const std::string& input) {

    if (input == "/exit") {
        m_agent.saveSession(m_historyDir, false);
        CLFSessionManager::removeAllIncomplete(m_historyDir);
        CLFTerminal::restoreScrollRegion();
        std::cout << "● 会话已保存。再见 — CLFCode" << std::endl;
        std::exit(0);
    }
    if (input == "/help") {
        CLFTerminal::scrollPrint("\n  ⎿ /exit   退出 | /clear 新会话 | /skill 知识库\n");
        CLFTerminal::scrollPrint("  ⎿ /mode   安全模式 | /history 会话 | /resume <n> 恢复\n");
        CLFTerminal::scrollPrint("  ⎿ /model  模型 | /config 配置 | /context 用量\n");
        CLFTerminal::scrollPrint("  ⎿ Shift+Tab 切换模式 | Esc 清空输入\n");
        return true;
    }
    if (input == "/clear") {
        m_agent.saveSession(m_historyDir, false);
        CLFSessionManager::removeAllIncomplete(m_historyDir);
        m_agent.clearContext();
        CLFTerminal::scrollPrint(CLFTerminal::green("✓ 会话已保存，新会话开始") + "\n");
        return true;
    }
    if (input == "/model") {
        const auto& cfg = m_agent.getConfig();
        CLFTerminal::scrollPrint("\n● 当前模型\n");
        CLFTerminal::scrollPrint("  ⎿ 主模型: " + CLFTerminal::cyan(cfg.m_modelName) + "\n");
        CLFTerminal::scrollPrint("  ⎿ 副模型: " + CLFTerminal::cyan(cfg.m_subModel) + "\n");
        return true;
    }
    if (input.rfind("/mode", 0) == 0) {
        std::string arg = input.size() > 6 ? input.substr(6) : "";
        while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);
        if (!arg.empty()) {
            m_modeName = arg;
            m_agent.setSecurityMode(CLFSecurityPolicy::modeFromString(arg));
            CLFTerminal::drawModeArea(m_modeName);
        }
        CLFTerminal::scrollPrint(CLFTerminal::cyan("● 模式: " + m_modeName) + "\n");
        return true;
    }
    if (input == "/config") {
        const auto& cfg = m_agent.getConfig();
        CLFTerminal::scrollPrint("\n● 配置信息\n");
        CLFTerminal::scrollPrint("  ⎿ 连接: " + CLFTerminal::cyan(cfg.m_apiBaseUrl) + "\n");
        CLFTerminal::scrollPrint("  ⎿ 模型: " + CLFTerminal::cyan(cfg.m_modelName)
                                 + " (副: " + cfg.m_subModel + ")\n");
        CLFTerminal::scrollPrint("  ⎿ 参数: temperature=" + std::to_string(cfg.m_temperature)
                                 + " top_p=" + std::to_string(cfg.m_topP)
                                 + " max_tokens=" + std::to_string(cfg.m_maxTokens) + "\n");
        CLFTerminal::scrollPrint("  ⎿ 流式: " + std::string(cfg.m_stream ? "开" : "关")
                                 + " | 安全: " + m_modeName + "\n");
        CLFTerminal::scrollPrint("  ⎿ 上下文: " + std::to_string(cfg.m_maxContextWindow)
                                 + " tokens\n");
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
        for (int i = 0; i < 20; ++i) {
            if (i < bars)
                bar += (pct >= 80) ? CLFTerminal::red("█")
                     : (pct >= 50) ? CLFTerminal::yellow("█")
                     :               CLFTerminal::green("█");
            else
                bar += CLFTerminal::gray("░");
        }

        CLFTerminal::scrollPrint("\n● 上下文用量\n");
        CLFTerminal::scrollPrint("  ⎿ 用量: " + std::to_string(used) + " / "
                                 + std::to_string(max) + " tokens"
                                 + " (" + std::to_string(pct) + "%)\n");
        CLFTerminal::scrollPrint("  ⎿ [" + bar + "]\n");
        CLFTerminal::scrollPrint("  ⎿ 剩余: ~" + std::to_string(max - used) + " tokens");
        if (pct >= 80)
            CLFTerminal::scrollPrint("  " + CLFTerminal::yellow("⚠ 建议 /clear"));
        CLFTerminal::scrollPrint("\n");
        return true;
    }
    if (input.rfind("/skill", 0) == 0) {
        std::string arg = input.size() > 7 ? input.substr(7) : "";
        while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);
        if (arg.empty() || arg == "list") {
            auto names = CLFSkillLoader::listNames();
            auto loaded = m_agent.getLoadedSkills();
            CLFTerminal::scrollPrint("\n● 知识库\n");
            for (const auto& n : names) {
                bool isLoaded = std::find(loaded.begin(), loaded.end(), n) != loaded.end();
                CLFTerminal::scrollPrint("  ⎿ " + n
                    + (isLoaded ? CLFTerminal::green("  [已加载]")
                                : CLFTerminal::gray("  [未加载]")) + "\n");
            }
        } else {
            std::string content = CLFSkillLoader::getContent(arg);
            if (content.empty()) {
                CLFTerminal::scrollPrint(CLFTerminal::red("✗ 未找到: " + arg) + "\n");
            } else {
                m_agent.injectSkillToContext(arg, content);
                CLFTerminal::scrollPrint(CLFTerminal::green("✓ 已加载: " + arg) + "\n");
            }
        }
        return true;
    }
    if (input == "/history") {
        auto sessions = CLFSessionManager::list(m_historyDir, 10);
        if (sessions.empty()) {
            CLFTerminal::scrollPrint("  ⎿ 暂无已保存的会话\n");
        } else {
            CLFTerminal::scrollPrint("\n● 会话列表\n");
            for (const auto& s : sessions) {
                CLFTerminal::scrollPrint("  ⎿ " + CLFTerminal::cyan(s.m_savedAt)
                                         + "  " + s.m_title + "\n");
            }
        }
        return true;
    }
    if (input.rfind("/resume", 0) == 0) {
        auto sessions = CLFSessionManager::list(m_historyDir, 10);
        if (sessions.empty()) {
            CLFTerminal::scrollPrint("  ⎿ 暂无已保存的会话\n");
            return true;
        }
        std::string arg = input.size() > 8 ? input.substr(8) : "";
        while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);
        int idx = 0;
        try { idx = std::stoi(arg); } catch (...) {}
        if (arg.empty()) {
            CLFTerminal::scrollPrint("\n● 会话列表\n");
            for (size_t i = 0; i < sessions.size(); ++i) {
                CLFTerminal::scrollPrint("  ⎿ " + CLFTerminal::cyan(
                    "[" + std::to_string(i + 1) + "]")
                    + " " + CLFTerminal::gray(sessions[i].m_savedAt)
                    + "  " + sessions[i].m_title + "\n");
            }
        } else if (idx >= 1 && idx <= static_cast<int>(sessions.size())) {
            if (m_agent.restoreSession(sessions[idx - 1].m_path)) {
                CLFTerminal::scrollPrint(CLFTerminal::green("✓ 会话已恢复: ")
                                         + sessions[idx - 1].m_title + "\n");
            } else {
                CLFTerminal::scrollPrint(CLFTerminal::red("✗ 恢复失败") + "\n");
            }
        } else {
            CLFTerminal::scrollPrint(CLFTerminal::red("✗ 无效序号: " + arg) + "\n");
        }
        return true;
    }
    return false;
}

} // namespace CLF::CLFCore
