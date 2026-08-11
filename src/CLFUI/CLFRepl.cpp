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
        inputOpt.multiline = true;  // 多行显示（Ctrl+N 换行后可见多行）
        ftxui::Ref<int> cursorPos = 0;
        inputOpt.cursor_position = cursorPos;  // ↑/↓ 历史导航需要光标位置

        std::string inputText;
        auto input = ftxui::Input(&inputText, "❯ ", inputOpt);
        auto root  = ftxui::Container::Vertical({input});
        root->SetActiveChild(input);
        // 暂不调 TakeFocus(): FTXUI v7 可能内部发 DSR 查询光标位置，
        //   终端 CPR 响应 \033[1;1R 会与用户 ESC 的 \033 在 stdin 合并
        // input->TakeFocus();

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
            int wrapW = (termW > 20) ? termW : 78;  // 不留 margin，用满终端宽度

            ftxui::Elements allLines;
            const bool hasStyles = (snap.lineStyles.size() == snap.lines.size());

            for (size_t i = 0; i < snap.lines.size(); ++i) {
                auto& l = snap.lines[i];
                ftxui::Element el;
                if (wrapW > 0 && l.size() > static_cast<size_t>(wrapW)) {
                    // 长行拆分为多个子元素，颜色统一应用
                    ftxui::Elements parts;
                    for (size_t pos = 0; pos < l.size(); pos += wrapW)
                        parts.push_back(ftxui::text(l.substr(pos, wrapW)));
                    el = ftxui::vbox(std::move(parts));
                } else {
                    el = ftxui::text(l);
                }
                if (hasStyles && i < snap.lineStyles.size()) {
                    auto s = static_cast<CLF::CLFTypes::ICLFOutput::LineStyle>(snap.lineStyles[i]);
                    if (s == CLF::CLFTypes::ICLFOutput::LineStyle::Add)
                        el = el | ftxui::color(ftxui::Color::Green);
                    else if (s == CLF::CLFTypes::ICLFOutput::LineStyle::Remove)
                        el = el | ftxui::color(ftxui::Color::Red);
                    else if (s == CLF::CLFTypes::ICLFOutput::LineStyle::Context)
                        el = ftxui::dim(el);
                }
                allLines.push_back(el);
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

            // ---- 思考过程（内容区最后，Ctrl+T 折叠/展开） ----
            if (snap.thinkingActive || !snap.thinkingLines.empty()) {
                allLines.push_back(ftxui::dim(ftxui::text(
                    "  Thought for " + std::to_string(snap.thinkingElapsed)
                    + "s (ctrl+t 展开)")));
                if (!snap.thinkingActive && m_showThinking && !snap.thinkingLines.empty()) {
                    for (auto& tl : snap.thinkingLines)
                        allLines.push_back(ftxui::dim(ftxui::text("  " + tl)));
                }
            }

            scrollView.update(static_cast<int>(allLines.size()),
                              CLFTerminal::getTerminalHeight(), 7);
            auto contentArea = ftxui::vbox(scrollView.renderWindow(allLines)) | ftxui::flex;

            // ---- 渐进式进度块（插在内容区和 statusLine 之间） ----
            ftxui::Elements progressElements;
            if (!snap.progressLines.empty()) {
                for (auto& pl : snap.progressLines) {
                    if (!pl.empty())
                        progressElements.push_back(ftxui::text(pl));
                }
            }

            auto statusLine = !snap.statusText.empty()
                ? ftxui::dim(ftxui::text("  " + snap.statusText))
                : ftxui::emptyElement();

            // 状态栏：模型名 │ 目录 │ 安全模式 │ 快捷键
            auto modeLine = ftxui::dim(ftxui::hbox({
                ftxui::text("  " + m_agent.getConfig().m_modelName),
                ftxui::separator(),
                ftxui::text(" 📁 " + std::filesystem::path(CLFConfigLoader::getWorkingDir()).filename().string()),
                ftxui::separator(),
                ftxui::text(" 🔒 " + m_dispatcher->modeName()),
                ftxui::filler(),
                ftxui::text("/help 帮助  "),
            }));

            return ftxui::vbox({
                contentArea,
                ftxui::vbox(std::move(progressElements)),
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
                // Shift+Tab: 确认栏期间仍可切换安全模式
                if (e == ftxui::Event::TabReverse) {
                    cycleMode();
                    return true;
                }
                return true;  // 屏蔽其他所有按键
            }

            // === 2. 提交 ===
            if (e == ftxui::Event::Return || e == ftxui::Event::CtrlD) {
                if (!inputText.empty() && !asyncSubmit.busy()) {
                    m_lastSubmittedInput = inputText;
                    m_inputHistory.push_back(inputText);
                    m_historyIndex = -1;
                    auto text = inputText;
                    inputText.clear();
                    asyncSubmit.launch([this, text]() { submit(text); });
                }
                return true;
            }

            // === 3. 换行 ===
            if (e == ftxui::Event::CtrlN) {
                input->OnEvent(ftxui::Event::Character("\n"));
                return true;
            }

            // Ctrl+T: 切换思考过程显示/隐藏
            if (e == ftxui::Event::CtrlT) {
                m_showThinking = !m_showThinking;
                return true;
            }

            // === 4a. ↑/↓ 历史导航（光标在首行按↑ / 尾行按↓ 触发） ===
            if (e == ftxui::Event::ArrowUp || e == ftxui::Event::ArrowDown) {
                int pos = *cursorPos;
                // ↑：光标在首行 + 有历史 → 取上一条
                if (e == ftxui::Event::ArrowUp) {
                    size_t prevNl = (pos == 0) ? std::string::npos
                                               : inputText.rfind('\n', pos - 1);
                    if (prevNl == std::string::npos && !m_inputHistory.empty()) {
                        if (m_historyIndex == -1) {
                            m_historyDraft = inputText;  // 保存当前草稿
                            m_historyIndex = static_cast<int>(m_inputHistory.size()) - 1;
                        } else if (m_historyIndex > 0) {
                            m_historyIndex--;
                        }
                        inputText = m_inputHistory[m_historyIndex];
                        *cursorPos = static_cast<int>(inputText.size());
                        return true;
                    }
                }
                // ↓：光标在尾行 + 在历史中 → 取下一条
                if (e == ftxui::Event::ArrowDown) {
                    size_t nextNl = inputText.find('\n', pos);
                    if (nextNl == std::string::npos && m_historyIndex != -1) {
                        if (m_historyIndex < static_cast<int>(m_inputHistory.size()) - 1) {
                            m_historyIndex++;
                            inputText = m_inputHistory[m_historyIndex];
                        } else {
                            // ↓ 到底 → 恢复进入历史前正在编辑的草稿
                            m_historyIndex = -1;
                            inputText = std::move(m_historyDraft);
                        }
                        *cursorPos = static_cast<int>(inputText.size());
                        return true;
                    }
                }
                // 非边界 → 不拦截，让 Input 组件正常处理行内移动
                return false;
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

            // === 5. Esc: 双击退出 + 立即中断 ===
            // 注：Alt+Enter 弃用（终端层面触发全屏），换行用 Ctrl+N
            if (e == ftxui::Event::Escape
                || e == ftxui::Event::Special({27, 27})) {
                // 5a. 双击检测（空闲时 500ms 内连续两次 Esc → 退出）
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_lastEscTime).count();
                if (elapsed < 500 && !asyncSubmit.busy()) {
                    m_lastEscTime = {};
                    m_dispatcher->handle("/exit");
                    return true;
                }
                m_lastEscTime = now;

                // 5b. 立即中断
                if (terminal && terminal->m_interruptCb)
                    terminal->m_interruptCb();
                m_justInterrupted = true;
                if (asyncSubmit.busy() || m_needRestoreInput) {
                    inputText.clear();
                    m_needRestoreInput = true;
                }
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
    CLFLogger::instance().info("[Submit] entry, input="
        + (input.size() > 60 ? input.substr(0, 60) + "..." : input));
    if (m_output) m_output->emitContent("> " + CLFTerminal::bold(input) + "\n");

    if (m_dispatcher->handle(input)) {
        if (m_output) m_output->setStatus("");
        CLFLogger::instance().info("[Submit] handled by dispatcher");
        return;
    }

    if (m_output) m_output->emitContent("\n● " + CLFTerminal::cyan("CLFCode") + ":\n ");
    try {
        std::string response = m_agent.runTurn(input);
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
    CLFLogger::instance().info("[Submit] exit, tools=" + std::to_string(st.totalCalls));
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
