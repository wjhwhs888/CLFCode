#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFTools/CLFBuiltinTools.hpp"

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "CLFCode — CLI Agent Framework for Code" << std::endl;

    // 加载配置（优先 .local.json → 环境变量 → agent_settings.json）
    CLF::CLFCore::CLFAgentConfig config;
    std::string configPath;

    // 1. 尝试本地覆盖文件（含 API Key，不提交 Git）
    std::string localPath =
        CLF::CLFCore::CLFConfigLoader::resolveConfigPath("config/agent_settings.local.json");
    bool loaded = CLF::CLFCore::CLFConfigLoader::loadFromFileWithEnv(localPath, config);
    if (loaded) {
        configPath = localPath;
    } else {
        // 2. 回退到模板配置 + 环境变量
        std::string defaultPath =
            CLF::CLFCore::CLFConfigLoader::resolveConfigPath("config/agent_settings.json");
        loaded = CLF::CLFCore::CLFConfigLoader::loadFromFileWithEnv(defaultPath, config);
        if (loaded) configPath = defaultPath;
    }

    if (loaded) {
        std::cout << "Config loaded: " << configPath << std::endl;
    } else {
        std::cerr << "[Warning] No config file found, using defaults." << std::endl;
    }

    if (config.m_apiKey.empty()) {
        std::cerr << "[Error] API Key is required." << std::endl
                  << "  Set CLF_API_KEY environment variable," << std::endl
                  << "  or create config/agent_settings.local.json with your api_key" << std::endl;
        return 1;
    }

    std::cout << "Model: " << config.m_modelName << std::endl;
    std::cout << "Type /exit to quit, /help for commands" << std::endl;
    std::cout << std::endl;

    // 创建 Agent 并注册全部内置工具
    CLF::CLFCore::CLFAgentLoop agent(config);
    CLF::CLFTools::registerBuiltinTools(agent);

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
                      << "  /exit  - quit" << std::endl
                      << "  /help  - show this help" << std::endl
                      << "  /clear - clear context" << std::endl;
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
            std::cerr << "[Fatal] " << e.what() << std::endl;
            agent.clearContext();
        }
        std::cout << std::endl;
    }

    return 0;
}
