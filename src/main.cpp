#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFRepl.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFTerminal.hpp"
#include "CLFTools/CLFBuiltinTools.hpp"

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
    CLF::CLFCore::CLFTerminal::enableAnsi();

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

    // 2. 初始化日志系统
    CLF::CLFCore::CLFLogger::instance().init(
        CLF::CLFCore::CLFLogger::levelFromString(config.m_logLevel),
        projectRoot + "/" + config.m_logFile,
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

    // 3. 创建 Agent + 注册内置工具（确认/状态回调由 CLFRepl 注入）
    CLF::CLFCore::CLFAgentLoop agent(config);
    CLF::CLFTools::registerBuiltinTools(agent);

    // 4. 启动 REPL（5 区 UI）
    std::string historyDir = projectRoot + "/doc/contextHistory";
    CLF::CLFCore::CLFSessionManager::cleanupOld(historyDir, 30);

    CLF::CLFCore::CLFRepl repl(agent, historyDir);
    return repl.run();
}
