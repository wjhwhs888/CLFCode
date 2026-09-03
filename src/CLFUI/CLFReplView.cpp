// CLFReplView.cpp — REPL 渲染器实现（批次 A1 纯搬移：原 CLFRepl::run() 内
// Renderer 闭包 178-484 + hitTest lambda 486-513 + CPR/ANSI 剥离两处同构收敛）
// ⚠ 纯搬移批次纪律：逐行搬移不改行为；成员状态仍驻留 CLFRepl（friend 访问）

#include "CLFUI/CLFReplView.hpp"

#include "CLFUI/CLFRepl.hpp"
#include "CLFUI/CLFAsyncSubmit.hpp"
#include "CLFUI/CLFCommandDispatcher.hpp"
#include "CLFUI/CLFConfirmBar.hpp"
#include "CLFUI/CLFSelectionModel.hpp"
#include "CLFUI/CLFTerminal.hpp"
#include "CLFUI/CLFTipsBar.hpp"
#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"

#include <filesystem>

namespace CLF::CLFUI {
using namespace CLF::CLFCore;

// CPR/ANSI 残留剥离（A1 抽取：两处逐字同构）
std::string& stripCprResidual(std::string& inputText) {
    // 终端 CPR \033[n;mR 的 \033 被 CatchEvent 吃掉后，[n;mR 作为
    // Character 事件到达。统一剥离。
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
    return inputText;
}

CLFReplView::CLFReplView(CLFRepl& repl, CLFTerminal* terminal, std::string& inputText,
                         ftxui::Component input, CLFConfirmBar& confirmBar,
                         CLFAsyncSubmit& asyncSubmit,
                         std::function<void(const std::string&)> dbgEvt,
                         std::function<std::string(const std::string&)> escDbg)
    : m_repl(repl)
    , m_terminal(terminal)
    , m_inputText(inputText)
    , m_input(std::move(input))
    , m_confirmBar(confirmBar)
    , m_asyncSubmit(asyncSubmit)
    , m_dbgEvt(std::move(dbgEvt))
    , m_escDbg(std::move(escDbg)) {
}

ftxui::Element CLFReplView::render() {
    // ---- 原闭包捕获 → 局部别名（保持搬移体逐行可对照）----
    auto* terminal = m_terminal;
    auto& inputText = m_inputText;
    auto& scrollView = m_scrollView;
    auto& dbgEvt = m_dbgEvt;
    auto& escDbg = m_escDbg;
    auto& m_lastSnapshot = m_repl.m_lastSnapshot;
    auto& m_selection = m_repl.m_selection;
    auto& m_lastRowMap = m_repl.m_lastRowMap;
    auto& m_lastRowTexts = m_repl.m_lastRowTexts;
    auto& m_lastRowStyles = m_repl.m_lastRowStyles;
    auto& m_showThinking = m_repl.m_showThinking;
    auto& m_foldJustToggled = m_repl.m_foldJustToggled;
    auto& m_needRestoreInput = m_repl.m_needRestoreInput;
    auto& m_lastSubmittedInput = m_repl.m_lastSubmittedInput;
    auto& m_agent = m_repl.m_agent;
    auto& m_dispatcher = m_repl.m_dispatcher;

    if (terminal) terminal->m_refreshPending = false;

    // 每帧剥离 CPR / ANSI 残留（\033 被 CatchEvent 吃掉后残留 [n;mR）
    // 不能用 m_justInterrupted 单帧判断（CPR 字节可能在渲染后到达）
    stripCprResidual(inputText);
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
        // u8path 构造：getWorkingDir 返回 UTF-8，窄字符 path 构造会按
        // ANSI 代码页（CP936）转宽字符 → 中文目录名乱码（v0.4.2 修复）
        ftxui::text(" 📁 " + std::filesystem::u8path(
            CLFConfigLoader::getWorkingDir()).filename().u8string())
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

    if (dbgEvt)
        dbgEvt("Render input=" + std::to_string(inputText.size())
               + " rows=" + std::to_string(m_lastRowTexts.size()));

    // T4: 任务面板（contentArea 与 progressElements 之间，§3.1）。
    // getTodos 锁内副本 + isTodoPanelDone 原子——渲染线程安全（§3.9）；
    // 空清单/已收尾 → 空 Element 零占用
    ftxui::Elements todoPanelElements;
    for (const auto& l : buildTodoPanelLines(
             m_agent.getTodos(), m_agent.isTodoPanelDone())) {
        todoPanelElements.push_back(
            ftxui::text(l.text) | ftxui::color(l.color));
    }
    auto todoPanel = ftxui::vbox(std::move(todoPanelElements));

    return ftxui::vbox({
        contentArea,
        todoPanel,
        ftxui::vbox(std::move(progressElements)),
        statusLine,
        // A5：Tips 行——仅请求处理中显示（busy），空闲自动空 Element；
        // 原子读 busy/ticker 状态，渲染线程安全（设计 §3.4）
        m_repl.m_tipsBar ? m_repl.m_tipsBar->render(m_asyncSubmit.busy())
                         : ftxui::emptyElement(),
        thinSep(),
        m_input->Render(),
        thinSep(),
        modeLine,
        m_confirmBar.render(*terminal),
    });
}

std::optional<std::tuple<int, int, int>> CLFReplView::hitTest(int x, int y) {
    // 原 hitTest lambda 486-513 搬移
    auto& m_lastRowMap = m_repl.m_lastRowMap;
    auto& m_lastRowTexts = m_repl.m_lastRowTexts;
    auto& scrollView = m_scrollView;
    auto& dbgEvt = m_dbgEvt;
    auto& escDbg = m_escDbg;

    if (m_lastRowMap.empty()) return std::nullopt;
    auto [vs, ve] = scrollView.visibleRange();
    if (ve <= vs) return std::nullopt;
    int gRow = vs + y - scrollView.topHintCount();
    if (gRow < vs) gRow = vs;  // 顶提示行 → 首行
    if (gRow >= ve || gRow >= static_cast<int>(m_lastRowTexts.size()))
        return std::nullopt;   // 内容区以下 → 放行
    const std::string& rowText = m_lastRowTexts[gRow];
    size_t bStart = CLFSelectionModel::colToByte(rowText, x);
    size_t bEnd   = CLFSelectionModel::colToByteEnd(rowText, x);
    if (dbgEvt)
        dbgEvt("  hit vs=" + std::to_string(vs) + " ve=" + std::to_string(ve)
               + " hints=" + std::to_string(scrollView.topHintCount())
               + " -> row=" + std::to_string(gRow)
               + " b0=" + std::to_string(bStart)
               + " b1=" + std::to_string(bEnd)
               + " text='" + escDbg(rowText) + "'");
    return std::make_tuple(gRow, static_cast<int>(bStart),
                           static_cast<int>(bEnd));
}

} // namespace CLF::CLFUI
