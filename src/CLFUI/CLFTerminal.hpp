// CLFTerminal.hpp — 终端 UI (FTXUI 组件树 + ICLFOutput 实现)
// 渲染核心由 FTXUI 全帧驱动, CLFTerminal 只管理状态 + 实现 ICLFOutput

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "CLFTypes/ICLFOutput.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

namespace CLF::CLFUI {

class CLFTerminal : public CLF::CLFTypes::ICLFOutput {
public:
    CLFTerminal() = default;
    ~CLFTerminal();

    // === FTXUI 入口 ===
    void setScreen(ftxui::ScreenInteractive* screen) { m_screen = screen; }
    void requestRefresh() override;  // ⑧ ICLFOutput：PostEvent(Custom)，m_screen 为空时安全跳过

    // === 静态工具 (委托 CLFAnsi) ===
    static void enableAnsi();
    static std::string cyan(const std::string& s);
    static std::string red(const std::string& s);
    static std::string gray(const std::string& s);
    static std::string bold(const std::string& s);
    static int  getTerminalHeight();
    static int  getTerminalWidth();
    static std::string diagnosticInfo();

    // === ICLFOutput 实现 ===
    void emitContent(const std::string& t) override;
    void emitRaw(const std::string& d) override;
    void emitStyledLine(const std::string& line, LineStyle style) override;
    void setStatus(const std::string& title, int cur=-1, int total=-1) override;
    void setStatusTextOnly(const std::string& title) override;
    void showProgress(const std::vector<std::string>& lines) override;
    void finishProgress(const std::string& summary) override;
    bool confirm(const std::string& prompt) override;
    void onInterrupt(std::function<void()> cb) override;
    void emitError(const std::string& m) override;

    // === ICLFOutput ⑧ 状态点 ===
    void setStatusKind(ICLFOutput::StatusKind kind) override;

    // === ICLFOutput ⑨ 恢复回显折叠块（P2-1） ===
    void showFoldedBlock(const std::string& summary,
                         const std::vector<std::string>& lines) override;
    void toggleFoldedBlock();  // Ctrl+R 展开/收起

    // === ICLFOutput ⑦ 思考内容 ===
    void appendThinking(const std::string& text) override;
    void clearThinking() override;
    // （A2：hasThinkingContent/getThinkingLines 死代码已删——零调用，
    //   思考行列表由 contentSnapshot 直接提供）

    // 线程安全：confirm 工作线程写 / 主线程 CatchEvent 读
    bool isConfirmActive() const { std::lock_guard lock(m_mutex); return m_confirmActive; }
    void setConfirmActive(bool v) { std::lock_guard lock(m_mutex); m_confirmActive = v; }

    // === 线程安全快照 (Renderer 使用, 一次加锁拷贝) ===
    struct ContentSnapshot {
        std::vector<std::string> lines;
        std::string pendingLine;
        std::vector<uint8_t> lineStyles; // 并行于 lines，LineStyle 枚举值
        std::string statusText;
        ICLFOutput::StatusKind statusKind = ICLFOutput::StatusKind::None; // 状态点种类
        std::vector<std::string> progressLines; // 渐进式进度块
        // 思考内容（折叠/展开用）
        std::vector<std::string> thinkingLines;
        bool thinkingActive = false;
        size_t thinkingBytes = 0;
        int  thinkingElapsed = 0;  // 思考已持续秒数
        // confirm
        bool confirmActive = false;
        std::string confirmPrompt;
        std::vector<std::string> confirmOpts;
        int  confirmSel = 0;
        // 恢复回显折叠块（P2-1）
        std::string foldedSummary;
        std::vector<std::string> foldedLines;
        bool foldedExpanded = false;
    };
    ContentSnapshot contentSnapshot() const;

    // === 组件状态（C4：收 private，外部经窄操作/快照访问） ===
    // 渲染读走 contentSnapshot()；确认协议走 submitConfirm/cycleConfirmSelection；
    // 中断走 interruptFromUi()；启动重置走 clearContent()；刷新消费走
    // consumeRefreshPending()。直写点已全量收敛（C4-1 清单，2026-09-07）。

    // === C4 窄操作 ===

    // 启动重置：清内容缓冲与待写行（CLFRepl::run 构造时调用，清残留）
    void clearContent();

    // 刷新标志消费（CLFReplView 每帧检查；返回旧值）
    bool consumeRefreshPending();

    // 确认协议（CLFInputHandler 事件 → 唤醒 confirm() 的 worker 线程）：
    // submitConfirm：锁内写结果 + 清激活态 + notify（锁序 confirmMutex→mutex，
    // 与原内联代码一致）
    void submitConfirm(bool accepted);
    // 当前选中项（UI 线程读；confirm 激活期 worker 不再写 sel，无锁与现状一致）
    int  confirmSelection() const { return m_confirmSel; }
    // 两选项切换 0↔1（UI 线程独占写）
    void cycleConfirmSelection() { m_confirmSel = 1 - m_confirmSel; }
    // UI 侧触发中断回调（判空调用；Esc/Ctrl+C 路径）
    void interruptFromUi();

private:
    std::vector<std::string> m_contentBuffer;
    std::vector<uint8_t> m_lineStyles;
    std::string  m_pendingLine;
    bool         m_inAnsiSeq = false;
    // Markdown 表格缓冲：连续以 | 开头的行暂存，遇非表行时对齐后一次性输出
    std::vector<std::string> m_tableBuffer;
    // 思考缓冲（与 content 分离，Ctrl+T 折叠/展开）
    std::string  m_thinkingBuffer;
    bool         m_thinkingActive = false;
    size_t       m_thinkingBytes = 0;
    int          m_thinkingElapsed = 0;  // 思考总耗时（秒）
    std::chrono::steady_clock::time_point m_thinkingStart;
    std::string  m_statusText;
    ICLFOutput::StatusKind m_statusKind = ICLFOutput::StatusKind::None;
    std::vector<std::string> m_progressLines;
    // 恢复回显折叠块（P2-1）
    std::string  m_foldedSummary;
    std::vector<std::string> m_foldedLines;
    bool         m_foldedExpanded = false;
    mutable std::mutex m_progressMutex;
    // confirm (由 CLFTerminal::confirm + CLFInputHandler 事件共同操作；
    // C4 后 UI 侧经 submitConfirm/cycleConfirmSelection 窄操作)
    std::string  m_confirmPrompt;
    std::vector<std::string> m_confirmOpts;
    int          m_confirmSel = 0;
    bool         m_confirmActive = false;
    bool         m_confirmResult = false;
    std::mutex   m_confirmMutex;
    std::condition_variable m_confirmCv;
    // 中断 / 刷新
    std::function<void()> m_interruptCb;
    std::atomic<bool>     m_refreshPending{false};

    ftxui::ScreenInteractive* m_screen = nullptr;
    mutable std::mutex m_mutex;

    // 将对齐后的表格缓冲写入 m_contentBuffer
    void flushTable();
};

} // namespace CLF::CLFUI
