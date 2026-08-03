// CLFRepl.cpp — REPL 交互循环实现（命令分发 → CLFCommandDispatcher）

#include "CLFCore/CLFRepl.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFCommandDispatcher.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFConsole.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"
#include "CLFCore/CLFTerminal.hpp"

namespace CLF::CLFCore {

namespace {

const char* kModeCycle[] = {"auto", "analyze", "edit", "manual"};
constexpr int kModeCount = 4;

} // anonymous namespace

CLFRepl::CLFRepl(CLFAgentLoop& agent, const std::string& historyDir)
    : m_agent(agent)
    , m_historyDir(historyDir)
    , m_dispatcher(std::make_unique<CLFCommandDispatcher>(agent, historyDir)) {
    m_agent.setConfirmCallback(
        [this](const std::string& prompt) { return confirmDialog(prompt); });
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

CLFRepl::~CLFRepl() = default;

int CLFRepl::run() {
    try {
    // 启动清理
    try {
        for (const auto& entry : std::filesystem::directory_iterator(".")) {
            std::string name = entry.path().filename().string();
            if (name.find("clf_cmd_stdout_") == 0 || name.find("clf_cmd_stderr_") == 0) {
                std::filesystem::remove(entry.path());
            }
        }
    } catch (...) {}

    CLFTerminal::initLayout(m_dispatcher->modeName());
    printBanner();
    checkIncompleteSession();

    CLFConsole::enterRawMode();
    atexit(CLFConsole::exitRawMode);

    int lastHeight = -1;
    while (true) {
        int currentHeight = CLFTerminal::getTerminalHeight();
        if (lastHeight > 0 && currentHeight != lastHeight && currentHeight >= 10) {
            CLFTerminal::redrawAll();
        }
        lastHeight = currentHeight;

        CLFTerminal::drawInputArea(m_input, m_cursorPos);
        CLFTerminal::drawModeArea(m_dispatcher->modeName());

        auto key = CLFConsole::readKey();

        switch (key.m_key) {
            case CLFKey::Char:
                m_input.insert(m_cursorPos, key.m_utf8);
                m_cursorPos += static_cast<int>(key.m_utf8.size());
                CLFTerminal::drawInputArea(m_input, m_cursorPos);
                break;

            case CLFKey::Backspace:
                if (m_cursorPos > 0 && !m_input.empty()) {
                    // 删除光标前一个 UTF-8 字符
                    int delPos = m_cursorPos - 1;
                    size_t len = 1;
                    while (delPos - static_cast<int>(len) + 1 >= 0
                           && (static_cast<unsigned char>(m_input[delPos - static_cast<int>(len) + 1]) & 0xC0) == 0x80)
                        ++len;
                    m_input.erase(m_cursorPos - static_cast<int>(len), len);
                    m_cursorPos -= static_cast<int>(len);
                    CLFTerminal::drawInputArea(m_input, m_cursorPos);
                }
                break;

            case CLFKey::Left:
                if (m_cursorPos > 0) {
                    --m_cursorPos;
                    // 跳过 UTF-8 续字节
                    while (m_cursorPos > 0
                           && (static_cast<unsigned char>(m_input[m_cursorPos]) & 0xC0) == 0x80)
                        --m_cursorPos;
                    CLFTerminal::drawInputArea(m_input, m_cursorPos);
                }
                break;

            case CLFKey::Right:
                if (m_cursorPos < static_cast<int>(m_input.size())) {
                    ++m_cursorPos;
                    while (m_cursorPos < static_cast<int>(m_input.size())
                           && (static_cast<unsigned char>(m_input[m_cursorPos]) & 0xC0) == 0x80)
                        ++m_cursorPos;
                    CLFTerminal::drawInputArea(m_input, m_cursorPos);
                }
                break;

            case CLFKey::Enter:
                if (!m_input.empty()) {
                    std::string submitted = m_input;
                    m_input.clear();
                    m_cursorPos = 0;
                    submit(submitted);
                }
                break;

            case CLFKey::ShiftEnter:
                m_input.insert(m_cursorPos, "\n");
                ++m_cursorPos;
                CLFTerminal::drawInputArea(m_input, m_cursorPos);
                break;

            case CLFKey::ShiftTab:
                cycleMode();
                break;

            case CLFKey::Esc:
                m_input.clear();
                m_cursorPos = 0;
                CLFTerminal::drawInputArea(m_input);
                break;

            case CLFKey::CtrlC:
                if (!m_input.empty()) {
                    m_input.clear();
                    m_cursorPos = 0;
                    CLFTerminal::drawInputArea(m_input);
                } else {
                    saveSession(false);
                    CLFTerminal::restoreScrollRegion();
                    std::cout << "● 再见 — CLFCode" << std::endl;
                    return 0;
                }
                break;

            case CLFKey::CtrlO:
                break;

            default:
                break;
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
    CLFTerminal::toContentArea();
    CLFTerminal::scrollPrint("> " + CLFTerminal::bold(input) + "\n");

    // 命令分发
    if (m_dispatcher->handle(input)) {
        CLFTerminal::drawStatusArea("", "");
        return;
    }

    CLFTerminal::scrollPrint("● " + CLFTerminal::cyan("CLFCode") + ": ");

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
        if (!response.empty() && response != "[Interrupted]") {
            CLFTerminal::scrollPrint(response + "\n");
        }
    } catch (const std::exception& e) {
        CLFLogger::instance().error(std::string("Fatal: ") + e.what());
        CLFTerminal::scrollPrint(CLFTerminal::red("✗ 异常: ") + e.what() + "\n");
        m_agent.clearContext();
    }

    auto stats = m_agent.getLastToolStats();
    saveSession(stats.totalCalls > 0);

    CLFTerminal::drawStatusArea("", "");
    CLFTerminal::scrollPrint("\n");
}

bool CLFRepl::confirmDialog(const std::string& prompt) {
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
            cycleMode();
            CLFTerminal::drawConfirmArea(options, selected);
        } else if (key.m_key == CLFKey::Enter) {
            break;
        } else if (key.m_key == CLFKey::Esc || key.m_key == CLFKey::CtrlC) {
            selected = 1;
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
            std::string newMode = kModeCycle[(i + 1) % kModeCount];
            m_dispatcher->setModeName(newMode);
            m_agent.setSecurityMode(CLFSecurityPolicy::modeFromString(newMode));
            break;
        }
    }
    CLFTerminal::drawModeArea(m_dispatcher->modeName());
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
