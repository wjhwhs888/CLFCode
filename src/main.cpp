#include <filesystem>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFArgParser.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFUI/CLFRepl.hpp"
#include "CLFUI/CLFTerminal.hpp"
#include "CLFTools/CLFBuiltinTools.hpp"

namespace {

void printHelp() {
    std::cout << "CLFCode — CLI Agent Framework for Code\n\n"
              << "Usage: CLFCode [options]\n\n"
              << "Options:\n"
              << "  --help, -h            Show this help\n"
              << "  --version, -v         Show version\n"
              << "  --config <path>       Config file path\n"
              << "  --project-root <path> Workspace root directory\n"
              << "  --prompt <text>       Non-interactive mode, single prompt\n"
              << "  --prompt-file <path>  Read prompt from file (non-interactive)\n"
              << "  --allow-write         Allow write/exec in non-interactive mode\n";
}

void printVersion() {
    std::error_code ec;
    std::string verPath = CLF::CLFCore::CLFConfigLoader::resolvePath("VERSION");
    if (std::filesystem::exists(verPath, ec)) {
        std::ifstream f(verPath);
        std::string v;
        std::getline(f, v);
        std::cout << v << std::endl;
    } else {
        std::cout << "unknown" << std::endl;
    }
}

} // anonymous namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

    // C: 全局 terminate 兜底——任何线程的未处理异常在触发 std::terminate 前留痕。
    //     默认行为（abort，退出码 3）保持不变，但日志可定位；handler 保持极简
    //     （运行在异常线程上）。先写 stderr（无锁、立即可见），再写日志文件
    //     （CLFLogger 每行 flush，abort 前已落盘；若日志互斥锁被异常线程持有导致
    //     死锁，cerr 已先行输出，进程仍按 abort 语义退出）。
    std::set_terminate([]() {
        std::cerr << "[Terminate] unhandled exception in thread — process aborting"
                  << std::endl;
        try {
            CLF::CLFCore::CLFLogger::instance().error(
                "[Terminate] unhandled exception in thread — process aborting");
        } catch (...) {}
        std::abort();
    });

    // 0. 解析 CLI 参数
    CLF::CLFCore::CLFLaunchArgs args;
    if (!CLF::CLFCore::CLFArgParser::parse(argc, argv, args)) {
        return 1;
    }
    if (args.showHelp)    { printHelp();    return 0; }
    if (args.showVersion) { printVersion(); return 0; }

    // 1. 加载配置
    CLF::CLFCore::CLFAgentConfig config;
    std::string projectRoot = args.projectRoot.empty()
        ? CLF::CLFCore::CLFConfigLoader::findProjectRoot()
        : args.projectRoot;

    std::string configPath = args.configPath.empty()
        ? projectRoot + "/config/agent_settings.local.json"
        : args.configPath;
    bool loaded = CLF::CLFCore::CLFConfigLoader::loadFromFileWithEnv(configPath, config);
    if (!loaded) {
        configPath = projectRoot + "/config/agent_settings.json";
        loaded = CLF::CLFCore::CLFConfigLoader::loadFromFileWithEnv(configPath, config);
    }

    // 2. 日志
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
        std::cerr << "[Error] API Key is required. Exiting." << std::endl;
        return 1;
    }

    // 3. 非交互模式判定 + prompt 来源
    bool nonInteractive = !args.oneShotPrompt.empty() || !args.promptFilePath.empty();
    std::string prompt;
    if (nonInteractive) {
        if (!args.oneShotPrompt.empty()) {
            prompt = args.oneShotPrompt;
        } else {
            std::ifstream file(args.promptFilePath);
            if (!file.is_open()) {
                std::cerr << "[Error] Cannot open prompt file: " << args.promptFilePath << std::endl;
                return 1;
            }
            std::ostringstream oss;
            oss << file.rdbuf();
            prompt = oss.str();
        }
        // 非交互模式关流式（流式路径内容走 m_output，无 output 会丢失返回值）
        // 非交互模式关流式（流式路径内容走 m_output，无 output 会丢失返回值）
        config.m_stream = false;
    }

    // 4. 创建 Agent
    CLF::CLFCore::CLFAgentLoop agent(config);
    CLF::CLFTools::registerBuiltinTools(agent);

    if (nonInteractive) {
        agent.setSecurityMode(args.allowWrite
            ? CLF::CLFCore::CLFSecurityMode::Auto
            : CLF::CLFCore::CLFSecurityMode::Analyze);
        std::string response = agent.runTurn(prompt);
        if (!response.empty()) std::cout << response << std::endl;
        return 0;
    }

    // 交互模式
    CLF::CLFUI::CLFTerminal terminal;
    agent.setOutput(&terminal);

    std::string historyDir = projectRoot + "/doc/contextHistory";
    CLF::CLFCore::CLFSessionManager::migrateLegacyIncomplete(historyDir);
    CLF::CLFCore::CLFSessionManager::cleanupOld(historyDir, 30);

    CLF::CLFUI::CLFRepl repl(agent, historyDir, &terminal);
    return repl.run();
}
