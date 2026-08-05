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
#include "CLFTypes/CLFEventQueue.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#undef min
#undef max
#endif

namespace CLF::CLFUI {
using namespace CLF::CLFCore;

// 从系统剪贴板读取文本 (Ctrl+V 粘贴)
static std::string readClipboard() {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return "";
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); return ""; }
    wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(h));
    if (!wstr) { CloseClipboard(); return ""; }
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    std::string out(len > 0 ? len - 1 : 0, '\0');
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &out[0], len, nullptr, nullptr);
    GlobalUnlock(h);
    CloseClipboard();
    return out;
#else
    return "";
#endif
}

// 写入系统剪贴板 (Ctrl+Y 复制全部内容)
static void writeClipboard(const std::string& text) {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wlen <= 0) { CloseClipboard(); return; }
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
    if (!hMem) { CloseClipboard(); return; }
    wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(hMem));
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wstr, wlen);
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
#endif
}

namespace {
const char* kModeCycle[] = {"auto", "analyze", "edit", "manual"};
constexpr int kModeCount = 4;
}

// ============================================================================
// 构造 / 析构
// ============================================================================

CLFRepl::CLFRepl(CLF::CLFCore::CLFAgentLoop& agent, const std::string& historyDir,
                 CLF::CLFTypes::ICLFOutput* output)
    : m_agent(agent)
    , m_output(output)
    , m_historyDir(historyDir)
    , m_dispatcher(std::make_unique<CLFCommandDispatcher>(agent, historyDir, output, nullptr))
    , m_eventQueue(std::make_unique<CLF::CLFCore::CLFEventQueue>()) {
    m_agent.setEventQueue(m_eventQueue.get());
    m_agent.setConfirmCallback(
        [this](const std::string& prompt) { return confirmDialog(prompt); });
    m_agent.setStatusCallback([this](const std::string& title, const std::string& content) {
        if (m_output) m_output->setStatus(title + ": " + content);
    });
}

CLFRepl::~CLFRepl() = default;

// ============================================================================
// run() — FTXUI 主循环
// ============================================================================

int CLFRepl::run() {
    try {
        // 启动清理
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
        inputOpt.multiline = true;  // Enter 换行, Ctrl+D 提交
        auto input = ftxui::Input(&inputText, "❯ ", inputOpt);

        auto root = ftxui::Container::Vertical({input});
        root->SetActiveChild(input);
        input->TakeFocus();

        // ---- 滚动偏移 (鼠标滚轮控制) ----
        int  scrollOffset   = 0;      // >0 = 向上滚动了多少行, 0 = 底部
        bool autoScroll      = true;  // 新内容到达时自动回底部
        int  lastTotalLines  = 0;     // 追踪内容增长以触发自动滚动

        // ---- 主渲染器 ----
        auto ui = ftxui::Renderer(root, [&] {
            // 防抖: 本帧已渲染, 允许新的 PostEvent
            if (terminal) terminal->m_refreshPending = false;

            // 构建全部内容行
            ftxui::Elements allLines;
            if (terminal) {
                for (auto& l : terminal->m_contentBuffer)
                    allLines.push_back(ftxui::text(l));
                if (!terminal->m_pendingLine.empty())
                    allLines.push_back(ftxui::text(terminal->m_pendingLine));
            }
            const int totalLines = static_cast<int>(allLines.size());

            // 自动滚动: 新内容到达 → 回底部
            if (totalLines != lastTotalLines) {
                lastTotalLines = totalLines;
                if (autoScroll) scrollOffset = 0;
            }

            // 计算可见窗口 (给输入/状态/模式留 6 行)
            const int termH  = CLFTerminal::getTerminalHeight();
            const int viewH  = std::max(8, termH - 6);
            const int maxOff = std::max(0, totalLines - viewH);
            if (scrollOffset < 0)        scrollOffset = 0;
            if (scrollOffset > maxOff)   scrollOffset = maxOff;

            // 截取可见行
            const int startLine = totalLines - viewH - scrollOffset;
            ftxui::Elements visible;
            for (int i = std::max(0, startLine);
                 i < totalLines && (int)visible.size() < viewH; ++i) {
                visible.push_back(allLines[i]);
            }

            // 滚动位置指示 (顶部/底部)
            if (scrollOffset > 0) {
                auto hint = "↑ " + std::to_string(scrollOffset) + " lines above";
                visible.insert(visible.begin(),
                    ftxui::dim(ftxui::text("  " + hint)));
            }
            if (scrollOffset < maxOff) {
                auto hint = "↓ " + std::to_string(maxOff - scrollOffset) + " lines below";
                visible.push_back(ftxui::dim(ftxui::text("  " + hint)));
            }

            auto contentArea = ftxui::vbox(visible) | ftxui::flex;

            // 状态行
            auto statusLine = terminal && !terminal->m_statusText.empty()
                ? ftxui::dim(ftxui::text("  " + terminal->m_statusText))
                : ftxui::emptyElement();

            // 模式行
            auto modeStr = m_dispatcher->modeName();
            auto modeLine = ftxui::dim(ftxui::hbox({
                ftxui::text("  " + modeStr + " mode on"),
                ftxui::filler(),
                ftxui::dim(ftxui::text(
                    "shift+tab 切换 · esc 中断 · /help 帮助"
                    " | ctrl+d 提交 · enter 换行 · ctrl+v 粘贴 · ctrl+y 全量复制")),
            }));

            // 确认栏 (设计 §3.6: 底部固定, paragraph 自动换行)
            auto confirmBar = terminal && terminal->m_confirmActive
                ? ftxui::vbox({
                      ftxui::separator(),
                      ftxui::color(ftxui::Color::Yellow,
                                   ftxui::paragraph("  ⚠ " + terminal->m_confirmPrompt)),
                      ftxui::hbox({
                          ftxui::text("  [") | ftxui::dim,
                          (0 == terminal->m_confirmSel
                              ? ftxui::bold(ftxui::text("●") | ftxui::color(ftxui::Color::Green))
                              : ftxui::dim(ftxui::text("○"))),
                          ftxui::text("] " + terminal->m_confirmOpts[0] + "    "),
                          ftxui::text("[") | ftxui::dim,
                          (1 == terminal->m_confirmSel
                              ? ftxui::bold(ftxui::text("●") | ftxui::color(ftxui::Color::Green))
                              : ftxui::dim(ftxui::text("○"))),
                          ftxui::text("] " + terminal->m_confirmOpts[1]),
                          ftxui::filler(),
                          ftxui::dim(ftxui::text("← → 选择  Enter 确认  Esc 取消")),
                      }),
                  })
                : ftxui::emptyElement();

            return ftxui::vbox({
                contentArea,
                statusLine,
                ftxui::separator(),
                input->Render(),
                ftxui::separator(),
                modeLine,
                confirmBar,
            });
        });

        // ---- 主事件处理 ----
        std::thread       submitThread;
        std::atomic<bool> submitting{false};

        auto submitHandler = ftxui::CatchEvent(ui, [&](ftxui::Event e) {
            // 鼠标滚轮 → 内容区滚动
            if (e.is_mouse()) {
                auto& mouse = e.mouse();
                if (mouse.button == ftxui::Mouse::WheelUp) {
                    scrollOffset += 3;  autoScroll = false;  return true;
                }
                if (mouse.button == ftxui::Mouse::WheelDown) {
                    scrollOffset -= 3;  if (scrollOffset <= 0)
                        { scrollOffset = 0; autoScroll = true; } return true;
                }
            }
            // 键盘翻页
            if (e == ftxui::Event::PageUp) {
                scrollOffset += 15;  autoScroll = false;  return true;
            }
            if (e == ftxui::Event::PageDown) {
                scrollOffset -= 15;  if (scrollOffset <= 0)
                    { scrollOffset = 0; autoScroll = true; } return true;
            }
            if (e == ftxui::Event::Home) {
                scrollOffset = 999999;  autoScroll = false;  return true;  // clamp in renderer
            }
            if (e == ftxui::Event::End) {
                scrollOffset = 0;  autoScroll = true;  return true;
            }
            // confirm 激活时: 拦截方向键 + Enter/Esc, 不走正常输入
            if (terminal && terminal->m_confirmActive) {
                if (e == ftxui::Event::Return) {
                    terminal->m_confirmResult = true;
                    terminal->m_confirmActive = false;
                    terminal->m_confirmCv.notify_one();
                    return true;
                }
                if (e == ftxui::Event::Escape) {
                    terminal->m_confirmResult = false;
                    terminal->m_confirmActive = false;
                    terminal->m_confirmCv.notify_one();
                    return true;
                }
                if (e == ftxui::Event::ArrowLeft || e == ftxui::Event::ArrowRight) {
                    terminal->m_confirmSel = 1 - terminal->m_confirmSel;
                    return true;
                }
                return false;  // confirm 期间屏蔽其他按键
            }
            // Ctrl+D → 异步提交 (Enter 在 multiline 模式下插入换行)
            if (e == ftxui::Event::CtrlD && !inputText.empty() && !submitting) {
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
            // Shift+Tab → 切换模式
            if (e == ftxui::Event::TabReverse) {
                cycleMode();
                return true;
            }
            // Escape → 中断
            if (e == ftxui::Event::Escape) {
                if (terminal && terminal->m_interruptCb)
                    terminal->m_interruptCb();
                return true;
            }
            // Ctrl+C → 等效 Esc (中断, 不杀进程)
            if (e == ftxui::Event::CtrlC) {
                if (terminal && terminal->m_interruptCb)
                    terminal->m_interruptCb();
                return true;
            }
            // Ctrl+V → 粘贴剪贴板 (保留换行, multiline 模式正常显示)
            if (e == ftxui::Event::CtrlV) {
                std::string clip = readClipboard();
                if (!clip.empty()) inputText += clip;
                return true;
            }
            // Ctrl+Y → 复制全部内容到剪贴板 (保真换行, 比终端拖选准确)
            if (e == ftxui::Event::CtrlY) {
                std::string all;
                if (terminal) {
                    for (auto& line : terminal->m_contentBuffer)
                        all += line + "\n";
                }
                writeClipboard(all);
                return true;
            }
            return false;
        });

        // ---- 运行 (无 Modal 包裹, 确认栏在主 UI 底部) ----
        screen.Loop(submitHandler);
        submitting = true;
        if (submitThread.joinable()) submitThread.join();

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
    if (term) return term->confirm(prompt);  // FTXUI Modal 路径
    return false;  // 无 Terminal 时直接拒绝
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
    if (incomplete) {
        m_agent.saveSession(m_historyDir, true);
    } else {
        m_agent.saveSession(m_historyDir, false);
        CLFSessionManager::removeAllIncomplete(m_historyDir);
    }
}

} // namespace CLF::CLFUI
