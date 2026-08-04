// CLFRepl.cpp — REPL 主循环 (事件驱动)

#include "CLFUI/CLFRepl.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFUI/CLFTerminal.hpp"  // dynamic_cast + FTXUI in run()
#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFUI/CLFCommandDispatcher.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFUI/CLFConsole.hpp"
#include "CLFTypes/CLFEvent.hpp"
#include "CLFTypes/CLFEventQueue.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"
#include "CLFUI/CLFTerminal.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <thread>

namespace CLF::CLFUI {
using namespace CLF::CLFCore;

namespace {
const char* kModeCycle[] = {"auto", "analyze", "edit", "manual"};
constexpr int kModeCount = 4;
}

CLFRepl::CLFRepl(CLF::CLFCore::CLFAgentLoop& agent, const std::string& historyDir,
                 CLF::CLFTypes::ICLFOutput* output)
    : m_agent(agent)
    , m_output(output)
    , m_historyDir(historyDir)
    , m_dispatcher(std::make_unique<CLFCommandDispatcher>(agent, historyDir))
    , m_eventQueue(std::make_unique<CLF::CLFCore::CLFEventQueue>()) {
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

    // 构建 FTXUI UI
    auto* terminal = dynamic_cast<CLFTerminal*>(m_output);
    auto screen = ftxui::ScreenInteractive::FullscreenPrimaryScreen();
    if (terminal) terminal->setScreen(&screen);

    CLFTerminal::initLayout(m_dispatcher->modeName());
    printBanner();

    // 组件树
    std::string inputText;
    ftxui::InputOption inputOpt;
    inputOpt.cursor_position = 0;
    auto input = ftxui::Input(&inputText, "> ", inputOpt);
    auto root = ftxui::Container::Vertical({input});
    root->SetActiveChild(input);  // Input 获得焦点

    auto ui = ftxui::Renderer(root, [&] {
        // 内容区 — 自然顺序(最新在底部), yframe 自动跟踪新内容
        ftxui::Elements lines;
        if (terminal) {
            for (auto& l : terminal->m_contentBuffer)
                lines.push_back(ftxui::text(l));
            if (!terminal->m_pendingLine.empty())
                lines.push_back(ftxui::text(terminal->m_pendingLine));
        }
        auto scroll = ftxui::vbox(lines) | ftxui::frame | ftxui::flex;

        // 状态行
        auto status = terminal && !terminal->m_statusText.empty()
            ? ftxui::text("  " + terminal->m_statusText) | ftxui::dim
            : ftxui::emptyElement();

        // 模式行 (实时读取, 非初始化快照)
        auto modeStr = m_dispatcher->modeName();
        auto mode = ftxui::hbox({
            ftxui::text("  " + modeStr + " mode on"),
            ftxui::filler(),
            ftxui::text("shift+tab to cycle · esc to interrupt · /help for help"),
        }) | ftxui::dim;

        // 确认区
        auto confirmBar = terminal && terminal->m_confirmActive
            ? ftxui::text("  [" + CLFTerminal::green("●") + "] 确认    [ ] 取消")
            : ftxui::emptyElement();

        return ftxui::vbox({
            scroll,
            ftxui::separator(),
            status,
            ftxui::separator(),
            input->Render() | ftxui::border,
            ftxui::separator(),
            mode,
            confirmBar,
        });
    });

    // 异步提交 + 快捷键
    std::thread submitThread;
    std::atomic<bool> submitting{false};

    auto submitHandler = ftxui::CatchEvent(ui, [&](ftxui::Event e) {
        if (e == ftxui::Event::Return && !inputText.empty() && !submitting) {
            auto text = inputText;
            inputText.clear();
            submitting = true;
            if (submitThread.joinable()) submitThread.join();
            submitThread = std::thread([&, text]() {
                submit(text);
                submitting = false;
            });
            return true;
        }
        if (e == ftxui::Event::TabReverse) {
            cycleMode();
            return true;
        }
        if (e == ftxui::Event::Escape) {
            if (terminal && terminal->m_interruptCb)
                terminal->m_interruptCb();
            return true;
        }
        return false;
    });

    // 注入确认回调: FTXUI 嵌套 Loop
    m_agent.setConfirmCallback([&](const std::string& prompt) -> bool {
        return terminal ? terminal->confirm(prompt) : false;
    });

    // 默认新会话启动; 旧会话通过 /resume 命令手动恢复
    // checkIncompleteSession();

    screen.Loop(submitHandler);
    submitting = true;
    if (submitThread.joinable()) submitThread.join();

    CLFTerminal::restoreScrollRegion();
    } catch (...) {
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
    if(m_output) m_output->emitContent(CLFTerminal::bold("● CLFCode") + " — CLI Agent Framework for Code\n");
    if(m_output) m_output->emitContent("  ⎿ " + CLFTerminal::gray(CLFTerminal::diagnosticInfo()) + "\n");
    if(m_output) m_output->emitContent("  ⎿ 工作目录: " + CLFTerminal::cyan(cwd) + "\n");
    if(m_output) m_output->emitContent("  ⎿ 配置: " + CLFTerminal::cyan(config.m_apiBaseUrl) + "\n");
    if(m_output) m_output->emitContent("  ⎿ 模型: " + CLFTerminal::cyan(config.m_modelName) + "\n");
    int sc = CLFSkillLoader::loadFromDir(CLFConfigLoader::resolvePath("data/skills"));
    if (sc > 0) if(m_output) m_output->emitContent("  ⎿ 知识库: " + CLFTerminal::cyan(std::to_string(sc)) + " skills\n");
}

void CLFRepl::checkIncompleteSession() {
    std::string ip = CLFSessionManager::findIncomplete(m_historyDir);
    if (ip.empty()) return;
    if(m_output) m_output->emitContent(CLFTerminal::yellow("● ⚠ 检测到上次会话未正常结束") + "\n");
    if(m_output) m_output->emitContent("  ⎿ 是否恢复？[确认/取消]\n");
    if (confirmDialog("恢复上次会话")) {
        if (m_agent.restoreSession(ip))
            if(m_output) m_output->emitContent(CLFTerminal::green("  ⎿ ✓ 会话已恢复") + "\n");
        else
            if(m_output) m_output->emitContent(CLFTerminal::red("  ⎿ ✗ 会话恢复失败") + "\n");
        CLFSessionManager::promote(ip);
    } else {
        CLFSessionManager::removeAllIncomplete(m_historyDir);
        if(m_output) m_output->emitContent("  ⎿ 未完成的会话已丢弃\n");
    }
}

void CLFRepl::submit(const std::string& input) {
    CLFTerminal::toContentArea();
    auto* term = dynamic_cast<CLFTerminal*>(m_output);
    if (term) { term->m_contentBuffer.clear(); term->m_pendingLine.clear(); }
    if(m_output) m_output->emitContent("> " + CLFTerminal::bold(input) + "\n");
    if (m_dispatcher->handle(input)) { if(m_output) m_output->setStatus(""); return; }
    if(m_output) m_output->emitContent("● " + CLFTerminal::cyan("CLFCode") + ": ");
    try {
        auto t1 = std::chrono::steady_clock::now();
        std::string response = m_agent.runTurn(input);
        auto t2 = std::chrono::steady_clock::now();
        auto el = std::chrono::duration_cast<std::chrono::seconds>(t2 - t1).count();
        if (el > 0) {
            auto st = m_agent.getLastToolStats();
            if(m_output) m_output->emitContent(
                CLFTerminal::gray("  Thought for "+std::to_string((int)el)+"s") + "\n");
        }
        if (!response.empty() && response != "[Interrupted]")
            if(m_output) m_output->emitContent(response + "\n");
    } catch (const std::exception& e) {
        CLFLogger::instance().error(std::string("Fatal: ") + e.what());
        if(m_output) m_output->emitContent(CLFTerminal::red("✗ 异常: ") + e.what() + "\n");
        m_agent.clearContext();
    }
    auto st = m_agent.getLastToolStats();
    saveSession(st.totalCalls > 0);
    if(m_output) m_output->setStatus("");
    if(m_output) m_output->emitContent("\n");
}

bool CLFRepl::confirmDialog(const std::string& prompt) {
    auto* term = dynamic_cast<CLFTerminal*>(m_output);
    if (term) return term->confirm(prompt);  // FTXUI 嵌套 Loop
    // 回退: 旧 CLFConsole 实现
    if(m_output) m_output->emitContent("\n● " + CLFTerminal::yellow("⚠ 高风险操作确认") + "\n");
    size_t p = prompt.find('\n');
    if(m_output) m_output->emitContent("  ⎿ " + CLFTerminal::cyan(p != std::string::npos ? prompt.substr(0, p) : prompt) + "\n");
    if (p != std::string::npos) {
        std::string args = prompt.substr(p + 1);
        const std::string ap = "参数: ";
        if (args.rfind(ap, 0) == 0) args = args.substr(ap.size());
        if(m_output) m_output->emitContent("  ⎿ 参数: " + CLFTerminal::gray(args) + "\n");
    }

    std::vector<std::string> opts = {"确认", "取消"};
    int sel = 0;
    CLFTerminal::showConfirm(opts, sel);
    CLFTerminal::drawInput(m_input, m_cursorPos); // 保持固定区完整

    while (true) {
        auto key = CLFConsole::readKey();
        if (key.m_key == CLFKey::Enter) break;
        if (key.m_key == CLFKey::Esc || key.m_key == CLFKey::CtrlC) { sel = 1; break; }
        if (key.m_key == CLFKey::Up || key.m_key == CLFKey::Left
            || key.m_key == CLFKey::Down || key.m_key == CLFKey::Right) {
            sel = (sel == 0) ? 1 : 0;
            CLFTerminal::showConfirm(opts, sel);
        }
        if (key.m_key == CLFKey::ShiftTab) cycleMode();
    }
    CLFTerminal::hideConfirm();
    if(m_output) m_output->emitContent(CLFTerminal::green("  ⎿ ✓ ") + (sel == 0 ? "已确认" : "已取消") + "\n");
    return sel == 0;
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

} // namespace CLF::CLFUI
