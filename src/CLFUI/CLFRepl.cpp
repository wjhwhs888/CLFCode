// CLFRepl.cpp — REPL 主循环 (FTXUI 全帧驱动)
// 快捷键处理: 第 1 批（核心输入输出）已实现

#include "CLFUI/CLFRepl.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFUI/CLFAsyncSubmit.hpp"
#include "CLFUI/CLFClipboard.hpp"
#include "CLFUI/CLFCommandDispatcher.hpp"
#include "CLFUI/CLFPasteCoalescer.hpp"
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
#include <iostream>
#include <mutex>
#include <optional>

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
        // 粘贴合并器：窗满定时线程经 PostEvent(Custom) 唤醒主循环消费
        CLFPasteCoalescer pasteCoalescer(
            [&] { screen.PostEvent(ftxui::Event::Custom); });

        ftxui::InputOption inputOpt;
        inputOpt.multiline = true;  // 多行显示（Ctrl+N 换行后可见多行）
        ftxui::Ref<int> cursorPos = 0;
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

        // ---- 事件调试日志（CLF_DEBUG_EVENTS=1 时开启，诊断用） ----
        const bool kDbgEvents = (std::getenv("CLF_DEBUG_EVENTS") != nullptr);
        auto dbgEvt = [&](const std::string& msg) {
            if (kDbgEvents) CLFLogger::instance().info("[EvtDbg] " + msg);
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

            m_lastSnapshot = terminal ? terminal->contentSnapshot()
                                      : CLFTerminal::ContentSnapshot{};
            const auto& snap = m_lastSnapshot;

            // confirm 激活即清选区（confirm 分支以外的事件通道外兜底）
            if (terminal && terminal->isConfirmActive())
                m_selection.clear();

            // 获取终端宽度用于硬换行（避免长行超出视口）
            int termW = CLFTerminal::getTerminalWidth();
            int wrapW = (termW > 20) ? termW : 78;

            // 显示宽度工具统一走 CLFSelectionModel（渲染硬换行 / 选区换算 / 高亮拆分共用）
            using Sel = CLFSelectionModel;

            // ---- 行映射表 + 行文本 + 行样式（选区坐标与提取的依据，每帧重建） ----
            // 行样式码：0=无 1=绿(Add) 2=红(Remove) 3=dim
            m_lastRowMap.clear();
            m_lastRowTexts.clear();
            m_lastRowStyles.clear();
            auto addRow = [&](std::string text, RowKind kind, size_t lineIdx,
                              size_t partIdx, int style) {
                m_lastRowMap.push_back(RowInfo{kind, lineIdx, partIdx});
                m_lastRowTexts.push_back(std::move(text));
                m_lastRowStyles.push_back(style);
            };

            const bool hasStyles = (snap.lineStyles.size() == snap.lines.size());

            for (size_t i = 0; i < snap.lines.size(); ++i) {
                const auto& l = snap.lines[i];
                int style = 0;
                if (hasStyles && i < snap.lineStyles.size()) {
                    auto s = static_cast<CLF::CLFTypes::ICLFOutput::LineStyle>(
                        snap.lineStyles[i]);
                    style = (s == CLF::CLFTypes::ICLFOutput::LineStyle::Add) ? 1
                          : (s == CLF::CLFTypes::ICLFOutput::LineStyle::Remove) ? 2
                          : (s == CLF::CLFTypes::ICLFOutput::LineStyle::Context) ? 3 : 0;
                }
                int lineW = Sel::displayWidth(l);
                if (wrapW > 0 && lineW > wrapW) {
                    // CJK 感知硬换行：每个 part 是独立的渲染行（选区行映射的基本单元）
                    std::string remaining = l;
                    size_t partIdx = 0;
                    while (!remaining.empty()) {
                        std::string part = Sel::substrByWidth(remaining, wrapW);
                        if (part.empty()) part = remaining.substr(0, 1); // fallback
                        addRow(part, RowKind::Content, i, partIdx++, style);
                        remaining = remaining.substr(part.size());
                    }
                } else {
                    addRow(l, RowKind::Content, i, 0, style);
                }
            }
            if (!snap.pendingLine.empty()) {
                const auto& pl = snap.pendingLine;
                if (wrapW > 0) {
                    size_t partIdx = 0;
                    for (size_t pos = 0; pos < pl.size(); pos += wrapW)
                        addRow(pl.substr(pos, wrapW), RowKind::Pending, 0, partIdx++, 0);
                } else {
                    addRow(pl, RowKind::Pending, 0, 0, 0);
                }
            }

            // ---- 思考过程（内容区最后，Ctrl+T 折叠/展开） ----
            if (snap.thinkingActive || !snap.thinkingLines.empty()) {
                // P1-3: 折叠行摘要——running 取实时尾行，完成取首行（UTF-8 边界截断）
                auto truncateUtf8 = [&](const std::string& s, size_t maxBytes) -> std::string {
                    if (s.size() <= maxBytes) return s;
                    size_t cut = maxBytes;
                    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
                    return s.substr(0, cut) + "…";
                };
                std::string fold = "  Thought for " + std::to_string(snap.thinkingElapsed) + "s";
                if (!snap.thinkingLines.empty()) {
                    const std::string& src = snap.thinkingActive
                        ? snap.thinkingLines.back() : snap.thinkingLines.front();
                    fold += " · " + truncateUtf8(src, 80);
                }
                fold += " (ctrl+t 展开)";
                addRow(fold, RowKind::ThinkingFold, 0, 0, 3);
                if (!snap.thinkingActive && m_showThinking && !snap.thinkingLines.empty()) {
                    for (size_t i = 0; i < snap.thinkingLines.size(); ++i)
                        addRow("  " + snap.thinkingLines[i], RowKind::ThinkingLine, i, 0, 3);
                }
            }

            // ---- P2-1: 恢复回显折叠块（滚动区末尾） ----
            size_t foldLineIdx = 0;
            if (!snap.foldedSummary.empty()) {
                foldLineIdx = m_lastRowMap.size();
                std::string marker = snap.foldedExpanded ? "▾" : "▸";
                addRow("  " + marker + " " + snap.foldedSummary,
                       RowKind::FoldSummary, 0, 0, 3);
                if (snap.foldedExpanded) {
                    for (size_t i = 0; i < snap.foldedLines.size(); ++i) {
                        // 与主内容一致的 CJK 感知硬换行
                        std::string remaining = "  " + snap.foldedLines[i];
                        int lw = Sel::displayWidth(remaining);
                        if (wrapW > 0 && lw > wrapW) {
                            size_t partIdx = 0;
                            while (!remaining.empty()) {
                                std::string part = Sel::substrByWidth(remaining, wrapW);
                                if (part.empty()) part = remaining.substr(0, 1);
                                addRow(part, RowKind::FoldLine, i, partIdx++, 3);
                                remaining = remaining.substr(part.size());
                            }
                        } else {
                            addRow(remaining, RowKind::FoldLine, i, 0, 3);
                        }
                    }
                }
            }

            // ---- 元素构建（含选区高亮：切片前按全局行号应用，切片保留元素） ----
            ftxui::Elements allLines;
            allLines.reserve(m_lastRowTexts.size());
            auto decorate = [](int style, ftxui::Element el) -> ftxui::Element {
                if (style == 1) return el | ftxui::color(ftxui::Color::Green);
                if (style == 2) return el | ftxui::color(ftxui::Color::Red);
                if (style == 3) return ftxui::dim(el);
                return el;
            };
            for (size_t i = 0; i < m_lastRowTexts.size(); ++i) {
                const std::string& rowText = m_lastRowTexts[i];
                auto sel = m_selection.rowSelection(static_cast<int>(i), rowText.size());
                if (!sel) {
                    allLines.push_back(decorate(m_lastRowStyles[i], ftxui::text(rowText)));
                    continue;
                }
                // 三段拆分：选中段加 bgcolor，行级样式（diff 色/dim）整行保留
                size_t a = sel->first, b = sel->second;
                ftxui::Elements segs;
                if (a > 0) segs.push_back(ftxui::text(rowText.substr(0, a)));
                segs.push_back(ftxui::text(rowText.substr(a, b - a))
                               | ftxui::bgcolor(ftxui::Color::Grey30));
                if (b < rowText.size()) segs.push_back(ftxui::text(rowText.substr(b)));
                allLines.push_back(decorate(m_lastRowStyles[i], ftxui::hbox(std::move(segs))));
            }

            scrollView.update(static_cast<int>(allLines.size()),
                              CLFTerminal::getTerminalHeight(), 7);
            // R5: 折叠/展开切换后保持折叠行可见（防顶出视口）
            if (m_foldJustToggled) {
                m_foldJustToggled = false;
                if (!snap.foldedSummary.empty())
                    scrollView.keepLineVisible(static_cast<int>(foldLineIdx));
            }
            auto contentArea = ftxui::vbox(scrollView.renderWindow(allLines)) | ftxui::flex;

            // ---- 渐进式进度块（插在内容区和 statusLine 之间） ----
            // D4: 动画帧按时间差推进（事件驱动，无定时器线程——流静止则动画静止）
            // P1-1: 帧计算上提，进度块与 running 状态点共用
            static const char* kSpinFrames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
            auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            size_t frame = static_cast<size_t>(nowMs / 100) % 10;
            ftxui::Elements progressElements;
            if (!snap.progressLines.empty()) {
                for (size_t pi = 0; pi < snap.progressLines.size(); ++pi) {
                    const auto& pl = snap.progressLines[pi];
                    if (pl.empty()) continue;
                    std::string line = pl;
                    // 仅末行附加动画帧（P0-4 单行进度的执行中态）
                    if (pi == snap.progressLines.size() - 1)
                        line += " " + std::string(kSpinFrames[frame]);
                    progressElements.push_back(ftxui::text(line));
                }
            }

            // P1-1: 状态点 + 文本（渲染条件：文本非空 || kind != None，F5）
            ftxui::Element statusLine = ftxui::emptyElement();
            if (!snap.statusText.empty()
                || snap.statusKind != CLF::CLFTypes::ICLFOutput::StatusKind::None) {
                ftxui::Elements statusParts;
                switch (snap.statusKind) {
                case CLF::CLFTypes::ICLFOutput::StatusKind::Running:
                    statusParts.push_back(ftxui::text("  " + std::string(kSpinFrames[frame]))
                                          | ftxui::color(ftxui::Color::CyanLight));
                    break;
                case CLF::CLFTypes::ICLFOutput::StatusKind::Done:
                    statusParts.push_back(ftxui::text("  ●")
                                          | ftxui::color(ftxui::Color::GreenLight));
                    break;
                case CLF::CLFTypes::ICLFOutput::StatusKind::Warn:
                    statusParts.push_back(ftxui::text("  ●")
                                          | ftxui::color(ftxui::Color::Orange1));
                    break;
                case CLF::CLFTypes::ICLFOutput::StatusKind::Error:
                    statusParts.push_back(ftxui::text("  ✕")
                                          | ftxui::color(ftxui::Color::RedLight));
                    break;
                default:
                    break;
                }
                if (!snap.statusText.empty())
                    statusParts.push_back(ftxui::dim(ftxui::text(snap.statusText)));
                statusLine = ftxui::hbox(std::move(statusParts));
            }

            // 状态栏：模型名 │ 目录 │ 安全模式 │ 快捷键
            // 模式 → 颜色映射
            auto modeColor = [&]() -> ftxui::Color {
                std::string mode = m_dispatcher->modeName();
                if (mode == "auto")    return ftxui::Color::GreenLight;
                if (mode == "analyze") return ftxui::Color::Magenta;  // D1: 蓝让给 running
                if (mode == "edit")    return ftxui::Color::Orange1;
                if (mode == "manual")  return ftxui::Color::GrayDark;
                return ftxui::Color::GrayDark;
            };
            auto sep = [] {
                return ftxui::separatorCharacter("│")
                     | ftxui::color(ftxui::Color::GrayDark);
            };
            auto modeLine = ftxui::hbox({
                ftxui::text("  ")
                  | ftxui::color(ftxui::Color::RedLight),  // 缩进不算
                ftxui::text(m_agent.getConfig().m_modelName)
                  | ftxui::color(ftxui::Color::RedLight)
                  | ftxui::bold,
                sep(),
                ftxui::text(" 📁 " + std::filesystem::path(
                    CLFConfigLoader::getWorkingDir()).filename().string())
                  | ftxui::color(ftxui::Color::GreenLight),
                sep(),
                ftxui::text(" 🔒 " + m_dispatcher->modeName())
                  | ftxui::color(modeColor()),
                ftxui::text("  Shift+Tab 切换  ")
                  | ftxui::dim,
                ftxui::filler(),
                ftxui::text("/help 帮助  ")
                  | ftxui::dim,
            });

            auto thinSep = [] {
                return ftxui::separatorLight()
                     | ftxui::color(ftxui::Color::CyanLight);
            };

            if (kDbgEvents)
                dbgEvt("Render input=" + std::to_string(inputText.size())
                       + " rows=" + std::to_string(m_lastRowTexts.size()));

            return ftxui::vbox({
                contentArea,
                ftxui::vbox(std::move(progressElements)),
                statusLine,
                thinSep(),
                input->Render(),
                thinSep(),
                modeLine,
                confirmBar.render(*terminal),
            });
        });

        // ---- 鼠标坐标 → (全局渲染行, 行内字节偏移) ----
        // x/y 为 SGR 1 基坐标；提示行点击 clamp 到最近内容行
        auto hitTest = [&](int x, int y) -> std::optional<std::pair<int, int>> {
            if (m_lastRowMap.empty()) return std::nullopt;
            auto [vs, ve] = scrollView.visibleRange();
            if (ve <= vs) return std::nullopt;
            int gRow = vs + (y - 1) - scrollView.topHintCount();
            gRow = std::max(vs, std::min(gRow, ve - 1));
            if (gRow >= static_cast<int>(m_lastRowTexts.size())) return std::nullopt;
            size_t byteOff = CLFSelectionModel::colToByte(m_lastRowTexts[gRow], x - 1);
            if (kDbgEvents)
                dbgEvt("  hit vs=" + std::to_string(vs) + " ve=" + std::to_string(ve)
                       + " hints=" + std::to_string(scrollView.topHintCount())
                       + " -> row=" + std::to_string(gRow)
                       + " byte=" + std::to_string(byteOff)
                       + " text='" + escDbg(m_lastRowTexts[gRow]) + "'");
            return std::make_pair(gRow, static_cast<int>(byteOff));
        };

        // ---- CatchEvent: 快捷键处理 ----
        auto handler = ftxui::CatchEvent(ui, [&](ftxui::Event e) {

            if (kDbgEvents) {
                std::string kind = e.is_character()
                    ? ("Char '" + escDbg(e.character()) + "'")
                    : (e == ftxui::Event::Return ? "Return"
                       : e.is_mouse() ? ("Mouse btn="
                            + std::to_string(static_cast<int>(e.mouse().button))
                            + " mot=" + std::to_string(static_cast<int>(e.mouse().motion))
                            + " x=" + std::to_string(e.mouse().x)
                            + " y=" + std::to_string(e.mouse().y))
                       : (e == ftxui::Event::CtrlC ? "CtrlC"
                          : e == ftxui::Event::CtrlS ? "CtrlS"
                          : "Other"));
                dbgEvt(kind + " sel=" + (m_selection.active() ? "1" : "0")
                       + " input=" + std::to_string(inputText.size()));
            }

            // === 0a. 提交主体（合并器确认路径与 Ctrl+D 共用，:414 原逻辑） ===
            auto doSubmit = [&] {
                if (!inputText.empty() && !asyncSubmit.busy()) {
                    m_lastSubmittedInput = inputText;
                    m_inputHistory.push_back(inputText);
                    m_historyIndex = -1;
                    auto text = inputText;
                    inputText.clear();
                    asyncSubmit.launch([this, text]() { submit(text); });
                }
            };

            // === 0b. 粘贴合并器窗满确认消费（任何事件到达时检查，幂等） ===
            if (pasteCoalescer.pendingConfirmed()) {
                bool shouldSubmit = pasteCoalescer.consumePendingConfirmation();
                // confirm 激活或 busy 时只复位不提交（文本留在输入框）
                if (shouldSubmit && !(terminal && terminal->isConfirmActive()))
                    doSubmit();
            }

            // === 1. 确认栏激活时（最小化处理，防卡死）===
            if (terminal && terminal->isConfirmActive()) {
                // confirm 激活期内取消任何待提交（定时线程可能已确认，见 0b）
                pasteCoalescer.onOtherEvent(std::chrono::steady_clock::now());
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

            // === 1.4 选区态事件接管（置于合并器路由之前：Enter=复制优先于 Return 路由） ===
            if (m_selection.active()) {
                if (e == ftxui::Event::Escape) { m_selection.clear(); return true; }
                // Ctrl+S 激活时忽略：按住会触发键盘自动重复，toggle 语义导致
                // 高亮闪烁 + 可能停在"激活"态吞掉后续输入（退出统一 Esc/Enter/Ctrl+C）
                if (e == ftxui::Event::CtrlS)  { return true; }
                if (e == ftxui::Event::Return || e == ftxui::Event::CtrlC) {
                    // 提取 → 写剪贴板 → 退出（选区态 Ctrl+C = 复制，不触发中断）
                    auto r = m_selection.range();
                    if (r.fromRow >= 0) {
                        std::string out = CLFSelectionModel::extract(
                            r, m_lastRowMap, m_lastRowTexts);
                        CLFClipboard::write(out);
                        if (kDbgEvents)
                            dbgEvt("  copy sel=[" + std::to_string(r.fromRow)
                                   + "," + std::to_string(r.toRow) + "] out='"
                                   + escDbg(out) + "'");
                    }
                    m_selection.clear();
                    return true;
                }
                if (e == ftxui::Event::ArrowUp || e == ftxui::Event::ArrowDown
                    || e == ftxui::Event::ArrowLeft || e == ftxui::Event::ArrowRight) {
                    auto d = e == ftxui::Event::ArrowUp   ? CLFSelectionModel::Dir::Up
                           : e == ftxui::Event::ArrowDown ? CLFSelectionModel::Dir::Down
                           : e == ftxui::Event::ArrowLeft ? CLFSelectionModel::Dir::Left
                                                          : CLFSelectionModel::Dir::Right;
                    m_selection.moveCursor(d, m_lastRowMap, m_lastRowTexts);
                    return true;
                }
                if (e == ftxui::Event::Home || e == ftxui::Event::End) {
                    m_selection.moveCursor(e == ftxui::Event::Home
                                               ? CLFSelectionModel::Dir::Home
                                               : CLFSelectionModel::Dir::End,
                                           m_lastRowMap, m_lastRowTexts);
                    return true;
                }
                if (e == ftxui::Event::PageUp || e == ftxui::Event::PageDown) {
                    auto [vs, ve] = scrollView.visibleRange();
                    if (ve > vs && ve <= static_cast<int>(m_lastRowTexts.size())) {
                        if (e == ftxui::Event::PageUp) m_selection.extendTo(vs, 0);
                        else m_selection.extendTo(ve - 1,
                            static_cast<int>(m_lastRowTexts[ve - 1].size()));
                    }
                    return true;
                }
                if (e.is_mouse()) {
                    auto& m = e.mouse();
                    if (m.button == ftxui::Mouse::WheelUp
                        || m.button == ftxui::Mouse::WheelDown)
                        return false;  // 滚轮放行到 :542 滚动处理
                    if (m.button == ftxui::Mouse::Left) {
                        if (m.motion == ftxui::Mouse::Released) {
                            // 单击（无移动，anchor==cursor）→ 取消选区
                            if (m_selection.empty()) m_selection.clear();
                            return true;
                        }
                        // Pressed / Moved → 扩展选区
                        if (auto hit = hitTest(m.x, m.y))
                            m_selection.extendTo(hit->first, hit->second);
                        return true;
                    }
                }
                return true;  // 选区态其他事件消费
            }

            // === 1.45 选区进入（鼠标左键按下 / Ctrl+S 键盘选区） ===
            if (e.is_mouse()) {
                auto& m = e.mouse();
                if (m.button == ftxui::Mouse::Left && m.motion == ftxui::Mouse::Pressed) {
                    // 内容区按下 → 进入选区；非内容区（输入框等）放行给 Input
                    if (auto hit = hitTest(m.x, m.y)) {
                        m_selection.startAt(hit->first, hit->second);
                        return true;
                    }
                    return false;
                }
            }
            if (e == ftxui::Event::CtrlS) {
                // 进入选区：锚点=游标=可见窗口首个内容行行首（空选区起点，
                // 方向键/拖拽扩展——验收反馈：全窗预选体验不对）
                auto [vs, ve] = scrollView.visibleRange();
                if (ve > vs && ve <= static_cast<int>(m_lastRowTexts.size()))
                    m_selection.startAt(vs, 0);
                return true;
            }

            // === 1.5 粘贴合并器事件路由（Return/字符/其他三类） ===
            {
                auto now = std::chrono::steady_clock::now();
                if (e == ftxui::Event::Return) {
                    auto act = pasteCoalescer.onReturn(now, inputText);
                    if (kDbgEvents)
                        dbgEvt("  onReturn -> " + std::to_string(static_cast<int>(act))
                               + " input=" + std::to_string(inputText.size()));
                    switch (act) {
                    case CLFPasteCoalescer::Action::Consume:
                        return true;  // 待提交已捕获 / 空文本短路
                    case CLFPasteCoalescer::Action::RestoreAndAppendNewline:
                        inputText = pasteCoalescer.pendingText() + "\n\n";
                        *cursorPos = static_cast<int>(inputText.size());
                        return true;
                    case CLFPasteCoalescer::Action::InsertNewline:
                        input->OnEvent(ftxui::Event::Character("\n"));
                        return true;
                    default:
                        break;  // PassThrough 不会出现在 Return 路径
                    }
                } else if (e.is_character()) {
                    auto act = pasteCoalescer.onCharacter(now);
                    if (kDbgEvents)
                        dbgEvt("  onChar -> " + std::to_string(static_cast<int>(act))
                               + " input=" + std::to_string(inputText.size()));
                    if (act == CLFPasteCoalescer::Action::RestoreAndAppendChar) {
                        inputText = pasteCoalescer.pendingText() + "\n" + e.character();
                        *cursorPos = static_cast<int>(inputText.size());
                        if (kDbgEvents)
                            dbgEvt("  restored input='" + escDbg(inputText) + "'");
                        return true;
                    }
                    // PassThrough → 放行给 Input
                } else {
                    pasteCoalescer.onOtherEvent(now);  // PENDING 取消 / PASTE_MODE 退出
                }
            }

            // === 2. 提交（Ctrl+D 立即提交；Return 已由 1.5 路由处理） ===
            if (e == ftxui::Event::CtrlD) {
                doSubmit();
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

            // Ctrl+R: 恢复回显折叠块展开/收起（P2-1）
            if (e == ftxui::Event::CtrlR) {
                if (terminal) {
                    terminal->toggleFoldedBlock();
                    m_foldJustToggled = true;
                }
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
                if (kDbgEvents)
                    dbgEvt("  CtrlC old-branch busy="
                           + std::string(asyncSubmit.busy() ? "1" : "0")
                           + " sel=" + (m_selection.active() ? "1" : "0"));
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

    auto st = m_agent.getLastToolStats();
    saveSession(false);  // 每轮都存 latest.json
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

void CLFRepl::saveSession(bool isFinal) {
    m_agent.saveSession(m_historyDir, isFinal);
}

} // namespace CLF::CLFUI
