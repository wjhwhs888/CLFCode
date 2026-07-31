#include <algorithm>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"
#include "CLFTools/CLFBuiltinTools.hpp"

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

    // 确定项目根目录（从 exe 向上找 CMakeLists.txt）
    std::string projectRoot = CLF::CLFCore::CLFConfigLoader::findProjectRoot();
    std::cout << "CLFCode — CLI Agent Framework for Code" << std::endl;
    std::cout << "Project root: " << projectRoot << std::endl;

    // 加载配置（优先 .local.json → 环境变量 → agent_settings.json）
    CLF::CLFCore::CLFAgentConfig config;
    std::string configPath;

    std::string localPath = projectRoot + "/config/agent_settings.local.json";
    bool loaded = CLF::CLFCore::CLFConfigLoader::loadFromFileWithEnv(localPath, config);
    if (loaded) {
        configPath = localPath;
    } else {
        std::string defaultPath = projectRoot + "/config/agent_settings.json";
        loaded = CLF::CLFCore::CLFConfigLoader::loadFromFileWithEnv(defaultPath, config);
        if (loaded) configPath = defaultPath;
    }

    // 初始化日志系统（基于配置的 logging 段）
    CLF::CLFCore::CLFLogger::instance().init(
        CLF::CLFCore::CLFLogger::levelFromString(config.m_logLevel),
        projectRoot + "/" + config.m_logFile,
        config.m_logConsole
    );
    CLF::CLFCore::CLFLogger::instance().info("CLFCode starting, project root: " + projectRoot);

    if (loaded) {
        std::cout << "Config loaded: " << configPath << std::endl;
    } else {
        CLF::CLFCore::CLFLogger::instance().warn("No config file found, using defaults.");
    }

    if (config.m_apiKey.empty()) {
        CLF::CLFCore::CLFLogger::instance().error(
            "API Key is required. Set CLF_API_KEY or create config/agent_settings.local.json");
        return 1;
    }

    std::cout << "Model: " << config.m_modelName << std::endl;
    std::cout << "Type /exit to quit, /help for commands" << std::endl;
    std::cout << std::endl;

    // 创建 Agent 并注册全部内置工具
    CLF::CLFCore::CLFAgentLoop agent(config);
    CLF::CLFTools::registerBuiltinTools(agent);

    // 注入高风险工具确认回调（终端 y/n 交互）
    agent.setConfirmCallback([](const std::string& prompt) {
        std::cout << std::endl << "[安全确认] " << prompt << std::endl;
        std::cout << "允许执行该操作？(y/n): " << std::flush;
        std::string answer;
        std::getline(std::cin, answer);
        return answer == "y" || answer == "Y" || answer == "yes";
    });

    std::cout << "Security mode: " << agent.getSecurityModeName() << std::endl;

    // 会话目录（崩溃恢复 + 历史）
    std::string historyDir = projectRoot + "/doc/contextHistory";
    CLF::CLFCore::CLFSessionManager::cleanupOld(historyDir, 30); // 30 天自动清理

    // 崩溃恢复检测：存在未完成会话 → 询问是否恢复
    std::string incompletePath = CLF::CLFCore::CLFSessionManager::findIncomplete(historyDir);
    if (!incompletePath.empty()) {
        std::cout << "[提示] 检测到上次会话未正常结束。" << std::endl;
        std::cout << "是否恢复该会话？(y/n): " << std::flush;
        std::string answer;
        std::getline(std::cin, answer);
        if (answer == "y" || answer == "Y" || answer == "yes") {
            if (agent.restoreSession(incompletePath)) {
                std::cout << "会话已恢复。" << std::endl;
            } else {
                std::cout << "会话恢复失败（文件损坏？）。" << std::endl;
            }
            CLF::CLFCore::CLFSessionManager::promote(incompletePath); // 转正
        } else {
            CLF::CLFCore::CLFSessionManager::remove(incompletePath);
            std::cout << "未完成的会话已丢弃。" << std::endl;
        }
    }

    // 加载知识库（Skills）
    std::string skillDir = projectRoot + "/data/skills";
    int skillCount = CLF::CLFCore::CLFSkillLoader::loadFromDir(skillDir);
    if (skillCount > 0) {
        std::cout << "Skills loaded: " << skillCount << std::endl;
        auto names = CLF::CLFCore::CLFSkillLoader::listNames();
        for (const auto& n : names) {
            std::cout << "  - " << n << std::endl;
        }
    }
    std::cout << std::endl;

    // REPL
    std::string input;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, input)) {
            break;
        }

        if (input.empty()) continue;

        if (input == "/exit") {
            // 保存正式会话 + 清理 incomplete
            agent.saveSession(historyDir, false);
            CLF::CLFCore::CLFSessionManager::remove(
                CLF::CLFCore::CLFSessionManager::findIncomplete(historyDir));
            std::cout << "会话已保存。" << std::endl;
            std::cout << "Goodbye." << std::endl;
            break;
        }
        if (input == "/help") {
            std::cout << "Commands:" << std::endl
                      << "  /exit    - quit (save session)" << std::endl
                      << "  /help    - show this help" << std::endl
                      << "  /clear   - save session & start new" << std::endl
                      << "  /skill   - list skills (with load status) or load one" << std::endl
                      << "  /mode    - switch security mode (auto/analyze/edit/manual)" << std::endl
                      << "  /history - list recent sessions" << std::endl
                      << "  /resume  - resume a session (/resume list | /resume <n>)" << std::endl
                      << "  /model   - show current model" << std::endl
                      << "  /config  - show current config" << std::endl;
            continue;
        }

        if (input == "/history") {
            auto sessions = CLF::CLFCore::CLFSessionManager::list(historyDir, 10);
            if (sessions.empty()) {
                std::cout << "No saved sessions yet." << std::endl;
            } else {
                std::cout << "Recent sessions:" << std::endl;
                for (const auto& s : sessions) {
                    std::cout << "  " << s.m_savedAt << " | " << s.m_title << std::endl;
                }
            }
            continue;
        }

        if (input == "/model") {
            const auto& cfg = agent.getConfig();
            std::cout << "Current model:   " << cfg.m_modelName << std::endl;
            std::cout << "Sub model:       " << cfg.m_subModel << std::endl;
            std::cout << std::endl;
            std::cout << "Available models (edit config/agent_settings.json):" << std::endl;
            std::cout << "  deepseek-v4-flash  - 主模型（Agent 能力最强）" << std::endl;
            std::cout << "  deepseek-v4-pro    - Pro（正式版待发布）" << std::endl;
            continue;
        }

        if (input == "/config") {
            const auto& cfg = agent.getConfig();
            std::cout << "── 配置信息 ─────────────────────────────" << std::endl;
            std::cout << "连接:    " << cfg.m_apiBaseUrl << std::endl;
            std::cout << "模型:    " << cfg.m_modelName
                      << " (副: " << cfg.m_subModel << ")" << std::endl;
            std::cout << "参数:    temperature=" << cfg.m_temperature
                      << " top_p=" << cfg.m_topP
                      << " max_tokens=" << cfg.m_maxTokens << std::endl;
            std::cout << "流式:    " << (cfg.m_stream ? "开" : "关") << std::endl;
            std::cout << "安全模式: " << agent.getSecurityModeName() << std::endl;
            std::cout << "上下文:  " << cfg.m_maxContextWindow
                      << " tokens (压缩: " << (cfg.m_contextCompression ? "开" : "关") << ")"
                      << std::endl;
            std::cout << "语言:    " << cfg.m_interactionLanguage << std::endl;
            std::cout << "日志:    " << cfg.m_logFile << std::endl;
            continue;
        }

        if (input.rfind("/resume", 0) == 0) {
            auto sessions = CLF::CLFCore::CLFSessionManager::list(historyDir, 10);
            if (sessions.empty()) {
                std::cout << "No saved sessions." << std::endl;
                continue;
            }

            std::string arg = input.size() > 8 ? input.substr(8) : "";
            while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);

            if (arg.empty()) {
                // 无参数：列出带序号的会话
                std::cout << "Sessions:" << std::endl;
                for (size_t i = 0; i < sessions.size(); ++i) {
                    std::cout << "  [" << i + 1 << "] " << sessions[i].m_savedAt
                              << " | " << sessions[i].m_title << std::endl;
                }
                std::cout << "Usage: /resume <n>  (恢复第 n 条会话)" << std::endl;
            } else {
                // 按序号恢复
                int idx = 0;
                try { idx = std::stoi(arg); } catch (...) {}
                if (idx < 1 || idx > static_cast<int>(sessions.size())) {
                    std::cout << "Invalid session number: " << arg << std::endl;
                } else {
                    const auto& s = sessions[idx - 1];
                    if (agent.restoreSession(s.m_path)) {
                        std::cout << "会话已恢复: " << s.m_title << std::endl;
                    } else {
                        std::cout << "会话恢复失败（文件损坏？）。" << std::endl;
                    }
                }
            }
            continue;
        }

        if (input.rfind("/mode", 0) == 0) {
            std::string arg = input.size() > 6 ? input.substr(6) : "";
            while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);

            if (arg.empty()) {
                std::cout << "Current security mode: " << agent.getSecurityModeName() << std::endl;
                std::cout << "Usage: /mode <auto|analyze|edit|manual>" << std::endl;
            } else {
                auto mode = CLF::CLFCore::CLFSecurityPolicy::modeFromString(arg);
                agent.setSecurityMode(mode);
                std::cout << "Security mode switched to: " << agent.getSecurityModeName() << std::endl;
                if (mode == CLF::CLFCore::CLFSecurityMode::Analyze) {
                    std::cout << "  (写操作和命令执行将被阻断)" << std::endl;
                }
            }
            continue;
        }

        if (input.rfind("/skill", 0) == 0) {
            std::string arg = input.size() > 7 ? input.substr(7) : "";
            // 去除前导空格
            while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);

            if (arg.empty() || arg == "list") {
                auto names = CLF::CLFCore::CLFSkillLoader::listNames();
                auto loaded = agent.getLoadedSkills();
                std::cout << "Available skills:" << std::endl;
                for (const auto& n : names) {
                    bool isLoaded = std::find(loaded.begin(), loaded.end(), n) != loaded.end();
                    std::cout << "  " << n << (isLoaded ? "  [已加载]" : "") << std::endl;
                }
                std::cout << "  constitution  [常驻]  (L1 编码宪法，始终注入)" << std::endl;
                std::cout << "Usage: /skill <name>" << std::endl;
            } else {
                std::string content = CLF::CLFCore::CLFSkillLoader::getContent(arg);
                if (content.empty()) {
                    std::cout << "Skill not found: " << arg << std::endl;
                    std::cout << "Use /skill list to see available skills." << std::endl;
                } else {
                    agent.injectSkillToContext(arg, content);
                    std::cout << "Skill loaded: " << arg << std::endl;
                }
            }
            continue;
        }
        if (input == "/clear") {
            // 先保存当前会话为正式会话，再开始新会话
            agent.saveSession(historyDir, false);
            CLF::CLFCore::CLFSessionManager::remove(
                CLF::CLFCore::CLFSessionManager::findIncomplete(historyDir));
            agent.clearContext();
            std::cout << "会话已保存，新会话开始。" << std::endl;
            continue;
        }

        try {
            std::string response = agent.runTurn(input);
            if (!response.empty()) {
                std::cout << response << std::endl;
            }
        } catch (const std::exception& e) {
            CLF::CLFCore::CLFLogger::instance().error(std::string("Fatal: ") + e.what());
            agent.clearContext();
        }

        // 每轮对话后自动存盘（incomplete 状态，崩溃可恢复）
        agent.saveSession(historyDir, true);
        std::cout << std::endl;
    }

    return 0;
}
