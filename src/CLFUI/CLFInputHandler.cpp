// CLFInputHandler.cpp — REPL 事件处理器实现（批次 A1 纯搬移：原
// CLFRepl::run() 内 CatchEvent 闭包 516-855）
// ⚠ 纯搬移批次纪律：逐行搬移不改行为（含每分支事件消费返回值）；
// 成员状态仍驻留 CLFRepl（friend 访问）

#include "CLFUI/CLFInputHandler.hpp"

#include "CLFUI/CLFRepl.hpp"
#include "CLFUI/CLFReplView.hpp"
#include "CLFUI/CLFAsyncSubmit.hpp"
#include "CLFUI/CLFClipboard.hpp"
#include "CLFUI/CLFCommandDispatcher.hpp"
#include "CLFUI/CLFPasteCoalescer.hpp"
#include "CLFUI/CLFSelectionModel.hpp"
#include "CLFUI/CLFTerminal.hpp"

#include <chrono>
#include <string_view>

namespace CLF::CLFUI {

CLFInputHandler::CLFInputHandler(CLFRepl& repl, CLFTerminal* terminal, CLFReplView& view,
                                 std::string& inputText, ftxui::Ref<int> cursorPos,
                                 ftxui::Component input, ftxui::ScreenInteractive* screen,
                                 CLFAsyncSubmit& asyncSubmit, CLFPasteCoalescer& pasteCoalescer,
                                 std::function<void(const std::string&)> dbgEvt,
                                 std::function<std::string(const std::string&)> escDbg)
    : m_repl(repl)
    , m_terminal(terminal)
    , m_view(view)
    , m_inputText(inputText)
    , m_cursorPos(cursorPos)
    , m_input(std::move(input))
    , m_screen(screen)
    , m_asyncSubmit(asyncSubmit)
    , m_pasteCoalescer(pasteCoalescer)
    , m_dbgEvt(std::move(dbgEvt))
    , m_escDbg(std::move(escDbg)) {
}

bool CLFInputHandler::handle(ftxui::Event e) {
    // ---- 原闭包捕获 → 局部别名（保持搬移体逐行可对照）----
    auto* terminal = m_terminal;
    auto& inputText = m_inputText;
    auto& cursorPos = m_cursorPos;
    auto& input = m_input;
    auto& screen = m_screen;
    auto& asyncSubmit = m_asyncSubmit;
    auto& pasteCoalescer = m_pasteCoalescer;
    auto& dbgEvt = m_dbgEvt;
    auto& escDbg = m_escDbg;
    auto& m_selection = m_repl.m_selection;
    auto& m_lastRowMap = m_repl.m_lastRowMap;
    auto& m_lastRowTexts = m_repl.m_lastRowTexts;
    auto& m_inputHistory = m_repl.m_inputHistory;
    auto& m_historyIndex = m_repl.m_historyIndex;
    auto& m_historyDraft = m_repl.m_historyDraft;
    auto& m_showThinking = m_repl.m_showThinking;
    auto& m_foldJustToggled = m_repl.m_foldJustToggled;
    auto& m_needRestoreInput = m_repl.m_needRestoreInput;
    auto& m_lastSubmittedInput = m_repl.m_lastSubmittedInput;
    auto& m_lastEscTime = m_repl.m_lastEscTime;
    auto& m_interruptCooldownUntil = m_repl.m_interruptCooldownUntil;  // B2 (b) 冷却
    auto& m_escCleanupFrames = m_repl.m_escCleanupFrames;
    auto& m_justInterrupted = m_repl.m_justInterrupted;
    auto& m_dispatcher = m_repl.m_dispatcher;

    // B3（2026-09-23）：中断 UI 收尾统一——ESC 与 Ctrl+C 共用同一套
    // （状态行"⏹ 中断中…"+hold / 输入框恢复守卫 / CPR 残留剥离 / 冷却）
    auto doInterruptCleanup = [&](bool wasBusy) {
        if (wasBusy) {
            m_interruptCooldownUntil = std::chrono::steady_clock::now()
                                       + std::chrono::milliseconds(800);  // B2 (b)
        }
        m_justInterrupted = true;
        // B4 修复（2026-09-23）：渲染层守卫（CLFReplView 仅空输入框回填）
        // 不再被架空——正在输入的内容保留，仅空框时置恢复位
        if (inputText.empty()) {
            m_needRestoreInput = true;
        }
        m_escCleanupFrames = 3;
        if (terminal) {
            terminal->setStatus("⏹ 中断中…");
            terminal->setStatusHold(true);   // W6：hold 期间 turnTimer 覆盖被忽略
        }
        screen->PostEvent(ftxui::Event::Custom);
    };

    if (dbgEvt) {
        std::string kind = e.is_character()
            ? ("Char '" + escDbg(e.character()) + "'")
            : (e == ftxui::Event::Return ? "Return"
               : e.is_mouse() ? ("Mouse btn="
                    + std::to_string(static_cast<int>(e.mouse().button))
                    + " mot=" + std::to_string(static_cast<int>(e.mouse().motion))
                    + " x=" + std::to_string(e.mouse().x)
                    + " y=" + std::to_string(e.mouse().y))
               : (e == ftxui::Event::CtrlC ? "CtrlC"
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
            // 2.2c 修根（2026-09-21 用户实抓"空闲 unload 被拒"）：命令输入
            // **不 launch**——UI 线程同步处理。launch 会置 m_submitting=true，
            // 异步线程内 dispatcher 处理命令时，/plugin 的 quiesce 判定读到
            // "自己这个提交" → 空闲自拒。命令处理是轻量同步（dispatcher +
            // 输出），不阻塞 UI
            if (!text.empty() && text[0] == '/') {
                m_repl.submit(text);
            } else {
                asyncSubmit.launch([&repl = m_repl, text]() { repl.submit(text); });
            }
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
        // "返回"/ESC/CtrlC 统一行为: 拒绝 + 中断 Agent，回到输入编辑。
        // C4：结果写入/唤醒经 CLFTerminal::submitConfirm 收敛（锁序保真）
        auto cancelWithInterrupt = [&] {
            terminal->interruptFromUi();    // 先中断再唤醒 worker
            terminal->submitConfirm(false);
        };

        if (e == ftxui::Event::Return) {
            if (terminal->confirmSelection() == 0) {
                // "确认" → 同意执行
                terminal->submitConfirm(true);
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
            terminal->cycleConfirmSelection();
            return true;
        }
        // Shift+Tab: 确认栏期间仍可切换安全模式
        if (e == ftxui::Event::TabReverse) {
            m_repl.cycleMode();
            return true;
        }
        return true;  // 屏蔽其他所有按键
    }

    // === 1.4 选区态事件接管（验收收敛：仅鼠标拖选，松手自动复制） ===
    // 交互定稿（用户决策）：放弃 Ctrl+S 键盘选区与 Ctrl+C/Enter 复制；
    // Shift+拖选+右键为终端原生路径（事件从未到达应用，日志实证 btn=2 为零），
    // 应用内等价操作 = 左键拖选 → 松手自动复制（copy-on-select），
    // 之后右键粘贴（终端原生粘贴）即可。
    if (m_selection.active()) {
        if (e == ftxui::Event::Escape) {
            m_selection.clear();
            m_repl.setDragAutoScroll(false);
            return true;
        }
        // 拖选自动滚动 tick（2026-09-20 记事本式边缘滚动）：鼠标贴顶/
        // 拖出终端窗口（事件停更、最后位置保持贴顶）→ 上滚；越过内容区
        // 下边缘（输入框侧）→ 下滚。滚动后按最后鼠标位置扩展选区。
        if (e.input() == std::string_view("\x1B[DT")) {
            if (m_dragMoved && m_view.aboveContent(m_dragLastY)) {
                m_view.autoScrollStep(true);
                if (auto hit = m_view.hitTest(m_dragLastX, m_dragLastY))
                    m_selection.extendTo(std::get<0>(*hit),
                                         std::get<1>(*hit),
                                         std::get<2>(*hit));
            } else if (m_view.belowContent(m_dragLastY)) {
                m_view.autoScrollStep(false);
                if (auto hit = m_view.hitTest(m_dragLastX, m_dragLastY))
                    m_selection.extendTo(std::get<0>(*hit),
                                         std::get<1>(*hit),
                                         std::get<2>(*hit));
            }
            return true;
        }
        if (e.is_mouse()) {
            auto& m = e.mouse();
            m_dragLastX = m.x;   // 记录最后鼠标位置（出窗口后事件停更）
            m_dragLastY = m.y;
            if (m.button == ftxui::Mouse::WheelUp
                || m.button == ftxui::Mouse::WheelDown) {
                // 拖选期间滚轮：滚动 + 选区跟随扩展（跨视口连续选择，
                // 2026-09-20）。滚轮事件带鼠标坐标——滚动后（可见区间
                // 已在 handleEvent 内立即重算）鼠标下方内容即新游标位置。
                if (m_view.scrollHandleEvent(e)) {
                    if (auto hit = m_view.hitTest(m.x, m.y))
                        m_selection.extendTo(std::get<0>(*hit),
                                             std::get<1>(*hit),
                                             std::get<2>(*hit));
                }
                return true;
            }
            if (m.button == ftxui::Mouse::Left) {
                if (m.motion == ftxui::Mouse::Moved)
                    m_dragMoved = true;   // 有拖动动作才允许贴顶上滚（防单击首行误滚）
                if (m.motion == ftxui::Mouse::Released) {
                    // 松手：先含入最终位置（松手点可能没有对应 Moved 事件），
                    // 非空选区 → 复制 + 清除；单击/拖回起点（空选区）→ 仅清除
                    if (auto hit = m_view.hitTest(m.x, m.y))
                        m_selection.extendTo(std::get<0>(*hit),
                                             std::get<1>(*hit),
                                             std::get<2>(*hit));
                    if (!m_selection.empty()) {
                        auto r = m_selection.range();
                        std::string out = CLFSelectionModel::extract(
                            r, m_lastRowMap, m_lastRowTexts);
                        if (!out.empty()) CLFClipboard::write(out);
                        if (dbgEvt)
                            dbgEvt("  dragcopy sel=[" + std::to_string(r.fromRow)
                                   + "," + std::to_string(r.toRow) + "] out='"
                                   + escDbg(out) + "'");
                    }
                    m_selection.clear();
                    m_repl.setDragAutoScroll(false);
                    return true;
                }
                // Pressed / Moved → 扩展选区（游标含入鼠标所在字符）
                if (auto hit = m_view.hitTest(m.x, m.y))
                    m_selection.extendTo(std::get<0>(*hit),
                                         std::get<1>(*hit),
                                         std::get<2>(*hit));
                return true;
            }
        }
        return true;  // 拖选期间其他事件消费
    }

    // === 1.45 选区进入（仅鼠标左键按下） ===
    if (e.is_mouse()) {
        auto& m = e.mouse();
        if (m.button == ftxui::Mouse::Left && m.motion == ftxui::Mouse::Pressed) {
            // 内容区按下 → 进入选区（锚点=字符起始）；非内容区（输入框等）放行给 Input
            if (auto hit = m_view.hitTest(m.x, m.y)) {
                m_selection.startAt(std::get<0>(*hit), std::get<1>(*hit),
                                    std::get<2>(*hit));
                // 启动拖选自动滚动定时（2026-09-20 记事本式边缘滚动）
                m_dragLastX = m.x;
                m_dragLastY = m.y;
                m_dragMoved = false;
                m_repl.setDragAutoScroll(true);
                return true;
            }
            return false;
        }
    }

    // === 1.5 粘贴合并器事件路由（Return/字符/其他三类） ===
    {
        auto now = std::chrono::steady_clock::now();
        if (e == ftxui::Event::Return) {
            auto act = pasteCoalescer.onReturn(now, inputText);
            if (dbgEvt)
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
            if (dbgEvt)
                dbgEvt("  onChar -> " + std::to_string(static_cast<int>(act))
                       + " input=" + std::to_string(inputText.size()));
            if (act == CLFPasteCoalescer::Action::RestoreAndAppendChar) {
                inputText = pasteCoalescer.pendingText() + "\n" + e.character();
                *cursorPos = static_cast<int>(inputText.size());
                if (dbgEvt)
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
    // 验收收敛（用户决策）：空闲时忽略——原"空闲 Ctrl+C 退出"与
    // 用户直觉冲突（误触即退出）；退出统一 Esc Esc / /exit。busy 时中断保留。
    // B3（2026-09-23）：Ctrl+C 与 ESC 收尾统一——原"只发信号无任何收尾"
    // 与 ESC 全套收尾（状态行/输入框恢复/CPR 残留剥离）两套观感
    if (e == ftxui::Event::CtrlC) {
        if (dbgEvt)
            dbgEvt("  CtrlC busy="
                   + std::string(asyncSubmit.busy() ? "1" : "0"));
        if (asyncSubmit.busy()) {
            if (terminal) terminal->interruptFromUi();
            doInterruptCleanup(true);
        }
        // 空闲：消费且无动作（不退出）
        return true;
    }

    // === 5. Esc: 双击退出 + 立即中断 ===
    // 注：Alt+Enter 弃用（终端层面触发全屏），换行用 Ctrl+N
    if (e == ftxui::Event::Escape
        || e == ftxui::Event::Special({27, 27})) {
        // 5a. 双击检测（空闲时 500ms 内连续两次 Esc → 退出）
        // B2 修复 (a)+(b)（2026-09-23，§13.4 定稿）：(a) 中断那次 ESC 不参与
        // 双击计时 + (b) 中断后 800ms 冷却——修复越快越危险：中断后回合
        // ~50ms 即空闲，用户"连按 ESC 确保停住"的习惯会命中退出分支
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastEscTime).count();
        const bool wasBusy = asyncSubmit.busy();
        if (elapsed < 500 && !wasBusy && now >= m_interruptCooldownUntil) {
            m_lastEscTime = {};
            m_dispatcher->handle("/exit");
            return true;
        }
        m_lastEscTime = wasBusy ? std::chrono::steady_clock::time_point{} : now;

        // 5b. 立即中断（B3：收尾与 Ctrl+C 共用 doInterruptCleanup）
        if (terminal) terminal->interruptFromUi();
        doInterruptCleanup(wasBusy);
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
        m_repl.cycleMode();
        return true;
    }

    // === 8. 滚动 ===
    if (m_view.scrollHandleEvent(e))
        return true;

    // === 通用：剥离泄露到输入框的 CPR/ANSI 残留 ===
    // 终端 CPR \033[n;mR 的 \033 被 CatchEvent 吃掉后，[n;mR 作为
    // Character 事件到达此处。在所有 handler 未匹配时统一剥离。
    stripCprResidual(inputText);

    // ESC 后延迟清理帧：继续 Post Custom 事件触发后续渲染
    if (m_escCleanupFrames > 0) {
        --m_escCleanupFrames;
        if (m_escCleanupFrames > 0)
            screen->PostEvent(ftxui::Event::Custom);
    }

    return false;
}

} // namespace CLF::CLFUI
