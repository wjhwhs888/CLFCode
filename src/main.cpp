#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFLogger.hpp"
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
            std::cout << "Goodbye." << std::endl;
            break;
        }
        if (input == "/help") {
            std::cout << "Commands:" << std::endl
                      << "  /exit   - quit" << std::endl
                      << "  /help   - show this help" << std::endl
                      << "  /clear  - clear context" << std::endl
                      << "  /skill  - list or load skills" << std::endl;
            continue;
        }

        if (input.rfind("/skill", 0) == 0) {
            std::string arg = input.size() > 7 ? input.substr(7) : "";
            // 去除前导空格
            while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);

            if (arg.empty() || arg == "list") {
                auto names = CLF::CLFCore::CLFSkillLoader::listNames();
                std::cout << "Available skills:" << std::endl;
                for (const auto& n : names) {
                    std::cout << "  " << n << std::endl;
                }
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
            agent.clearContext();
            std::cout << "Context cleared." << std::endl;
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
        std::cout << std::endl;
    }

    return 0;
}
