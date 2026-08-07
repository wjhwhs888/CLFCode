// CLFRepl.cpp — REPL 主循环 (FTXUI 全帧驱动)
// 快捷键处理: 第 1 批（核心输入输出）已实现

#include "CLFUI/CLFRepl.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFUI/CLFAsyncSubmit.hpp"
#include "CLFUI/CLFClipboard.hpp"
#include "CLFUI/CLFCommandDispatcher.hpp"
#include "CLFUI/CLFConfirmBar.hpp"
#include "CLFUI/CLFScrollView.hpp"
#include "CLFUI/CLFTerminal.hpp"
#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>

namespace CLF::CLFUI {
using namespace CLF::CLFCore;

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

        // ---- 组件声明 ----
        CLFScrollView   scrollView;
        CLFAsyncSubmit  asyncSubmit;

        ftxui::InputOption inputOpt;
        inputOpt.multiline = true;  // 多行显示（Alt+Enter/Ctrl+N 换行后可见多行）

        std::string inputText;
        auto input = ftxui::Input(&inputText, "❯ ", inputOpt);
        auto root  = ftxui::Container::Vertical({input});
        root->SetActiveChild(input);
        input->TakeFocus();

        CLFConfirmBar confirmBar;

        // ---- 主渲染器 ----
        auto ui = ftxui::Renderer(root, [&] {
            if (terminal) terminal->m_refreshPending = false;

            // 每帧剥离 CPR / ANSI 残留（\033 被 CatchEvent 吃掉后残留 [n;mR）
            // 不能用 m_justInterrupted 单帧判断（CPR 字节可能在渲染后到达）
            for (size_t i = 0; i < inputText.size(); ) {
                if (inputText[i] == '\033') {
                    size_t end = i + 1;
                    while (end < inputText.size() && inputText[end] >= 0x20
                           && inputText[end] < 0x40) ++end;
                    if (end < inputText.size() && inputText[end] >= 0x40
                        && inputText[end] <= 0x7E) ++end;
                    inputText.erase(i, end - i);
                } else { ++i; }
            }
            if (inputText.size() >= 3 && inputText.back() == 'R') {
                auto pos = inputText.rfind('[');
                if (pos != std::string::npos && pos + 2 < inputText.size()) {
                    bool valid = true;
                    for (size_t j = pos + 1; j + 1 < inputText.size(); ++j) {
                        char c = inputText[j];
                        if (!((c >= '0' && c <= '9') || c == ';')) { valid = false; break; }
                    }
                    if (valid) inputText.erase(pos);
                }
            }
            // 中断后恢复上次提交的输入
            if (m_needRestoreInput) {
                if (inputText.empty()) {
                    inputText = m_lastSubmittedInput;
                }
                m_needRestoreInput = false;
            }

            auto snap = terminal ? terminal->contentSnapshot()
                                 : CLFTerminal::ContentSnapshot{};

            // 获取终端宽度用于硬换行（避免长行超出视口）
            int termW = CLFTerminal::getTerminalWidth();
            int wrapW = (termW > 20) ? termW - 2 : 78;  // 留 2 列 margin

            ftxui::Elements allLines;
            for (auto& l : snap.lines) {
                if (wrapW > 0) {
                    for (size_t pos = 0; pos < l.size(); pos += wrapW)
                        allLines.push_back(ftxui::text(l.substr(pos, wrapW)));
                } else {
                    allLines.push_back(ftxui::text(l));
                }
            }
            if (!snap.pendingLine.empty()) {
                auto& pl = snap.pendingLine;
                if (wrapW > 0) {
                    for (size_t pos = 0; pos < pl.size(); pos += wrapW)
                        allLines.push_back(ftxui::text(pl.substr(pos, wrapW)));
                } else {
                    allLines.push_back(ftxui::text(pl));
                }
            }

            scrollView.update(static_cast<int>(allLines.size()),
                              CLFTerminal::getTerminalHeight(), 7);
            auto contentArea = ftxui::vbox(scrollView.renderWindow(allLines)) | ftxui::flex;

            auto statusLine = !snap.statusText.empty()
                ? ftxui::dim(ftxui::text("  " + snap.statusText))
                : ftxui::emptyElement();

            auto modeLine = ftxui::dim(ftxui::hbox({
                ftxui::text("  " + m_dispatcher->modeName() + " mode on"),
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

        // ---- CatchEvent: 快捷键处理 ----
        auto handler = ftxui::CatchEvent(ui, [&](ftxui::Event e) {

            // === 1. 确认栏激活时（最小化处理，防卡死）===
            if (terminal && terminal->isConfirmActive()) {
                // "返回"/ESC/CtrlC 统一行为: 拒绝 + 中断 Agent，回到输入编辑
                auto cancelWithInterrupt = [&] {
                    if (terminal->m_interruptCb)
                        terminal->m_interruptCb();  // 先中断再唤醒 worker
                    {
                        std::lock_guard lock(terminal->m_confirmMutex);
                        terminal->m_confirmResult = false;
                        terminal->setConfirmActive(false);
                    }
                    terminal->m_confirmCv.notify_one();
                };

                if (e == ftxui::Event::Return) {
                    if (terminal->m_confirmSel == 0) {
                        // "确认" → 同意执行
                        std::lock_guard lock(terminal->m_confirmMutex);
                        terminal->m_confirmResult = true;
                        terminal->setConfirmActive(false);
                        terminal->m_confirmCv.notify_one();
                    } else {
                        // "返回" → 中断
                        cancelWithInterrupt();
                    }
                    return true;
                }
                if (e == ftxui::Event::Escape || e == ftxui::Event::CtrlC) {
                    cancelWithInterrupt();
                    return true;
                }
                if (e == ftxui::Event::ArrowLeft || e == ftxui::Event::ArrowRight) {
                    // 两选项切换: 0↔1
                    terminal->m_confirmSel = 1 - terminal->m_confirmSel;
                    return true;
                }
                return true;  // 屏蔽其他所有按键
            }

            // === 2. 提交 ===
            if (e == ftxui::Event::Return || e == ftxui::Event::CtrlD) {
                if (!inputText.empty() && !asyncSubmit.busy()) {
                    m_lastSubmittedInput = inputText;
                    auto text = inputText;
                    inputText.clear();
                    asyncSubmit.launch([this, text]() { submit(text); });
                }
                return true;
            }

            // === 3. 换行（仅 Ctrl+N, Alt+Enter 见第 2 批）===
            if (e == ftxui::Event::CtrlN) {
                input->OnEvent(ftxui::Event::Character("\n"));
                return true;
            }

            // === 4. Ctrl+C: 上下文感知分发 ===
            if (e == ftxui::Event::CtrlC) {
                m_escPending = false;
                if (asyncSubmit.busy()) {
                    if (terminal && terminal->m_interruptCb)
                        terminal->m_interruptCb();
                } else {
                    screen.ExitLoopClosure()();
                }
                return true;
            }

            // === 5. Esc: 立即中断（双击退出 + Alt+Enter 见第 2 批）===
            if (e == ftxui::Event::Escape) {
                if (terminal && terminal->m_interruptCb)
                    terminal->m_interruptCb();
                m_justInterrupted = true;
                // Agent 运行中（或 CPR 的 ESC 紧随其后的情况）→ 清空输入截获 CPR 泄露
                if (asyncSubmit.busy() || m_needRestoreInput) {
                    inputText.clear();
                    m_needRestoreInput = true;
                }
                // 接下来 N 帧持续剥离 CPR（捕获跨事件循环到达的残留字节）
                m_escCleanupFrames = 3;
                if (terminal) terminal->setStatus("⏹ 中断中…");
                screen.PostEvent(ftxui::Event::Custom);
                return true;
            }

            // === 6. Tab: 占位拦截 ===
            if (e == ftxui::Event::Tab) {
                if (input->Focused()) {
                    return true;  // 吃掉事件，后续实现补全
                }
                return false;  // 非输入区放行
            }

            // === 7. 模式切换 ===
            if (e == ftxui::Event::TabReverse) {
                cycleMode();
                return true;
            }

            // === 8. 滚动 ===
            if (scrollView.handleEvent(e))
                return true;

            // === 通用：剥离泄露到输入框的 CPR/ANSI 残留 ===
            // 终端 CPR \033[n;mR 的 \033 被 CatchEvent 吃掉后，[n;mR 作为
            // Character 事件到达此处。在所有 handler 未匹配时统一剥离。
            for (size_t i = 0; i < inputText.size(); ) {
                if (inputText[i] == '\033') {
                    size_t end = i + 1;
                    while (end < inputText.size() && inputText[end] >= 0x20
                           && inputText[end] < 0x40) ++end;
                    if (end < inputText.size() && inputText[end] >= 0x40
                        && inputText[end] <= 0x7E) ++end;
                    inputText.erase(i, end - i);
                } else { ++i; }
            }
            if (inputText.size() >= 3 && inputText.back() == 'R') {
                auto pos = inputText.rfind('[');
                if (pos != std::string::npos && pos + 2 < inputText.size()) {
                    bool valid = true;
                    for (size_t j = pos + 1; j + 1 < inputText.size(); ++j) {
                        char c = inputText[j];
                        if (!((c >= '0' && c <= '9') || c == ';')) { valid = false; break; }
                    }
                    if (valid) inputText.erase(pos);
                }
            }

            // ESC 后延迟清理帧：继续 Post Custom 事件触发后续渲染
            if (m_escCleanupFrames > 0) {
                --m_escCleanupFrames;
                if (m_escCleanupFrames > 0)
                    screen.PostEvent(ftxui::Event::Custom);
            }

            return false;
        });

        // ---- 运行 ----
        screen.Loop(handler);
        asyncSubmit.join();
        if (m_escTimer.joinable()) m_escTimer.join();
        if (terminal) terminal->setScreen(nullptr);

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

    if (m_dispatcher->handle(input)) {
        if (m_output) m_output->setStatus("");
        return;
    }

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
