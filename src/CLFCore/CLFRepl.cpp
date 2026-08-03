// CLFRepl.cpp — REPL 交互循环实现

#include "CLFCore/CLFRepl.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>

#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFConsole.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"
#include "CLFCore/CLFTerminal.hpp"

namespace CLF::CLFCore {

namespace {

// 模式循环切换顺序
const char* kModeCycle[] = {"auto", "analyze", "edit", "manual"};
constexpr int kModeCount = 4;

} // anonymous namespace

CLFRepl::CLFRepl(CLFAgentLoop& agent, const std::string& historyDir)
    : m_agent(agent)
    , m_historyDir(historyDir)
    , m_modeName(agent.getSecurityModeName()) {
    // 工具高风险确认 → 区域 5 交互
    m_agent.setConfirmCallback(
        [this](const std::string& prompt) { return confirmDialog(prompt); });

    // 工具执行状态 → 区域 2 状态区（标题带时间）
    m_agent.setStatusCallback([](const std::string& title, const std::string& content) {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%H:%M", &tm);
        CLFTerminal::drawStatusArea("[" + std::string(buf) + " " + title + "]", content);
    });
}

int CLFRepl::run() {
    try {
    // 启动清理：上次崩溃可能残留 clf_cmd_*.txt 临时文件
    try {
        for (const auto& entry : std::filesystem::directory_iterator(".")) {
            std::string name = entry.path().filename().string();
            if (name.find("clf_cmd_stdout_") == 0 || name.find("clf_cmd_stderr_") == 0) {
                std::filesystem::remove(entry.path());
            }
        }
    } catch (...) {}

    // 布局初始化 + 横幅
    CLFTerminal::initLayout(m_modeName);
    printBanner();

    // 崩溃恢复检测
    checkIncompleteSession();

    // 进入原始模式（键盘交互）
    CLFConsole::enterRawMode();
    atexit(CLFConsole::exitRawMode);

    // 按键主循环
    int lastHeight = -1;
    while (true) {
        // 窗口缩放检测：高度变化 → 清屏重绘（输入区吸底）
        int currentHeight = CLFTerminal::getTerminalHeight();
        if (lastHeight > 0 && currentHeight != lastHeight && currentHeight >= 10) {
            CLFTerminal::redrawAll();
        }
        lastHeight = currentHeight;

        CLFTerminal::drawInputArea(m_input);
        CLFTerminal::drawModeArea(m_modeName);

        auto key = CLFConsole::readKey();

        switch (key.m_key) {
            case CLFKey::Char:
                m_input += key.m_utf8;
                CLFTerminal::drawInputArea(m_input);
                break;

            case CLFKey::Backspace:
                if (!m_input.empty()) {
                    // 删除最后一个 UTF-8 字符
                    size_t len = 1;
                    while (len < m_input.size()
                           && (static_cast<unsigned char>(m_input[m_input.size() - len]) & 0xC0) == 0x80) {
                        ++len;
                    }
                    m_input.erase(m_input.size() - len);
                    CLFTerminal::drawInputArea(m_input);
                }
                break;

            case CLFKey::Enter:
                if (!m_input.empty()) {
                    std::string submitted = m_input;
                    m_input.clear();
                    submit(submitted);
                }
                break;

            case CLFKey::ShiftTab:
                cycleMode();
                break;

            case CLFKey::Esc:
                m_input.clear();
                CLFTerminal::drawInputArea(m_input);
                break;

            case CLFKey::CtrlC:
                if (!m_input.empty()) {
                    m_input.clear();
                    CLFTerminal::drawInputArea(m_input);
                } else {
                    // 空输入时 Ctrl+C 退出
                    saveSession(false);
                    CLFTerminal::restoreScrollRegion();
                    std::cout << "● 再见 — CLFCode" << std::endl;
                    return 0;
                }
                break;

            case CLFKey::CtrlO:
                // [折叠功能暂缓] Ctrl+O 展开/折叠待后续恢复
                break;

            default:
                break; // Up/Down/Left/Right 在输入区暂不处理
        }
    }
    } catch (...) {
        CLFConsole::exitRawMode();
        CLFTerminal::restoreScrollRegion();
        std::cerr << "[Fatal] Unexpected error — CLFCode terminated." << std::endl;
        return 1;
    }
}

// ============================================================================
// 私有方法
// ============================================================================

void CLFRepl::printBanner() {

    const auto& config = m_agent.getConfig();
    std::string cwd = CLFConfigLoader::getWorkingDir();

    CLFTerminal::scrollPrint(CLFTerminal::bold("● CLFCode") + " — CLI Agent Framework for Code\n");
    CLFTerminal::scrollPrint("  ⎿ " + CLFTerminal::gray(CLFTerminal::diagnosticInfo()) + "\n");
    CLFTerminal::scrollPrint("  ⎿ 工作目录: " + CLFTerminal::cyan(cwd) + "\n");
    CLFTerminal::scrollPrint("  ⎿ 配置: " + CLFTerminal::cyan(m_agent.getConfig().m_apiBaseUrl) + "\n");
    CLFTerminal::scrollPrint("  ⎿ 模型: " + CLFTerminal::cyan(config.m_modelName) + "\n");

    // 知识库（加载路径基于 CLFCode 安装目录，非工作目录）
    int skillCount = CLFSkillLoader::loadFromDir(
        CLFConfigLoader::resolvePath("data/skills"));
    if (skillCount > 0) {
        CLFTerminal::scrollPrint("  ⎿ 知识库: " + CLFTerminal::cyan(std::to_string(skillCount))
                                 + " skills\n");
    }
}

void CLFRepl::checkIncompleteSession() {

    std::string incompletePath = CLFSessionManager::findIncomplete(m_historyDir);
    if (incompletePath.empty()) return;

    CLFTerminal::scrollPrint(CLFTerminal::yellow("● ⚠ 检测到上次会话未正常结束") + "\n");
    CLFTerminal::scrollPrint("  ⎿ 是否恢复？[确认/取消]\n");

    if (confirmDialog("恢复上次会话")) {
        if (m_agent.restoreSession(incompletePath)) {
            CLFTerminal::scrollPrint(CLFTerminal::green("  ⎿ ✓ 会话已恢复") + "\n");
        } else {
            CLFTerminal::scrollPrint(CLFTerminal::red("  ⎿ ✗ 会话恢复失败") + "\n");
        }
        CLFSessionManager::promote(incompletePath);
    } else {
        CLFSessionManager::removeAllIncomplete(m_historyDir);
        CLFTerminal::scrollPrint("  ⎿ 未完成的会话已丢弃\n");
    }
}

void CLFRepl::submit(const std::string& input) {
    // 清除输入区，光标回内容区输出位置
    CLFTerminal::toContentArea();

    // 回显输入
    CLFTerminal::scrollPrint("> " + CLFTerminal::bold(input) + "\n");

    if (handleCommand(input)) {
        CLFTerminal::drawStatusArea("", "");
        return;
    }

    // 回复前缀（仅普通对话，命令不需要）
    CLFTerminal::scrollPrint("● " + CLFTerminal::cyan("CLFCode") + ": ");

    // 普通对话
    try {
        auto t1 = std::chrono::steady_clock::now();
        std::string response = m_agent.runTurn(input);
        auto t2 = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(t2 - t1).count();

        if (elapsed > 0) {
            auto stats = m_agent.getLastToolStats();
            CLFTerminal::thoughtMark(static_cast<int>(elapsed),
                                     stats.searchCount, stats.readCount);
        }
        // 中断已在 CLFAgentLoop 中输出 "⏹ 已中断"，不重复打印
        if (!response.empty() && response != "[Interrupted]") {
            CLFTerminal::scrollPrint(response + "\n");
        }
    } catch (const std::exception& e) {
        CLFLogger::instance().error(std::string("Fatal: ") + e.what());
        CLFTerminal::scrollPrint(CLFTerminal::red("✗ 异常: ") + e.what() + "\n");
        m_agent.clearContext();
    }

    // 自动存盘（仅工具调用时标记 incomplete，纯对话直接完成）
    auto stats = m_agent.getLastToolStats();
    saveSession(stats.totalCalls > 0);

    CLFTerminal::drawStatusArea("", "");
    CLFTerminal::scrollPrint("\n");
}

bool CLFRepl::handleCommand(const std::string& input) {

    if (input == "/exit") {
        saveSession(false);
        CLFTerminal::restoreScrollRegion();
        std::cout << "● 会话已保存。再见 — CLFCode" << std::endl;
        std::exit(0);
    }
    if (input == "/help") {
        CLFTerminal::scrollPrint("\n  ⎿ /exit   退出 | /clear 新会话 | /skill 知识库\n");
        CLFTerminal::scrollPrint("  ⎿ /mode   安全模式 | /history 会话 | /resume <n> 恢复\n");
        CLFTerminal::scrollPrint("  ⎿ /model  模型 | /config 配置 | /context 用量\n");
        CLFTerminal::scrollPrint("  ⎿ Ctrl+N 切换模式 | Esc 清空输入\n");
        return true;
    }
    if (input == "/clear") {
        saveSession(false);
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

        // 用量条（20 格）
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
    return false; // 非命令
}

bool CLFRepl::confirmDialog(const std::string& prompt) {

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

    // 区域 5 选择（上下键 + 回车）
    std::vector<std::string> options = {"确认", "取消"};
    int selected = 0;
    CLFTerminal::drawConfirmArea(options, selected);

    while (true) {
        auto key = CLFConsole::readKey();
        if (key.m_key == CLFKey::Up || key.m_key == CLFKey::Down
            || key.m_key == CLFKey::Left || key.m_key == CLFKey::Right) {
            selected = (selected == 0) ? 1 : 0;
            CLFTerminal::drawConfirmArea(options, selected);
        } else if (key.m_key == CLFKey::ShiftTab) {
            cycleMode(); // 确认过程中支持切换模式
            CLFTerminal::drawConfirmArea(options, selected);
        } else if (key.m_key == CLFKey::Enter) {
            break;
        } else if (key.m_key == CLFKey::Esc || key.m_key == CLFKey::CtrlC) {
            selected = 1; // 取消
            break;
        }
    }

    CLFTerminal::clearConfirmArea();
    CLFTerminal::scrollPrint(CLFTerminal::green("  ⎿ ✓ ")
        + (selected == 0 ? "已确认" : "已取消") + "\n");
    return selected == 0;
}

void CLFRepl::cycleMode() {
    auto current = m_agent.getSecurityModeName();
    for (int i = 0; i < kModeCount; ++i) {
        if (std::string(kModeCycle[i]) == current) {
            m_modeName = kModeCycle[(i + 1) % kModeCount];
            m_agent.setSecurityMode(CLFSecurityPolicy::modeFromString(m_modeName));
            break;
        }
    }
    // 原地更新状态栏模式行（不输出 scrollPrint 避免内容区堆积）
    CLFTerminal::drawModeArea(m_modeName);
}

void CLFRepl::saveSession(bool incomplete) {
    if (incomplete) {
        m_agent.saveSession(m_historyDir, true);
    } else {
        m_agent.saveSession(m_historyDir, false);
        CLFSessionManager::removeAllIncomplete(m_historyDir);
    }
}

} // namespace CLF::CLFCore
