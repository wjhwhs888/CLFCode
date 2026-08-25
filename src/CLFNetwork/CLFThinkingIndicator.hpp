// CLFThinkingIndicator.hpp — API 等待指示器
// RAII：构造标记等待开始，stop()/析构时清理状态行。
// ESC 中断由 AgentLoop 通过 onInterrupt 回调处理, 此处不再检测。
//
// 注：此前这里跑着一个每秒轮询的后台线程，但其循环体仅计算了一个从未被使用的
// elapsed（StatusLine 已由 AgentLoop 的 turnTimer 统一管理），属纯空转；
// 唯一实效是退出时清一次状态行。线程已移除，stop() 现在同步完成、立即返回。
//
// example:
//   CLFThinkingIndicator thinking(client.get(), output);
//   auto resp = client->postJson(...);
//   thinking.stop();

#pragma once

#include <atomic>
#include <chrono>

namespace CLF::CLFNetwork { class ICLFHttpClient; }
namespace CLF::CLFTypes { class ICLFOutput; }

namespace CLF::CLFNetwork {

class CLFThinkingIndicator {
public:
    CLFThinkingIndicator(CLF::CLFNetwork::ICLFHttpClient* http = nullptr,
                         CLF::CLFTypes::ICLFOutput* output = nullptr);
    ~CLFThinkingIndicator();

    // 清理状态行（幂等）
    void stop();

    CLFThinkingIndicator(const CLFThinkingIndicator&) = delete;
    CLFThinkingIndicator& operator=(const CLFThinkingIndicator&) = delete;

private:
    std::atomic<bool> m_done{false};
    // 当前未使用——保留是为了将来在此处接入超时自动 abort（构造签名亦保持稳定）
    CLF::CLFNetwork::ICLFHttpClient* m_http;
    CLF::CLFTypes::ICLFOutput* m_output = nullptr;
    std::chrono::steady_clock::time_point m_start;
};

} // namespace CLF::CLFNetwork
