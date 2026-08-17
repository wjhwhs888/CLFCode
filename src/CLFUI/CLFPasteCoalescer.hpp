// CLFPasteCoalescer.hpp — 粘贴事件突发合并器
// 终端粘贴（Ctrl+V / Shift+右键）以"字符 + Return 突发批次"到达，
// 用事件时间戳区分粘贴换行与手打回车：Return 后开 quietWindowMs 静默窗，
// 窗内后续事件视为粘贴 → 换行插入而非提交。
// 设计：`.claude/plans/设计/归档/归档-复制粘贴功能修改.md` §二

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace CLF::CLFUI {

class CLFPasteCoalescer {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    // wakeCb：定时线程唤醒主循环的通道（CLFRepl 注入 PostEvent(Custom)）；
    // quietWindowMs：静默窗时长，默认 40ms，测试注入小值加速
    explicit CLFPasteCoalescer(std::function<void()> wakeCb, int quietWindowMs = 40);
    ~CLFPasteCoalescer();  // 常驻定时线程 join

    // 事件入口（CLFRepl CatchEvent 调用，仅主循环）。now 注入以便单测。
    // 返回对当前事件的处理指令；文本拼接动作由 CLFRepl 执行——
    // 事件字符在 CLFRepl 手中（e.character()），pendingText 经 pendingText() 查询。
    enum class Action {
        PassThrough,             // 放行给 Input（PASTE_MODE 内字符 / IDLE 任意事件）
        Consume,                 // 吃掉，无文本动作（空文本 Return 短路 / Return 进入待提交）
        RestoreAndAppendChar,    // PENDING→PASTE（字符触发）：CLFRepl 执行
                                 //   inputText = pendingText() + "\n" + e.character()
        RestoreAndAppendNewline, // PENDING→PASTE（空行 Return 触发）：CLFRepl 执行
                                 //   inputText = pendingText() + "\n\n"
        InsertNewline,           // PASTE_MODE 内 Return：CLFRepl 执行
                                 //   input->OnEvent(Event::Character("\n"))
    };
    Action onReturn(TimePoint now, const std::string& inputText);
    Action onCharacter(TimePoint now);
    Action onOtherEvent(TimePoint now);   // PENDING→取消待提交；PASTE_MODE→退出；IDLE→PassThrough

    // 窗满确认状态（主循环在 CatchEvent 顶部轮询，线程安全）
    bool pendingConfirmed() const { return m_pendingConfirmed.load(); }

    // 仅主循环调用！幂等：仍处 PENDING_SUBMIT 才复位回 IDLE 并返回 true（= 应执行提交）；
    // 状态已变（竞态被取消）→ 仅清标志返回 false。提交动作由调用方用当前
    // inputText 走既有 :414 提交分支执行——窗口期内 inputText 未被改写（不变式见设计 §2.1）。
    bool consumePendingConfirmation();

    // 供 CLFRepl 查询/接管文本。仅主循环调用（与写入同线程），返回引用
    const std::string& pendingText() const { return m_pendingText; }

private:
    enum class State { Idle, PendingSubmit, PasteMode };

    // 进入 PENDING_SUBMIT（调用方持锁）：捕获文本 + deadline + 唤醒定时线程
    void enterPendingLocked(const std::string& inputText, TimePoint now);
    // 取消待提交（调用方持锁）：仅清谓词，定时线程到点后见谓词 false 继续休眠
    void cancelPendingLocked();

    State       m_state = State::Idle;
    std::string m_pendingText;
    TimePoint   m_lastPasteEvent;
    TimePoint   m_deadline;
    std::mutex  m_mutex;                 // 保护 m_state/m_pendingText/m_deadline/m_pendingActive/m_stopRequested
    std::condition_variable m_cv;
    bool        m_pendingActive = false; // cv 谓词
    bool        m_stopRequested = false;
    std::thread m_timer;                 // 常驻线程：wait(active && now>=deadline) → 置 confirmed + wakeCb
    std::function<void()> m_wakeCb;
    std::atomic<bool> m_pendingConfirmed{false};
    int         m_quietWindowMs;
};

} // namespace CLF::CLFUI
