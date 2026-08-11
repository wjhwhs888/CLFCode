#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFUI/CLFRepl.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFUI/CLFTerminal.hpp"
#include "CLFTools/CLFBuiltinTools.hpp"

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
    CLF::CLFUI::CLFTerminal::enableAnsi();

    // 1. 加载配置（优先 .local.json → 环境变量 → agent_settings.json）
    CLF::CLFCore::CLFAgentConfig config;
    std::string projectRoot = CLF::CLFCore::CLFConfigLoader::findProjectRoot();
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

    // 2. 初始化日志系统（启动时轮转：上次日志 → .old，本次重新开始）
    std::string logPath = projectRoot + "/" + config.m_logFile;
    std::error_code ec;
    if (std::filesystem::exists(logPath, ec) && std::filesystem::file_size(logPath, ec) > 0) {
        std::filesystem::rename(logPath, logPath + ".old", ec);
    }
    CLF::CLFCore::CLFLogger::instance().init(
        CLF::CLFCore::CLFLogger::levelFromString(config.m_logLevel),
        logPath,
        config.m_logConsole
    );
    CLF::CLFCore::CLFLogger::instance().info("CLFCode starting, project root: " + projectRoot);

    if (!loaded) {
        CLF::CLFCore::CLFLogger::instance().warn("No config file found, using defaults.");
    }

    if (config.m_apiKey.empty()) {
        CLF::CLFCore::CLFLogger::instance().error(
            "API Key is required. Set CLF_API_KEY or create config/agent_settings.local.json");
        std::cout << "[Error] API Key is required. Exiting." << std::endl;
        return 1;
    }

    // 3. 创建 Terminal (ICLFOutput 实现) + Agent + 注入
    CLF::CLFUI::CLFTerminal terminal;
    CLF::CLFCore::CLFAgentLoop agent(config);
    agent.setOutput(&terminal);               // Agent → ICLFOutput
    CLF::CLFTools::registerBuiltinTools(agent);

    // 4. 启动 REPL
    std::string historyDir = projectRoot + "/doc/contextHistory";
    CLF::CLFCore::CLFSessionManager::cleanupOld(historyDir, 30);

    CLF::CLFUI::CLFRepl repl(agent, historyDir, &terminal);
    return repl.run();
}
