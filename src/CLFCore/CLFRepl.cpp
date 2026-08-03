// CLFRepl.cpp — REPL 主循环 (事件驱动)

#include "CLFCore/CLFRepl.hpp"
#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFCommandDispatcher.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFConsole.hpp"
#include "CLFCore/CLFEvent.hpp"
#include "CLFCore/CLFEventQueue.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"
#include "CLFCore/CLFTerminal.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>

namespace CLF::CLFCore {

namespace {
const char* kModeCycle[] = {"auto", "analyze", "edit", "manual"};
constexpr int kModeCount = 4;
}

CLFRepl::CLFRepl(CLFAgentLoop& agent, const std::string& historyDir)
    : m_agent(agent)
    , m_historyDir(historyDir)
    , m_dispatcher(std::make_unique<CLFCommandDispatcher>(agent, historyDir))
    , m_eventQueue(std::make_unique<CLFEventQueue>()) {
    m_agent.setEventQueue(m_eventQueue.get());
    m_agent.setConfirmCallback(
        [this](const std::string& prompt) { return confirmDialog(prompt); });
    m_agent.setStatusCallback([](const std::string& title, const std::string& content) {
        CLFTerminal::drawStatusArea(title, content);
    });
}

CLFRepl::~CLFRepl() = default;

int CLFRepl::run() {
    try {
    // 启动清理
    try { for (auto& e : std::filesystem::directory_iterator(".")) {
        std::string n = e.path().filename().string();
        if (n.find("clf_cmd_stdout_") == 0 || n.find("clf_cmd_stderr_") == 0)
            std::filesystem::remove(e.path());
    }} catch (...) {}

    CLFTerminal::initLayout(m_dispatcher->modeName());
    printBanner();
    checkIncompleteSession();
    CLFConsole::enterRawMode();
    atexit(CLFConsole::exitRawMode);

    // ====== 事件驱动主循环 ======
    while (!m_exit) {
        // —— 键盘输入 ——
        if (!m_confirmActive) {
            auto key = CLFConsole::readKey();
            Event ev;
            switch (key.m_key) {
                case CLFKey::Char:
                    ev.type = EventType::KeyChar; ev.text = key.m_utf8; break;
                case CLFKey::Enter:
                    ev.type = m_input.empty() ? EventType::None : EventType::KeySubmit; break;
                case CLFKey::ShiftEnter:
                    ev.type = EventType::KeyNewLine; break;
                case CLFKey::Backspace:
                    ev.type = EventType::KeyBackspace; break;
                case CLFKey::Left:   ev.type = EventType::KeyMoveLeft; break;
                case CLFKey::Right:  ev.type = EventType::KeyMoveRight; break;
                case CLFKey::Up:     ev.type = EventType::KeyMoveUp; break;
                case CLFKey::Down:   ev.type = EventType::KeyMoveDown; break;
                case CLFKey::Home:   ev.type = EventType::KeyHome; break;
                case CLFKey::End:    ev.type = EventType::KeyEnd; break;
                case CLFKey::Esc:
                    ev.type = EventType::KeyClearInput; break;
                case CLFKey::ShiftTab:
                    ev.type = EventType::KeyCycleMode; break;
                case CLFKey::CtrlC:
                    ev.type = m_input.empty() ? EventType::KeyExit : EventType::KeyClearInput; break;
                default: break;
            }
            if (ev.type != EventType::None) m_eventQueue->push(ev);
        } else {
            auto key = CLFConsole::readKey();
            Event ev;
            if (key.m_key == CLFKey::Enter)
                ev.type = EventType::ConfirmHide;
            else if (key.m_key == CLFKey::Esc || key.m_key == CLFKey::CtrlC)
                { ev.type = EventType::ConfirmHide; ev.i1 = 1; }
            else if (key.m_key == CLFKey::Up || key.m_key == CLFKey::Left
                     || key.m_key == CLFKey::Down || key.m_key == CLFKey::Right)
                { ev.type = EventType::ConfirmShow; ev.i1 = 1 - ev.i1; }
            else if (key.m_key == CLFKey::ShiftTab)
                ev.type = EventType::KeyCycleMode;
            if (ev.type != EventType::None) m_eventQueue->push(ev);
        }

        // —— 缩放检测 ——
        int h = CLFTerminal::getTerminalHeight();
        if (m_lastHeight > 0 && h != m_lastHeight && h >= 10) {
            Event ev; ev.type = EventType::LayoutResize; ev.i1 = CLFTerminal::getTerminalWidth(); ev.i2 = h;
            m_eventQueue->push(ev);
        }
        m_lastHeight = h;

        // —— 消费事件队列 + 应用状态变更 ——
        m_eventQueue->coalesce();
        while (!m_eventQueue->empty()) {
            Event e = m_eventQueue->pop();
            switch (e.type) {
                // ==== 输入操作 ====
                case EventType::KeyChar:
                    m_input.insert(m_cursorPos, e.text);
                    m_cursorPos += (int)e.text.size();
                    break;
                case EventType::KeyBackspace:
                    if (m_cursorPos > 0 && !m_input.empty()) {
                        int dp = m_cursorPos - 1; size_t len = 1;
                        while (dp - (int)len + 1 >= 0
                               && (static_cast<unsigned char>(m_input[dp - (int)len + 1]) & 0xC0) == 0x80) ++len;
                        m_input.erase(m_cursorPos - (int)len, len);
                        m_cursorPos -= (int)len;
                    } break;
                case EventType::KeyMoveLeft:
                    if (m_cursorPos > 0) { --m_cursorPos;
                        while (m_cursorPos > 0 && (static_cast<unsigned char>(m_input[m_cursorPos]) & 0xC0) == 0x80) --m_cursorPos; }
                    break;
                case EventType::KeyMoveRight:
                    if (m_cursorPos < (int)m_input.size()) { ++m_cursorPos;
                        while (m_cursorPos < (int)m_input.size() && (static_cast<unsigned char>(m_input[m_cursorPos]) & 0xC0) == 0x80) ++m_cursorPos; }
                    break;
                case EventType::KeyMoveUp: {
                    int ls = m_cursorPos;
                    while (ls > 0 && m_input[ls-1] != '\n') --ls;
                    int co = m_cursorPos - ls;
                    if (ls > 0) {
                        int pe = ls - 1, ps = pe;
                        while (ps > 0 && m_input[ps-1] != '\n') --ps;
                        m_cursorPos = ps + std::min(co, pe - ps);
                    }} break;
                case EventType::KeyMoveDown: {
                    int ls = m_cursorPos;
                    while (ls > 0 && m_input[ls-1] != '\n') --ls;
                    int co = m_cursorPos - ls;
                    int ns = m_cursorPos;
                    while (ns < (int)m_input.size() && m_input[ns] != '\n') ++ns;
                    if (ns < (int)m_input.size()) { ++ns;
                        int ne = ns;
                        while (ne < (int)m_input.size() && m_input[ne] != '\n') ++ne;
                        m_cursorPos = ns + std::min(co, ne - ns);
                    }} break;
                case EventType::KeyHome: {
                    int p = m_cursorPos;
                    while (p > 0 && m_input[p-1] != '\n') --p;
                    m_cursorPos = p;
                } break;
                case EventType::KeyEnd: {
                    int p = m_cursorPos;
                    while (p < (int)m_input.size() && m_input[p] != '\n') ++p;
                    m_cursorPos = p;
                } break;
                case EventType::KeyNewLine:
                    m_input.insert(m_cursorPos, "\n"); ++m_cursorPos; break;
                case EventType::KeyClearInput:
                    m_input.clear(); m_cursorPos = 0; break;
                case EventType::KeySubmit: {
                    std::string s = m_input; m_input.clear(); m_cursorPos = 0;
                    submit(s);
                } break;

                // ==== 模式切换 ====
                case EventType::KeyCycleMode:
                    cycleMode(); break;

                // ==== 内容渲染 ====
                case EventType::ContentAppend:
                    CLFTerminal::scrollPrint(e.text); break;
                case EventType::ContentNewline:
                    CLFTerminal::scrollPrint("\n"); break;
                case EventType::ContentThought:
                    CLFTerminal::thoughtMark(e.i1, e.i2, e.tree.empty() ? 0 : std::stoi(e.tree[0])); break;

                // ==== 状态渲染 ====
                case EventType::StatusThinking:
                    CLFTerminal::showThinking(e.i1); break;
                case EventType::StatusClear:
                    CLFTerminal::clearStatus(); break;
                case EventType::StatusWorking:
                    CLFTerminal::showWorking(e.text); break;
                case EventType::StatusTaskTree:
                    CLFTerminal::showTaskTree(e.tree); break;

                // ==== 确认 ====
                case EventType::ConfirmShow:
                    m_confirmActive = true;
                    CLFTerminal::showConfirm(e.tree, e.i1); break;
                case EventType::ConfirmHide:
                    m_confirmActive = false;
                    CLFTerminal::hideConfirm(); break;

                // ==== 布局 ====
                case EventType::LayoutResize:
                    CLFTerminal::redrawAll(); break;

                // ==== 退出 ====
                case EventType::KeyExit:
                    saveSession(false);
                    CLFTerminal::restoreScrollRegion();
                    std::cout << "● 再见 — CLFCode" << std::endl;
                    return 0;

                default: break;
            }
        }

        // —— 帧末: 刷新输入区 ——
        if (!m_confirmActive) {
            CLFTerminal::drawInput(m_input, m_cursorPos);
        }
        CLFTerminal::drawMode(m_dispatcher->modeName());
    }
    } catch (...) {
        CLFConsole::exitRawMode();
        CLFTerminal::restoreScrollRegion();
        std::cerr << "[Fatal] Unexpected error — CLFCode terminated." << std::endl;
        return 1;
    }
    return 0;
}

// ============================================================================
void CLFRepl::printBanner() {
    const auto& config = m_agent.getConfig();
    std::string cwd = CLFConfigLoader::getWorkingDir();
    CLFTerminal::scrollPrint(CLFTerminal::bold("● CLFCode") + " — CLI Agent Framework for Code\n");
    CLFTerminal::scrollPrint("  ⎿ " + CLFTerminal::gray(CLFTerminal::diagnosticInfo()) + "\n");
    CLFTerminal::scrollPrint("  ⎿ 工作目录: " + CLFTerminal::cyan(cwd) + "\n");
    CLFTerminal::scrollPrint("  ⎿ 配置: " + CLFTerminal::cyan(config.m_apiBaseUrl) + "\n");
    CLFTerminal::scrollPrint("  ⎿ 模型: " + CLFTerminal::cyan(config.m_modelName) + "\n");
    int sc = CLFSkillLoader::loadFromDir(CLFConfigLoader::resolvePath("data/skills"));
    if (sc > 0) CLFTerminal::scrollPrint("  ⎿ 知识库: " + CLFTerminal::cyan(std::to_string(sc)) + " skills\n");
}

void CLFRepl::checkIncompleteSession() {
    std::string ip = CLFSessionManager::findIncomplete(m_historyDir);
    if (ip.empty()) return;
    CLFTerminal::scrollPrint(CLFTerminal::yellow("● ⚠ 检测到上次会话未正常结束") + "\n");
    CLFTerminal::scrollPrint("  ⎿ 是否恢复？[确认/取消]\n");
    if (confirmDialog("恢复上次会话")) {
        if (m_agent.restoreSession(ip))
            CLFTerminal::scrollPrint(CLFTerminal::green("  ⎿ ✓ 会话已恢复") + "\n");
        else
            CLFTerminal::scrollPrint(CLFTerminal::red("  ⎿ ✗ 会话恢复失败") + "\n");
        CLFSessionManager::promote(ip);
    } else {
        CLFSessionManager::removeAllIncomplete(m_historyDir);
        CLFTerminal::scrollPrint("  ⎿ 未完成的会话已丢弃\n");
    }
}

void CLFRepl::submit(const std::string& input) {
    CLFTerminal::toContentArea();
    CLFTerminal::scrollPrint("> " + CLFTerminal::bold(input) + "\n");
    if (m_dispatcher->handle(input)) { CLFTerminal::clearStatus(); return; }
    CLFTerminal::scrollPrint("● " + CLFTerminal::cyan("CLFCode") + ": ");
    try {
        auto t1 = std::chrono::steady_clock::now();
        std::string response = m_agent.runTurn(input);
        auto t2 = std::chrono::steady_clock::now();
        auto el = std::chrono::duration_cast<std::chrono::seconds>(t2 - t1).count();
        if (el > 0) {
            auto st = m_agent.getLastToolStats();
            CLFTerminal::thoughtMark((int)el, st.searchCount, st.readCount);
        }
        if (!response.empty() && response != "[Interrupted]")
            CLFTerminal::scrollPrint(response + "\n");
    } catch (const std::exception& e) {
        CLFLogger::instance().error(std::string("Fatal: ") + e.what());
        CLFTerminal::scrollPrint(CLFTerminal::red("✗ 异常: ") + e.what() + "\n");
        m_agent.clearContext();
    }
    auto st = m_agent.getLastToolStats();
    saveSession(st.totalCalls > 0);
    CLFTerminal::clearStatus();
    CLFTerminal::scrollPrint("\n");
}

bool CLFRepl::confirmDialog(const std::string& prompt) {
    CLFTerminal::scrollPrint("\n● " + CLFTerminal::yellow("⚠ 高风险操作确认") + "\n");
    size_t p = prompt.find('\n');
    CLFTerminal::scrollPrint("  ⎿ " + CLFTerminal::cyan(p != std::string::npos ? prompt.substr(0, p) : prompt) + "\n");
    if (p != std::string::npos) {
        std::string args = prompt.substr(p + 1);
        const std::string ap = "参数: ";
        if (args.rfind(ap, 0) == 0) args = args.substr(ap.size());
        CLFTerminal::scrollPrint("  ⎿ 参数: " + CLFTerminal::gray(args) + "\n");
    }

    // 使用事件队列处理确认交互
    m_confirmActive = true;
    Event ev; ev.type = EventType::ConfirmShow; ev.tree = {"确认", "取消"}; ev.i1 = 0;
    m_eventQueue->push(ev);

    // 等待确认结果 (事件循环在主循环中处理)
    int result = 1; // 默认取消
    while (m_confirmActive) {
        // 消费事件直到确认完成
        if (!m_eventQueue->empty()) {
            Event e = m_eventQueue->pop();
            if (e.type == EventType::ConfirmShow) {
                ev.i1 = e.i1; ev.tree = {"确认", "取消"};
                m_eventQueue->push(ev);
            } else if (e.type == EventType::ConfirmHide) {
                result = e.i1; // 0=确认, 1=取消
                m_confirmActive = false;
            } else if (e.type == EventType::KeyCycleMode) {
                cycleMode();
            }
        }
        // 读取键盘
        auto key = CLFConsole::readKey();
        if (key.m_key == CLFKey::Enter) {
            result = ev.i1; m_confirmActive = false;
        } else if (key.m_key == CLFKey::Esc || key.m_key == CLFKey::CtrlC) {
            result = 1; m_confirmActive = false;
        } else if (key.m_key == CLFKey::Up || key.m_key == CLFKey::Left
                   || key.m_key == CLFKey::Down || key.m_key == CLFKey::Right) {
            ev.i1 = (ev.i1 == 0) ? 1 : 0;
            ev.tree = {"确认", "取消"};
            m_eventQueue->push(ev);
        } else if (key.m_key == CLFKey::ShiftTab) {
            cycleMode();
        }
    }
    CLFTerminal::hideConfirm();
    CLFTerminal::scrollPrint(CLFTerminal::green("  ⎿ ✓ ") + (result == 0 ? "已确认" : "已取消") + "\n");
    return result == 0;
}

void CLFRepl::cycleMode() {
    auto cur = m_agent.getSecurityModeName();
    for (int i = 0; i < kModeCount; ++i) {
        if (std::string(kModeCycle[i]) == cur) {
            std::string nm = kModeCycle[(i + 1) % kModeCount];
            m_dispatcher->setModeName(nm);
            m_agent.setSecurityMode(CLFSecurityPolicy::modeFromString(nm));
            break;
        }
    }
}

void CLFRepl::saveSession(bool incomplete) {
    if (incomplete) m_agent.saveSession(m_historyDir, true);
    else { m_agent.saveSession(m_historyDir, false);
           CLFSessionManager::removeAllIncomplete(m_historyDir); }
}

} // namespace CLF::CLFCore
