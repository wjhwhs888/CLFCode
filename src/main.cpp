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
#include "CLFCore/CLFTerminal.hpp"
#include "CLFTools/CLFBuiltinTools.hpp"

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
    using CLF::CLFCore::CLFTerminal;
    CLFTerminal::enableAnsi();

    // 确定项目根目录（从 exe 向上找 CMakeLists.txt）
    std::string projectRoot = CLF::CLFCore::CLFConfigLoader::findProjectRoot();
    CLFTerminal::item(CLFTerminal::bold("CLFCode") + " — CLI Agent Framework for Code");
    CLFTerminal::sub("项目根: " + CLFTerminal::cyan(projectRoot));

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
        CLFTerminal::sub("配置: " + CLFTerminal::cyan(configPath));
    } else {
        CLF::CLFCore::CLFLogger::instance().warn("No config file found, using defaults.");
        CLFTerminal::sub(CLFTerminal::yellow("配置: 未找到，使用默认值"));
    }

    if (config.m_apiKey.empty()) {
        CLF::CLFCore::CLFLogger::instance().error(
            "API Key is required. Set CLF_API_KEY or create config/agent_settings.local.json");
        return 1;
    }

    CLFTerminal::sub("模型: " + CLFTerminal::cyan(config.m_modelName));
    CLFTerminal::sub(CLFTerminal::gray("输入 /help 查看命令，/exit 退出"));

    // 创建 Agent 并注册全部内置工具
    CLF::CLFCore::CLFAgentLoop agent(config);
    CLF::CLFTools::registerBuiltinTools(agent);

    // 注入高风险工具确认回调（终端 y/n 交互，在输入框位置进行）
    agent.setConfirmCallback([&](const std::string& prompt) {
        std::cout << std::endl;
        CLFTerminal::info("高风险操作确认");
        // 拆分提示文本（工具描述 + 参数），按 \n 切分避免 UTF-8 字节错位
        size_t pos = prompt.find('\n');
        if (pos != std::string::npos) {
            CLFTerminal::sub(CLFTerminal::cyan(prompt.substr(0, pos))); // 工具描述
            std::string args = prompt.substr(pos + 1);
            // 去掉 "参数: " 前缀（下方已加标签）
            const std::string argPrefix = "参数: ";
            if (args.rfind(argPrefix, 0) == 0) {
                args = args.substr(argPrefix.size());
            }
            CLFTerminal::sub("参数: " + CLFTerminal::gray(args));
        } else {
            CLFTerminal::sub(prompt);
        }
        return CLFTerminal::confirmInput("允许执行该操作？(y/n): ", agent.getSecurityModeName());
    });

    CLFTerminal::sub("安全模式: " + CLFTerminal::cyan(std::string(agent.getSecurityModeName())));

    // 会话目录（崩溃恢复 + 历史）
    std::string historyDir = projectRoot + "/doc/contextHistory";
    CLF::CLFCore::CLFSessionManager::cleanupOld(historyDir, 30); // 30 天自动清理

    // 崩溃恢复检测：存在未完成会话 → 询问是否恢复
    std::string incompletePath = CLF::CLFCore::CLFSessionManager::findIncomplete(historyDir);
    if (!incompletePath.empty()) {
        std::cout << "[提示] 检测到上次会话未正常结束。" << std::endl;
        std::cout << "是否恢复该会话？" << CLFTerminal::green("(y/n)") << ": " << std::flush;
        std::string answer;
        std::getline(std::cin, answer);
        if (answer == "y" || answer == "Y" || answer == "yes") {
            if (agent.restoreSession(incompletePath)) {
                CLFTerminal::ok("会话已恢复");
            } else {
                CLFTerminal::fail("会话恢复失败（文件损坏？）");
            }
            CLF::CLFCore::CLFSessionManager::promote(incompletePath); // 转正
        } else {
            CLF::CLFCore::CLFSessionManager::remove(incompletePath);
            CLFTerminal::sub(CLFTerminal::gray("未完成的会话已丢弃"));
        }
    }

    // 加载知识库（Skills）
    std::string skillDir = projectRoot + "/data/skills";
    int skillCount = CLF::CLFCore::CLFSkillLoader::loadFromDir(skillDir);
    if (skillCount > 0) {
        CLFTerminal::item("知识库: " + CLFTerminal::cyan(std::to_string(skillCount)) + " skills");
        auto names = CLF::CLFCore::CLFSkillLoader::listNames();
        for (const auto& n : names) {
            std::string extra = (n == "constitution") ? "  [常驻注入]" : "";
            CLFTerminal::sub(n + CLFTerminal::gray(" (L1 常驻)" + extra));
        }
    }
    std::cout << std::endl;

    // REPL（分屏：内容滚动区 + 底部固定输入框）
    std::string input;
    while (true) {
        // 初始化分屏（底线显示当前安全模式），光标停在输入位置
        CLFTerminal::setupSplitScreen(1, agent.getSecurityModeName());

        if (!std::getline(std::cin, input)) {
            CLFTerminal::restoreScrollRegion();
            break;
        }

        if (input.empty()) continue;

        // 光标移到内容区（滚动区最后一行），回复内容全部显示在分割线之上
        CLFTerminal::toContentArea();

        // 回显用户输入（内容区）
        std::cout << "> " << CLFTerminal::bold(input) << std::endl;

        if (input == "/exit") {
            // 保存正式会话 + 清理全部 incomplete
            agent.saveSession(historyDir, false);
            CLF::CLFCore::CLFSessionManager::removeAllIncomplete(historyDir);
            CLFTerminal::restoreScrollRegion();
            CLFTerminal::ok("会话已保存");
            CLFTerminal::item(CLFTerminal::bold("再见") + CLFTerminal::gray(" — CLFCode"));
            break;
        }
        if (input == "/help") {
            CLFTerminal::item("命令列表");
            CLFTerminal::sub(CLFTerminal::bold("/exit")    + CLFTerminal::gray("    - 退出（保存会话）"));
            CLFTerminal::sub(CLFTerminal::bold("/help")    + CLFTerminal::gray("    - 显示帮助"));
            CLFTerminal::sub(CLFTerminal::bold("/clear")   + CLFTerminal::gray("   - 保存会话并开始新会话"));
            CLFTerminal::sub(CLFTerminal::bold("/skill")   + CLFTerminal::gray("   - 查看/加载知识库"));
            CLFTerminal::sub(CLFTerminal::bold("/mode")    + CLFTerminal::gray("    - 切换安全模式"));
            CLFTerminal::sub(CLFTerminal::bold("/history") + CLFTerminal::gray(" - 会话列表"));
            CLFTerminal::sub(CLFTerminal::bold("/resume")  + CLFTerminal::gray("  - 恢复会话 (/resume <n>)"));
            CLFTerminal::sub(CLFTerminal::bold("/model")   + CLFTerminal::gray("   - 查看模型"));
            CLFTerminal::sub(CLFTerminal::bold("/config")  + CLFTerminal::gray("   - 查看配置"));
            continue;
        }

        if (input == "/history") {
            auto sessions = CLF::CLFCore::CLFSessionManager::list(historyDir, 10);
            if (sessions.empty()) {
                CLFTerminal::sub(CLFTerminal::gray("暂无已保存的会话"));
            } else {
                CLFTerminal::item("会话列表");
                for (const auto& s : sessions) {
                    CLFTerminal::sub(CLFTerminal::cyan(s.m_savedAt) + "  " + s.m_title);
                }
            }
            continue;
        }

        if (input == "/model") {
            const auto& cfg = agent.getConfig();
            CLFTerminal::item("当前模型");
            CLFTerminal::sub("主模型: " + CLFTerminal::cyan(cfg.m_modelName));
            CLFTerminal::sub("副模型: " + CLFTerminal::cyan(cfg.m_subModel));
            CLFTerminal::sub(CLFTerminal::gray("可用模型（修改 config/agent_settings.json）:"));
            CLFTerminal::sub2("deepseek-v4-flash  - 主模型（Agent 能力最强）");
            CLFTerminal::sub2("deepseek-v4-pro    - Pro（正式版待发布）");
            continue;
        }

        if (input == "/config") {
            const auto& cfg = agent.getConfig();
            CLFTerminal::item("配置信息");
            CLFTerminal::sub(CLFTerminal::bold("连接") + ":   " + CLFTerminal::cyan(cfg.m_apiBaseUrl));
            CLFTerminal::sub(CLFTerminal::bold("模型") + ":   " + CLFTerminal::cyan(cfg.m_modelName)
                + CLFTerminal::gray(" (副: ") + cfg.m_subModel + CLFTerminal::gray(")"));
            CLFTerminal::sub(CLFTerminal::bold("参数") + ":   temperature="
                + std::to_string(cfg.m_temperature) + " top_p="
                + std::to_string(cfg.m_topP) + " max_tokens="
                + std::to_string(cfg.m_maxTokens));
            CLFTerminal::sub(CLFTerminal::bold("流式") + ":   "
                + (cfg.m_stream ? CLFTerminal::green("开") : CLFTerminal::gray("关")));
            CLFTerminal::sub(CLFTerminal::bold("安全") + ":   " + CLFTerminal::cyan(agent.getSecurityModeName()));
            CLFTerminal::sub(CLFTerminal::bold("上下文") + ": " + std::to_string(cfg.m_maxContextWindow)
                + " tokens" + CLFTerminal::gray(" (压缩: ")
                + (cfg.m_contextCompression ? "开" : "关") + CLFTerminal::gray(")"));
            CLFTerminal::sub(CLFTerminal::bold("语言") + ":   " + cfg.m_interactionLanguage);
            CLFTerminal::sub(CLFTerminal::bold("日志") + ":   " + CLFTerminal::gray(cfg.m_logFile));
            continue;
        }

        if (input.rfind("/resume", 0) == 0) {
            auto sessions = CLF::CLFCore::CLFSessionManager::list(historyDir, 10);
            if (sessions.empty()) {
                CLFTerminal::sub(CLFTerminal::gray("暂无已保存的会话"));
                continue;
            }

            std::string arg = input.size() > 8 ? input.substr(8) : "";
            while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);

            if (arg.empty()) {
                // 无参数：列出带序号的会话
                CLFTerminal::item("会话列表");
                for (size_t i = 0; i < sessions.size(); ++i) {
                    CLFTerminal::sub(CLFTerminal::cyan("[" + std::to_string(i + 1) + "]")
                        + " " + CLFTerminal::gray(sessions[i].m_savedAt)
                        + "  " + sessions[i].m_title);
                }
                CLFTerminal::sub(CLFTerminal::gray("用法: /resume <n> 恢复第 n 条会话"));
            } else {
                // 按序号恢复
                int idx = 0;
                try { idx = std::stoi(arg); } catch (...) {}
                if (idx < 1 || idx > static_cast<int>(sessions.size())) {
                    CLFTerminal::fail("无效的会话序号: " + arg);
                } else {
                    const auto& s = sessions[idx - 1];
                    if (agent.restoreSession(s.m_path)) {
                        CLFTerminal::ok("会话已恢复: " + s.m_title);
                    } else {
                        CLFTerminal::fail("会话恢复失败（文件损坏？）");
                    }
                }
            }
            continue;
        }

        if (input.rfind("/mode", 0) == 0) {
            std::string arg = input.size() > 6 ? input.substr(6) : "";
            while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);

            if (arg.empty()) {
                CLFTerminal::item("当前安全模式: " + CLFTerminal::cyan(agent.getSecurityModeName()));
                CLFTerminal::sub(CLFTerminal::gray("用法: /mode <auto|analyze|edit|manual>"));
            } else {
                auto mode = CLF::CLFCore::CLFSecurityPolicy::modeFromString(arg);
                agent.setSecurityMode(mode);
                CLFTerminal::ok("安全模式: " + CLFTerminal::cyan(agent.getSecurityModeName()));
                if (mode == CLF::CLFCore::CLFSecurityMode::Analyze) {
                    CLFTerminal::sub(CLFTerminal::yellow("⚠ 写操作和命令执行将被阻断"));
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
                CLFTerminal::item("知识库");
                for (const auto& n : names) {
                    bool isLoaded = std::find(loaded.begin(), loaded.end(), n) != loaded.end();
                    CLFTerminal::sub(n + (isLoaded
                        ? CLFTerminal::green("  [已加载]")
                        : CLFTerminal::gray("  [未加载]")));
                }
                CLFTerminal::sub(CLFTerminal::gray("constitution  [常驻]  (L1 编码宪法，始终注入)"));
                CLFTerminal::sub(CLFTerminal::gray("用法: /skill <name>"));
            } else {
                std::string content = CLF::CLFCore::CLFSkillLoader::getContent(arg);
                if (content.empty()) {
                    CLFTerminal::fail("知识库未找到: " + arg);
                    CLFTerminal::sub(CLFTerminal::gray("用法: /skill list 查看可用项"));
                } else {
                    agent.injectSkillToContext(arg, content);
                    CLFTerminal::ok("知识库已加载: " + CLFTerminal::cyan(arg));
                }
            }
            continue;
        }
        if (input == "/clear") {
            // 先保存当前会话为正式会话，再开始新会话
            agent.saveSession(historyDir, false);
            CLF::CLFCore::CLFSessionManager::removeAllIncomplete(historyDir);
            agent.clearContext();
            CLFTerminal::ok("会话已保存，新会话开始");
            continue;
        }

        // 回复前缀（流式内容紧随其后）
        std::cout << "● " << CLFTerminal::cyan("CLFCode") << ": " << std::flush;

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
