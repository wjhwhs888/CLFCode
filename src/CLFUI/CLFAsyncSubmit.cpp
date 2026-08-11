// CLFAsyncSubmit.cpp — 异步提交线程管理实现

#include "CLFUI/CLFAsyncSubmit.hpp"
#include "CLFCore/CLFLogger.hpp"

namespace CLF::CLFUI {

CLFAsyncSubmit::~CLFAsyncSubmit() {
    join();
}

void CLFAsyncSubmit::launch(std::function<void()> task) {
    join();  // 保证上一个已完成
    m_submitting = true;
    m_thread = std::thread([this, t = std::move(task)]() {
        try {
            t();
        } catch (const std::exception& e) {
            CLF::CLFCore::CLFLogger::instance().error(
                std::string("[AsyncSubmit] Uncaught std::exception: ") + e.what());
        } catch (...) {
            CLF::CLFCore::CLFLogger::instance().error(
                "[AsyncSubmit] Uncaught unknown exception — thread rescued from std::terminate");
        }
        m_submitting = false;
    });
}

void CLFAsyncSubmit::join() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

} // namespace CLF::CLFUI
