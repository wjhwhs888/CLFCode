// CLFRepl.cpp — REPL 主循环 (FTXUI 全帧驱动 + v7 Modal)

#include "CLFUI/CLFRepl.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFUI/CLFTerminal.hpp"
#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFUI/CLFCommandDispatcher.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"
#include "CLFUI/CLFClipboard.hpp"
#include "CLFUI/CLFAsyncSubmit.hpp"
#include "CLFUI/CLFScrollView.hpp"
#include "CLFUI/CLFConfirmBar.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>

namespace CLF::CLFUI {
using namespace CLF::CLFCore;

namespace {
// Keybinding 说明（Renderer + CatchEvent 共用，防止 drift）
const char* kKeybindLegend =
    "shift+tab 切换 · esc 中断 · /help 帮助"
    " | ctrl+d 提交 · enter 换行 · ctrl+v 粘贴 · ctrl+y 全量复制";
}

// ============================================================================
// 构造 / 析构
// ============================================================================

CLFRepl::CLFRepl(CLF::CLFCore::CLFAgentLoop& agent, const std::string& historyDir,
                 CLF::CLFTypes::ICLFOutput* output)
    : m_agent(agent)
    , m_output(output)
    , m_historyDir(historyDir)
    , m_dispatcher(std::make_unique<CLFCommandDispatcher>(agent, historyDir, output, nullptr)) {
    m_agent.setConfirmCallback(
        [this](const std::string& prompt) { return confirmDialog(prompt); });
}

CLFRepl::~CLFRepl() = default;

// ============================================================================
// run() — FTXUI 主循环
// ============================================================================

int CLFRepl::run() {
    try {
        // 启动临时文件清理
        try {
            for (auto& e : std::filesystem::directory_iterator(".")) {
                std::string n = e.path().filename().string();
                if (n.find("clf_cmd_stdout_") == 0 || n.find("clf_cmd_stderr_") == 0)
                    std::filesystem::remove(e.path());
            }
        } catch (...) {}

        // ---- 初始化 FTXUI ----
        auto* terminal = dynamic_cast<CLFTerminal*>(m_output);
        auto  screen   = ftxui::ScreenInteractive::FullscreenPrimaryScreen();
        if (terminal) {
            terminal->setScreen(&screen);
            terminal->m_contentBuffer.clear();
            terminal->m_pendingLine.clear();
        }
        m_dispatcher->setOnExit([&] { screen.ExitLoopClosure()(); });
        printBanner();

        // ---- 输入组件 ----
        std::string inputText;
        ftxui::InputOption inputOpt;
        inputOpt.multiline = true;
        auto input = ftxui::Input(&inputText, "❯ ", inputOpt);
        auto root = ftxui::Container::Vertical({input});
        root->SetActiveChild(input);
        input->TakeFocus();

        // ---- 组件 ----
        CLFScrollView   scrollView;
        CLFConfirmBar   confirmBar;
        CLFAsyncSubmit  asyncSubmit;

        // ---- 主渲染器 ----
        auto ui = ftxui::Renderer(root, [&] {
            // 防抖
            if (terminal) terminal->m_refreshPending = false;

            // 线程安全快照
            auto snap = terminal ? terminal->contentSnapshot()
                                 : CLFTerminal::ContentSnapshot{};

            // 构建全部内容行 + 更新滚动视口
            ftxui::Elements allLines;
            for (auto& l : snap.lines)
                allLines.push_back(ftxui::text(l));
            if (!snap.pendingLine.empty())
                allLines.push_back(ftxui::text(snap.pendingLine));

            scrollView.update(static_cast<int>(allLines.size()),
                              CLFTerminal::getTerminalHeight());

            auto contentArea = ftxui::vbox(scrollView.renderWindow(allLines))
                             | ftxui::flex;

            // 状态行
            auto statusLine = !snap.statusText.empty()
                ? ftxui::dim(ftxui::text("  " + snap.statusText))
                : ftxui::emptyElement();

            // 模式行
            auto modeLine = ftxui::dim(ftxui::hbox({
                ftxui::text("  " + m_dispatcher->modeName() + " mode on"),
                ftxui::filler(),
                ftxui::dim(ftxui::text(kKeybindLegend)),
            }));

            return ftxui::vbox({
                contentArea,
                statusLine,
                ftxui::separator(),
                input->Render(),
                ftxui::separator(),
                modeLine,
                confirmBar.render(*terminal),
            });
        });

        // ---- 事件处理 ----
        auto handler = ftxui::CatchEvent(ui, [&](ftxui::Event e) {
            // 1) 确认栏按键
            if (terminal && confirmBar.handleEvent(e, *terminal))
                return true;

            // 2) 滚动按键
            if (scrollView.handleEvent(e))
                return true;

            // 3) Ctrl+D → 异步提交
            if (e == ftxui::Event::CtrlD && !inputText.empty() && !asyncSubmit.busy()) {
                auto text = inputText;
                inputText.clear();
                asyncSubmit.launch([&, text]() { submit(text); });
                return true;
            }

            // 4) Shift+Tab → 切换模式
            if (e == ftxui::Event::TabReverse) {
                cycleMode();
                return true;
            }

            // 5) Escape / Ctrl+C → 中断
            if (e == ftxui::Event::Escape || e == ftxui::Event::CtrlC) {
                if (terminal && terminal->m_interruptCb)
                    terminal->m_interruptCb();
                return true;
            }

            // 6) Ctrl+V → 粘贴
            if (e == ftxui::Event::CtrlV) {
                std::string clip = CLFClipboard::read();
                if (!clip.empty()) inputText += clip;
                return true;
            }

            // 7) Ctrl+Y → 全量复制
            if (e == ftxui::Event::CtrlY) {
                std::string all;
                if (terminal) {
                    for (auto& line : terminal->m_contentBuffer)
                        all += line + "\n";
                }
                CLFClipboard::write(all);
                return true;
            }

            return false;
        });

        // ---- 运行 ----
        screen.Loop(handler);
        asyncSubmit.join();

    } catch (...) {
        std::cerr << "[Fatal] Unexpected error — CLFCode terminated." << std::endl;
        return 1;
    }
    return 0;
}

// ============================================================================
// submit / confirm / cycle / printBanner / saveSession
// ============================================================================

void CLFRepl::printBanner() {
    const auto& config = m_agent.getConfig();
    std::string cwd = CLFConfigLoader::getWorkingDir();
    if (m_output) m_output->emitContent(
        CLFTerminal::bold("● CLFCode") + " — CLI Agent Framework for Code\n");
    if (m_output) m_output->emitContent(
        "  ⎿ " + CLFTerminal::gray(CLFTerminal::diagnosticInfo()) + "\n");
    if (m_output) m_output->emitContent(
        "  ⎿ 工作目录: " + CLFTerminal::cyan(cwd) + "\n");
    if (m_output) m_output->emitContent(
        "  ⎿ 配置: " + CLFTerminal::cyan(config.m_apiBaseUrl) + "\n");
    if (m_output) m_output->emitContent(
        "  ⎿ 模型: " + CLFTerminal::cyan(config.m_modelName) + "\n");
    int sc = CLFSkillLoader::loadFromDir(CLFConfigLoader::resolvePath("data/skills"));
    if (sc > 0) if (m_output) m_output->emitContent(
        "  ⎿ 知识库: " + CLFTerminal::cyan(std::to_string(sc)) + " skills\n");
}

void CLFRepl::submit(const std::string& input) {
    if (m_output) m_output->emitContent("> " + CLFTerminal::bold(input) + "\n");

    // 命令分发
    if (m_dispatcher->handle(input)) {
        if (m_output) m_output->setStatus("");
        return;
    }

    // Agent 调用
    if (m_output) m_output->emitContent("● " + CLFTerminal::cyan("CLFCode") + ": ");
    try {
        auto t1 = std::chrono::steady_clock::now();
        std::string response = m_agent.runTurn(input);
        auto t2 = std::chrono::steady_clock::now();
        auto el = std::chrono::duration_cast<std::chrono::seconds>(t2 - t1).count();
        if (el > 0) {
            if (m_output) m_output->emitContent(
                "\n" + CLFTerminal::gray("  Thought for " + std::to_string((int)el) + "s") + "\n");
        }
        if (!response.empty() && response != "[Interrupted]")
            if (m_output) m_output->emitContent(response + "\n");
    } catch (const std::exception& e) {
        CLFLogger::instance().error(std::string("Fatal: ") + e.what());
        if (m_output) m_output->emitContent(
            CLFTerminal::red("✗ 异常: ") + e.what() + "\n");
        m_agent.clearContext();
    }

    auto st = m_agent.getLastToolStats();
    saveSession(st.totalCalls > 0);
    if (m_output) m_output->setStatus("");
    if (m_output) m_output->emitContent("\n");
}

bool CLFRepl::confirmDialog(const std::string& prompt) {
    auto* term = dynamic_cast<CLFTerminal*>(m_output);
    if (term) return term->confirm(prompt);
    return false;
}

void CLFRepl::cycleMode() {
    auto next = CLFSecurityPolicy::nextMode(
        CLFSecurityPolicy::modeFromString(m_agent.getSecurityModeName()));
    m_agent.setSecurityMode(next);
}

void CLFRepl::saveSession(bool incomplete) {
    if (incomplete) {
        m_agent.saveSession(m_historyDir, true);
    } else {
        m_agent.saveSession(m_historyDir, false);
        CLFSessionManager::removeAllIncomplete(m_historyDir);
    }
}

} // namespace CLF::CLFUI
