#include <algorithm>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFConsole.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"
#include "CLFCore/CLFTerminal.hpp"
#include "CLFTools/CLFBuiltinTools.hpp"

// 模式循环切换顺序
static const char* kModeCycle[] = {"auto", "analyze", "edit", "manual"};

// 当前安全模式名（供确认回调/快捷键使用）
static std::string g_modeName = "edit";

// 循环切换模式
static void cycleMode(CLF::CLFCore::CLFAgentLoop& agent) {
    auto current = agent.getSecurityModeName();
    constexpr int count = 4;
    for (int i = 0; i < count; ++i) {
        if (std::string(kModeCycle[i]) == current) {
            int next = (i + 1) % count;
            g_modeName = kModeCycle[next];
            agent.setSecurityMode(CLF::CLFCore::CLFSecurityPolicy::modeFromString(g_modeName));
            break;
        }
    }
    CLF::CLFCore::CLFTerminal::drawModeArea(g_modeName);
    CLF::CLFCore::CLFTerminal::scrollPrint(CLF::CLFCore::CLFTerminal::cyan(
        "● 模式切换: " + g_modeName) + "\n");
}

// 确认交互（区域 5：上下键选择 + 回车确认）
static bool confirmDialog(const std::string& prompt) {
    using CLF::CLFCore::CLFConsole;
    using CLF::CLFCore::CLFTerminal;
    using CLF::CLFCore::CLFKey;

    // 滚动区显示确认信息
    size_t pos = prompt.find('\n');
    std::string toolDesc = (pos != std::string::npos) ? prompt.substr(0, pos) : prompt;
    CLFTerminal::scrollPrint("\n● " + CLFTerminal::yellow("⚠ 高风险操作确认") + "\n");
    CLFTerminal::scrollPrint("  ⎿ " + CLFTerminal::cyan(toolDesc) + "\n");
    if (pos != std::string::npos) {
        std::string args = prompt.substr(pos + 1);
        const std::string argPrefix = "参数: ";
        if (args.rfind(argPrefix, 0) == 0) args = args.substr(argPrefix.size());
        CLFTerminal::scrollPrint("  ⎿ 参数: " + CLFTerminal::gray(args) + "\n");
    }

    // 区域 5 选择
    std::vector<std::string> options = {"确认", "取消"};
    int selected = 0;
    CLFTerminal::drawConfirmArea(options, selected);

    while (true) {
        auto key = CLFConsole::readKey();
        if (key.m_key == CLFKey::Up || key.m_key == CLFKey::Left) {
            selected = (selected == 0) ? 1 : 0;
            CLFTerminal::drawConfirmArea(options, selected);
        } else if (key.m_key == CLFKey::Down || key.m_key == CLFKey::Right) {
            selected = (selected == 0) ? 1 : 0;
            CLFTerminal::drawConfirmArea(options, selected);
        } else if (key.m_key == CLFKey::Enter) {
            break;
        } else if (key.m_key == CLFKey::Esc || key.m_key == CLFKey::CtrlC) {
            selected = 1; // 取消
            break;
        }
    }

    CLFTerminal::clearConfirmArea();
    CLFTerminal::scrollPrint(CLFTerminal::green("  ⎿ ✓ ") + (selected == 0 ? "已确认" : "已取消") + "\n");
    return selected == 0;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
    using CLF::CLFCore::CLFTerminal;
    using CLF::CLFCore::CLFConsole;
    using CLF::CLFCore::CLFKey;
    CLFTerminal::enableAnsi();

    // 确定项目根目录
    std::string projectRoot = CLF::CLFCore::CLFConfigLoader::findProjectRoot();

    // 加载配置
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

    // 初始化日志系统
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

    // 创建 Agent + 注册工具
    CLF::CLFCore::CLFAgentLoop agent(config);
    CLF::CLFTools::registerBuiltinTools(agent);

    g_modeName = agent.getSecurityModeName();

    // 确认回调（区域 5 交互）
    agent.setConfirmCallback(confirmDialog);

    // 状态回调（区域 2）
    agent.setStatusCallback([](const std::string& title, const std::string& content) {
        CLFTerminal::drawStatusArea(title, content);
    });

    // 会话目录
    std::string historyDir = projectRoot + "/doc/contextHistory";
    CLF::CLFCore::CLFSessionManager::cleanupOld(historyDir, 30);

    // 启动横幅（滚动区）
    CLFTerminal::initLayout(g_modeName);
    CLFTerminal::scrollPrint(CLFTerminal::bold("● CLFCode") + " — CLI Agent Framework for Code\n");
    CLFTerminal::scrollPrint("  ⎿ 项目根: " + CLFTerminal::cyan(projectRoot) + "\n");
    if (loaded) {
        CLFTerminal::scrollPrint("  ⎿ 配置: " + CLFTerminal::cyan(configPath) + "\n");
    }
    CLFTerminal::scrollPrint("  ⎿ 模型: " + CLFTerminal::cyan(config.m_modelName) + "\n");

    // 崩溃恢复检测
    std::string incompletePath = CLF::CLFCore::CLFSessionManager::findIncomplete(historyDir);
    if (!incompletePath.empty()) {
        CLFTerminal::scrollPrint(CLFTerminal::yellow("● ⚠ 检测到上次会话未正常结束") + "\n");
        CLFTerminal::scrollPrint("  ⎿ 是否恢复？[确认/取消]\n");
        bool restore = confirmDialog("恢复上次会话");
        if (restore) {
            if (agent.restoreSession(incompletePath)) {
                CLFTerminal::scrollPrint(CLFTerminal::green("  ⎿ ✓ 会话已恢复") + "\n");
            } else {
                CLFTerminal::scrollPrint(CLFTerminal::red("  ⎿ ✗ 会话恢复失败") + "\n");
            }
            CLF::CLFCore::CLFSessionManager::promote(incompletePath);
        } else {
            CLF::CLFCore::CLFSessionManager::removeAllIncomplete(historyDir);
            CLFTerminal::scrollPrint("  ⎿ 未完成的会话已丢弃\n");
        }
    }

    // 知识库
    std::string skillDir = projectRoot + "/data/skills";
    int skillCount = CLF::CLFCore::CLFSkillLoader::loadFromDir(skillDir);
    if (skillCount > 0) {
        CLFTerminal::scrollPrint("  ⎿ 知识库: " + CLFTerminal::cyan(std::to_string(skillCount))
                                 + " skills\n");
    }

    // 进入原始模式（键盘交互）
    CLFConsole::enterRawMode();
    atexit(CLFConsole::exitRawMode);

    // ============ REPL 主循环 ============
    std::string input;
    while (true) {
        CLFTerminal::drawInputArea(input);
        CLFTerminal::drawModeArea(g_modeName);

        auto key = CLFConsole::readKey();

        switch (key.m_key) {
            case CLFKey::Char: {
                input += key.m_utf8;
                CLFTerminal::drawInputArea(input);
                break;
            }
            case CLFKey::Backspace: {
                if (!input.empty()) {
                    // 删除最后一个 UTF-8 字符
                    size_t len = 1;
                    while (len < input.size()
                           && (static_cast<unsigned char>(input[input.size() - len]) & 0xC0) == 0x80) {
                        ++len;
                    }
                    input.erase(input.size() - len);
                    CLFTerminal::drawInputArea(input);
                }
                break;
            }
            case CLFKey::Enter: {
                if (input.empty()) break;

                // 提交：滚动区回显 + 前缀
                CLFTerminal::scrollPrint("> " + CLFTerminal::bold(input) + "\n");
                CLFTerminal::scrollPrint("● " + CLFTerminal::cyan("CLFCode") + ": ");

                if (input == "/exit") {
                    agent.saveSession(historyDir, false);
                    CLF::CLFCore::CLFSessionManager::removeAllIncomplete(historyDir);
                    CLFTerminal::scrollPrint("\n");
                    CLFTerminal::restoreScrollRegion();
                    std::cout << "● 会话已保存。再见 — CLFCode" << std::endl;
                    return 0;
                }
                if (input == "/help") {
                    CLFTerminal::scrollPrint("\n  ⎿ /exit   退出 | /clear 新会话 | /skill 知识库\n");
                    CLFTerminal::scrollPrint("  ⎿ /mode   安全模式 | /history 会话 | /resume <n> 恢复\n");
                    CLFTerminal::scrollPrint("  ⎿ /model  模型 | /config 配置\n");
                    CLFTerminal::scrollPrint("  ⎿ Ctrl+N 切换模式 | Ctrl+O 折叠/展开 | Esc 清空输入\n");
                    input.clear();
                    CLFTerminal::drawStatusArea("", "");
                    break;
                }
                if (input == "/clear") {
                    agent.saveSession(historyDir, false);
                    CLF::CLFCore::CLFSessionManager::removeAllIncomplete(historyDir);
                    agent.clearContext();
                    CLFTerminal::scrollPrint(CLFTerminal::green("✓ 会话已保存，新会话开始") + "\n");
                    input.clear();
                    CLFTerminal::drawStatusArea("", "");
                    break;
                }
                if (input.rfind("/mode", 0) == 0) {
                    std::string arg = input.size() > 6 ? input.substr(6) : "";
                    while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);
                    if (!arg.empty()) {
                        g_modeName = arg;
                        agent.setSecurityMode(
                            CLF::CLFCore::CLFSecurityPolicy::modeFromString(arg));
                        CLFTerminal::drawModeArea(g_modeName);
                    }
                    CLFTerminal::scrollPrint(CLFTerminal::cyan("● 模式: " + g_modeName) + "\n");
                    input.clear();
                    break;
                }
                if (input.rfind("/skill", 0) == 0) {
                    std::string arg = input.size() > 7 ? input.substr(7) : "";
                    while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);
                    if (arg.empty() || arg == "list") {
                        auto names = CLF::CLFCore::CLFSkillLoader::listNames();
                        auto loadedSkills = agent.getLoadedSkills();
                        CLFTerminal::scrollPrint("\n● 知识库\n");
                        for (const auto& n : names) {
                            bool isLoaded = std::find(loadedSkills.begin(), loadedSkills.end(), n)
                                         != loadedSkills.end();
                            CLFTerminal::scrollPrint("  ⎿ " + n
                                + (isLoaded ? CLFTerminal::green("  [已加载]")
                                            : CLFTerminal::gray("  [未加载]")) + "\n");
                        }
                    } else {
                        std::string content = CLF::CLFCore::CLFSkillLoader::getContent(arg);
                        if (content.empty()) {
                            CLFTerminal::scrollPrint(CLFTerminal::red("✗ 未找到: " + arg) + "\n");
                        } else {
                            agent.injectSkillToContext(arg, content);
                            CLFTerminal::scrollPrint(CLFTerminal::green("✓ 已加载: " + arg) + "\n");
                        }
                    }
                    input.clear();
                    CLFTerminal::drawStatusArea("", "");
                    break;
                }
                if (input == "/history") {
                    auto sessions = CLF::CLFCore::CLFSessionManager::list(historyDir, 10);
                    if (sessions.empty()) {
                        CLFTerminal::scrollPrint("  ⎿ 暂无已保存的会话\n");
                    } else {
                        CLFTerminal::scrollPrint("\n● 会话列表\n");
                        for (const auto& s : sessions) {
                            CLFTerminal::scrollPrint("  ⎿ " + CLFTerminal::cyan(s.m_savedAt)
                                                     + "  " + s.m_title + "\n");
                        }
                    }
                    input.clear();
                    CLFTerminal::drawStatusArea("", "");
                    break;
                }
                if (input.rfind("/resume", 0) == 0) {
                    auto sessions = CLF::CLFCore::CLFSessionManager::list(historyDir, 10);
                    if (sessions.empty()) {
                        CLFTerminal::scrollPrint("  ⎿ 暂无已保存的会话\n");
                    } else {
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
                            if (agent.restoreSession(sessions[idx - 1].m_path)) {
                                CLFTerminal::scrollPrint(
                                    CLFTerminal::green("✓ 会话已恢复: ")
                                    + sessions[idx - 1].m_title + "\n");
                            } else {
                                CLFTerminal::scrollPrint(CLFTerminal::red("✗ 恢复失败") + "\n");
                            }
                        } else {
                            CLFTerminal::scrollPrint(CLFTerminal::red("✗ 无效序号: " + arg) + "\n");
                        }
                    }
                    input.clear();
                    CLFTerminal::drawStatusArea("", "");
                    break;
                }
                if (input == "/model") {
                    const auto& cfg = agent.getConfig();
                    CLFTerminal::scrollPrint("\n● 当前模型\n");
                    CLFTerminal::scrollPrint("  ⎿ 主模型: " + CLFTerminal::cyan(cfg.m_modelName) + "\n");
                    CLFTerminal::scrollPrint("  ⎿ 副模型: " + CLFTerminal::cyan(cfg.m_subModel) + "\n");
                    input.clear();
                    CLFTerminal::drawStatusArea("", "");
                    break;
                }
                if (input == "/config") {
                    const auto& cfg = agent.getConfig();
                    CLFTerminal::scrollPrint("\n● 配置信息\n");
                    CLFTerminal::scrollPrint("  ⎿ 连接: " + CLFTerminal::cyan(cfg.m_apiBaseUrl) + "\n");
                    CLFTerminal::scrollPrint("  ⎿ 模型: " + CLFTerminal::cyan(cfg.m_modelName)
                                             + " (副: " + cfg.m_subModel + ")\n");
                    CLFTerminal::scrollPrint("  ⎿ 参数: temperature=" + std::to_string(cfg.m_temperature)
                                             + " top_p=" + std::to_string(cfg.m_topP)
                                             + " max_tokens=" + std::to_string(cfg.m_maxTokens) + "\n");
                    CLFTerminal::scrollPrint("  ⎿ 流式: " + std::string(cfg.m_stream ? "开" : "关")
                                             + " | 安全: " + g_modeName + "\n");
                    CLFTerminal::scrollPrint("  ⎿ 上下文: " + std::to_string(cfg.m_maxContextWindow)
                                             + " tokens\n");
                    input.clear();
                    CLFTerminal::drawStatusArea("", "");
                    break;
                }

                // 普通对话
                try {
                    std::string response = agent.runTurn(input);
                    if (!response.empty()) {
                        CLFTerminal::scrollPrint(response + "\n");
                    }
                } catch (const std::exception& e) {
                    CLF::CLFCore::CLFLogger::instance().error(std::string("Fatal: ") + e.what());
                    CLFTerminal::scrollPrint(CLFTerminal::red("✗ 异常: ") + e.what() + "\n");
                    agent.clearContext();
                }

                // 自动存盘（incomplete）
                agent.saveSession(historyDir, true);

                input.clear();
                CLFTerminal::drawStatusArea("", "");
                CLFTerminal::scrollPrint("\n");
                break;
            }
            case CLFKey::CtrlO: {
                CLFTerminal::setScrollCollapsed(!CLFTerminal::isScrollCollapsed());
                break;
            }
            case CLFKey::CtrlN: {
                cycleMode(agent);
                break;
            }
            case CLFKey::Esc: {
                input.clear();
                CLFTerminal::drawInputArea(input);
                break;
            }
            case CLFKey::CtrlC: {
                if (!input.empty()) {
                    input.clear();
                    CLFTerminal::drawInputArea(input);
                } else {
                    // 空输入时 Ctrl+C 退出
                    agent.saveSession(historyDir, false);
                    CLF::CLFCore::CLFSessionManager::removeAllIncomplete(historyDir);
                    CLFTerminal::restoreScrollRegion();
                    std::cout << "● 再见 — CLFCode" << std::endl;
                    return 0;
                }
                break;
            }
            default:
                break; // Up/Down/Left/Right 在输入区暂不处理
        }
    }

    return 0;
}
