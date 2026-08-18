// CLFPasteCoalescer.cpp — 粘贴事件突发合并器实现
// 状态机与线程模型详见设计文档 §2.1/§2.2

#include "CLFUI/CLFPasteCoalescer.hpp"
#include "CLFCore/CLFLogger.hpp"

namespace CLF::CLFUI {

CLFPasteCoalescer::CLFPasteCoalescer(std::function<void()> wakeCb, int quietWindowMs)
    : m_wakeCb(std::move(wakeCb))
    , m_quietWindowMs(quietWindowMs) {
    m_timer = std::thread([this] {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (;;) {
            if (m_pendingActive) {
                // 有待提交：睡到 deadline（wait_until 时间到自动唤醒，不依赖 notify）
                m_cv.wait_until(lock, m_deadline, [this] {
                    return m_stopRequested || !m_pendingActive;
                });
                if (m_stopRequested) break;
                if (!m_pendingActive) continue;  // 被取消（取消不 notify，到点醒来见谓词）
                // deadline 到且仍 active → 确认
                m_pendingActive = false;
                m_pendingConfirmed.store(true);
                auto cb = m_wakeCb;
                lock.unlock();
                // B3: 兜底防 std::terminate；catch 后必须重新加锁（异常路径也走 lock.lock()），
                //     否则析构时 notify_one 在未持锁状态下调用 = UB/死锁
                try {
                    if (cb) cb();
                } catch (const std::exception& e) {
                    CLF::CLFCore::CLFLogger::instance().error(
                        std::string("[PasteTimer] wakeCb exception: ") + e.what());
                } catch (...) {
                    CLF::CLFCore::CLFLogger::instance().error(
                        "[PasteTimer] wakeCb unknown exception");
                }
                lock.lock();
            } else {
                // 无待提交：无限睡，新 PENDING 进入时 notify 唤醒
                m_cv.wait(lock, [this] { return m_stopRequested || m_pendingActive; });
                if (m_stopRequested) break;
                // 新 PENDING 已置 → 回到 wait_until 分支
            }
        }
    });
}

CLFPasteCoalescer::~CLFPasteCoalescer() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopRequested = true;
        m_pendingActive = false;
    }
    m_cv.notify_one();
    if (m_timer.joinable()) m_timer.join();
}

void CLFPasteCoalescer::enterPendingLocked(const std::string& inputText, TimePoint now) {
    m_state = State::PendingSubmit;
    m_pendingText = inputText;
    m_pendingActive = true;
    m_deadline = now + std::chrono::milliseconds(m_quietWindowMs);
    m_cv.notify_one();
}

void CLFPasteCoalescer::cancelPendingLocked() {
    m_pendingActive = false;  // 定时线程到点后见谓词 false 继续休眠，无需 notify
}

CLFPasteCoalescer::Action CLFPasteCoalescer::onReturn(TimePoint now,
                                                      const std::string& inputText) {
    std::unique_lock<std::mutex> lock(m_mutex);
    switch (m_state) {
    case State::Idle:
        // 空文本也进 PENDING：粘贴以空行开头（"\nfoo"）的首个 Return 即空文本，
        // 短路会丢前导空行。窗满后 doSubmit 对空文本 no-op，现有空回车语义不变
        enterPendingLocked(inputText, now);
        return Action::Consume;  // Return 被消费，提交由窗满确认路径执行

    case State::PendingSubmit: {
        // 空行规则：连续 Return（粘贴含空行）→ 转 PASTE_MODE
        cancelPendingLocked();
        m_state = State::PasteMode;
        m_lastPasteEvent = now;
        return Action::RestoreAndAppendNewline;
    }

    case State::PasteMode: {
        auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - m_lastPasteEvent).count();
        if (since > m_quietWindowMs) {
            // 静默后回车 = 真提交：退出 PASTE_MODE 按 IDLE Return 处理
            m_state = State::Idle;
            enterPendingLocked(inputText, now);
            return Action::Consume;
        }
        m_lastPasteEvent = now;
        return Action::InsertNewline;
    }
    }
    return Action::PassThrough;
}

CLFPasteCoalescer::Action CLFPasteCoalescer::onCharacter(TimePoint now) {
    std::unique_lock<std::mutex> lock(m_mutex);
    switch (m_state) {
    case State::Idle:
        return Action::PassThrough;
    case State::PendingSubmit:
        cancelPendingLocked();
        m_state = State::PasteMode;
        m_lastPasteEvent = now;
        return Action::RestoreAndAppendChar;
    case State::PasteMode:
        m_lastPasteEvent = now;
        return Action::PassThrough;
    }
    return Action::PassThrough;
}

CLFPasteCoalescer::Action CLFPasteCoalescer::onOtherEvent(TimePoint now) {
    (void)now;
    std::unique_lock<std::mutex> lock(m_mutex);
    switch (m_state) {
    case State::Idle:
        return Action::PassThrough;
    case State::PendingSubmit:
        cancelPendingLocked();
        m_state = State::Idle;  // 事件本身继续正常处理（PassThrough）
        return Action::PassThrough;
    case State::PasteMode:
        m_state = State::Idle;  // 非粘贴事件天然结束粘贴序列
        return Action::PassThrough;
    }
    return Action::PassThrough;
}

bool CLFPasteCoalescer::consumePendingConfirmation() {
    m_pendingConfirmed.store(false);
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_state != State::PendingSubmit) return false;
    m_state = State::Idle;
    cancelPendingLocked();  // 防御：确认后谓词已由定时线程清空，此处兜底
    return true;
}

} // namespace CLF::CLFUI
