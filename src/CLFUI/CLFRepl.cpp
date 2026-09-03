// CLFRepl.cpp — REPL 主循环 (FTXUI 全帧驱动)
// 快捷键处理: 第 1 批（核心输入输出）已实现
//
// 批次 A1（2026-09-03）：run() 两个巨型闭包已拆出——
//   Renderer  → CLFReplView（渲染/行映射/选区命中）
//   CatchEvent → CLFInputHandler（按键分发/粘贴/确认/选区/历史）
//   本文件瘦壳 = run 编排 + 组件装配 + 生命周期收尾（A1-5 取证划界）

#include "CLFUI/CLFRepl.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFUI/CLFAsyncSubmit.hpp"
#include "CLFUI/CLFClipboard.hpp"
#include "CLFUI/CLFCommandDispatcher.hpp"
#include "CLFUI/CLFInputHandler.hpp"
#include "CLFUI/CLFPasteCoalescer.hpp"
#include "CLFUI/CLFReplView.hpp"
#include "CLFUI/CLFSelectionModel.hpp"
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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>

#ifdef _WIN32
#include <windows.h>
#undef min
#undef max
#endif

namespace CLF::CLFUI {
using namespace CLF::CLFCore;

// ============================================================================
// 任务面板行构建（设计-任务清单UI显示 §4.2，2026-09-02）
// ============================================================================

std::vector<CLFTodoPanelLine> buildTodoPanelLines(
    const std::vector<CLFTodoItem>& todos, bool panelDone) {
    std::vector<CLFTodoPanelLine> lines;
    if (todos.empty() || panelDone) return lines;   // 入口检查（§4.2 第 1 条）

    size_t completed = 0;
    for (const auto& t : todos)
        if (t.m_status == "completed") ++completed;

    // 标题行：📋 任务清单 n/total（执行中形态；全完成收尾走 T6，面板随即清空）
    lines.push_back({
        "📋 任务清单 " + std::to_string(completed) + "/" + std::to_string(todos.size()),
        ftxui::Color::Default});

    // 逐项（>10 截断，§3.2 溢出处理）
    const size_t showCount = std::min<size_t>(todos.size(), 10);
    for (size_t i = 0; i < showCount; ++i) {
        const auto& t = todos[i];
        std::string icon;
        ftxui::Color color;
        if (t.m_status == "in_progress") {
            icon = "⏳"; color = ftxui::Color::CyanLight;
        } else if (t.m_status == "completed") {
            icon = "✓";  color = ftxui::Color::GreenLight;
        } else {
            icon = "○";  color = ftxui::Color::GrayDark;   // pending：灰显；未知状态按 pending 兜底
        }
        const std::string content = t.m_content.empty() ? "(无内容)" : t.m_content;
        lines.push_back({"   " + icon + " " + content, color});
    }
    if (todos.size() > 10) {
        lines.push_back({
            "   … 还有 " + std::to_string(todos.size() - 10) + " 项",
            ftxui::Color::GrayDark});
    }
    return lines;
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
    , m_passerby(m_output) {
    m_agent.setConfirmCallback(
        [this](const std::string& prompt) { return confirmDialog(prompt); });
    // J3: jsonl 会话文件目录注入（beginSessionFile 建文件用，设计 §3.9）
    m_agent.setHistoryDir(historyDir);
    // A5：Tips 行（默认 5s 轮播 / 300s 静默阈值；数据源 config/tips.txt + 内置兜底）
    m_tipsBar = std::make_unique<CLFTipsBar>(m_output);
}

CLFRepl::~CLFRepl() = default;

// ============================================================================
// run() — FTXUI 主循环（A1 瘦壳：编排 + 装配 + 生命周期收尾）
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
        CLFAsyncSubmit  asyncSubmit;
        // 粘贴合并器：窗满定时线程经 PostEvent(Custom) 唤醒主循环消费
        CLFPasteCoalescer pasteCoalescer(
            [&] { screen.PostEvent(ftxui::Event::Custom); });

        ftxui::InputOption inputOpt;
        inputOpt.multiline = true;  // 多行显示（Ctrl+N 换行后可见多行）
        // 光标须为引用型 Ref（指针构造）：拥有型 Ref(T t) 在 Input 内部是副本，
        // restore/历史导航对 *cursorPos 的直接赋值不同步到 Input，其光标停在旧位置
        // 导致后续粘贴字符插在 '\n' 之前、首两行渲染合并（验收实证，字节级定位）
        int cursorPosValue = 0;
        ftxui::Ref<int> cursorPos(&cursorPosValue);
        inputOpt.cursor_position = cursorPos;  // ↑/↓ 历史导航需要光标位置
        // 自定义渲染：去除聚焦时的背景色
        inputOpt.transform = [](ftxui::InputState state) {
            return state.element;
        };

        std::string inputText;
        auto input = ftxui::Input(&inputText, "❯ ", inputOpt);
        auto root  = ftxui::Container::Vertical({input});
        root->SetActiveChild(input);
        // 暂不调 TakeFocus(): FTXUI v7 可能内部发 DSR 查询光标位置，
        //   终端 CPR 响应 \033[1;1R 会与用户 ESC 的 \033 在 stdin 合并
        // input->TakeFocus();

        CLFConfirmBar confirmBar;

        // ---- 事件调试日志（取证用，按需开启 CLF_DEBUG_EVENTS=1） ----
        // 独立追加文件：CLFLogger 的 clf_agent.log 每会话覆盖，取证需跨会话保留
        const bool kDbgEvents = (std::getenv("CLF_DEBUG_EVENTS") != nullptr);
        auto dbgEvt = [&](const std::string& msg) {
            if (!kDbgEvents) return;
            std::ofstream f("doc/log/clf_events.log", std::ios::app);
            if (f) f << msg << std::endl;
        };
        // 控制字符转义（日志中区分 \n \r 等）
        auto escDbg = [](const std::string& s) {
            std::string o;
            for (char c : s) {
                if (c == '\n') o += "\\n";
                else if (c == '\r') o += "\\r";
                else if (c == '\033') o += "\\e";
                else o += c;
            }
            return o;
        };

        // ---- A1 拆分装配：Renderer → CLFReplView / CatchEvent → CLFInputHandler ----
        CLFReplView view(*this, terminal, inputText, input, confirmBar,
                         asyncSubmit, dbgEvt, escDbg);
        auto ui = ftxui::Renderer(root, [&] { return view.render(); });
        CLFInputHandler inputHandler(*this, terminal, view, inputText, cursorPos,
                                     input, &screen, asyncSubmit, pasteCoalescer,
                                     dbgEvt, escDbg);
        auto handler = ftxui::CatchEvent(ui, [&](ftxui::Event e) {
            return inputHandler.handle(e);
        });

        // ---- 运行 ----
#ifdef _WIN32
        // 关闭 ENABLE_PROCESSED_INPUT：FTXUI 设置控制台模式时未清除该位
        // （3rdparty app.cpp:617-624 只动 echo/line/VT/window 四位），该位开启时
        // 系统把 Ctrl+C 转成 CTRL_C_EVENT 信号，FTXUI 的 SIGINT 处理器直接退出
        // 主循环（RecordSignal→ExecuteSignalHandlers→Signal(SIGABRT)→ExitNow）——
        // 事件永远到不了应用层（验收实证：选区态 Ctrl+C 复制失效、应用直接退出，
        // 事件日志零 Ctrl+C 记录）。清除后 VT 输入模式下 Ctrl+C 以 0x03 字符事件
        // 到达 CatchEvent，旧"上下文感知分发"分支恢复生效。
        {
            HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
            DWORD mode = 0;
            if (hIn != INVALID_HANDLE_VALUE && GetConsoleMode(hIn, &mode))
                SetConsoleMode(hIn, mode & ~ENABLE_PROCESSED_INPUT);
        }
#endif
        screen.Loop(handler);
        asyncSubmit.join();
        if (terminal) terminal->setScreen(nullptr);

    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unexpected error: " << e.what()
                  << " — CLFCode terminated." << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "[Fatal] Unknown exception — CLFCode terminated." << std::endl;
        return 1;
    }
    return 0;
}

// ============================================================================
// submit / confirm / cycle / printBanner
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

    // 回显用户输入（渲染异常不应阻塞逻辑）
    try {
        if (m_output) {
            // P2-3: 用户消息行尾时间戳（跨日带日期；状态与发射点同驻 CLFRepl——R2）
            std::string tsDate = CLF::CLFCore::localDateStamp();
            bool withDate = (tsDate != m_lastTsDate);
            m_lastTsDate = tsDate;
            m_output->emitContent("> " + CLFTerminal::bold(input)
                                  + "  " + CLF::CLFCore::localTimeStamp(withDate) + "\n");
        }
    } catch (const std::exception& e) {
        CLFLogger::instance().warn(std::string("[Submit] echo failed: ") + e.what());
    }

    // 命令分发（/exit 须在此处执行 save，不能因渲染异常而跳过）
    bool handled = false;
    try {
        handled = m_dispatcher->handle(input);
    } catch (const std::exception& e) {
        CLFLogger::instance().error(std::string("[Submit] dispatcher exception: ") + e.what());
    }

    if (handled) {
        try {
            if (m_output) m_output->setStatus("");
        } catch (...) {}
        CLFLogger::instance().info("[Submit] handled by dispatcher");
        return;
    }

    // AI 对话处理
    try {
        if (m_output) m_output->emitContent("\n● " + CLFTerminal::cyan("CLFCode") + ":\n ");
    } catch (...) {}

    // J3/J4: 新回合清面板（resume 续写不清空，§八 补丁 4/5）
    if (m_agent.getResumedFrom().empty()) {
        m_agent.setTodoPanelDone(true);
    }
    // J4: 懒创建——第一条新对话输入建文件（resume 续写由 beginSessionFile 内部复制）
    if (m_agent.getActiveSessionFile().empty()) {
        m_agent.beginSessionFile(input);
    }

    try {
        std::string response = m_agent.runTurn(input);
        if (!response.empty() && response != "[Interrupted]") {
            try {
                if (m_output) m_output->emitContent(response + "\n");
            } catch (...) {}
        }
    } catch (const std::exception& e) {
        CLFLogger::instance().error(std::string("Fatal: ") + e.what());
        try {
            if (m_output) m_output->emitContent(
                CLFTerminal::red("✗ 异常: ") + e.what() + "\n");
        } catch (...) {}
        // F19: 异常路径无 return 点接线，Repl 侧兜底 Error
        if (m_output) m_output->setStatusKind(CLF::CLFTypes::ICLFOutput::StatusKind::Error);
        m_agent.clearContext();
    }

    // 会话节奏观察：每轮 AI 对话结束后检查（时间窗 + 轮数，进程内一次）
    m_passerby.onTurnFinished();

    auto st = m_agent.getLastToolStats();
    // J4: 轮末 turn 行追加（替换覆盖式 saveSession(false)）——jsonl 追加式保存
    m_agent.appendTurnLine();
    try {
        if (m_output) m_output->setStatus("");
        if (m_output) m_output->emitContent("\n");
    } catch (...) {}
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

} // namespace CLF::CLFUI
