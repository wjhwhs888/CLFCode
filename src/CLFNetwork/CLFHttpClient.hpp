// CLFHttpClient.hpp — HTTP 通信客户端 (cpp-httplib 封装)
// 接口定义见 ICLFHttpClient.hpp

#pragma once

#include "CLFNetwork/ICLFHttpClient.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

// fwd
namespace httplib { class Client; }

namespace CLF::CLFNetwork {

class CLFHttpClient : public ICLFHttpClient {
public:
    CLFHttpClient(const std::string& baseUrl, const std::string& apiKey);

    CLFHttpResponse postJson(const std::string& path, const std::string& jsonBody) override;

    CLFHttpResponse postJsonStream(
        const std::string& path,
        const std::string& jsonBody,
        std::function<void(const std::string& line)> onLine) override;

    void setTimeout(int seconds) override;
    void abort() override;

private:
    std::string m_baseUrl;
    std::string m_apiKey;
    int         m_timeoutSec = 30;
    std::mutex  m_cliMutex;
    std::shared_ptr<httplib::Client> m_activeCli;
    std::atomic<bool> m_aborted{false};  // 流式中断：回调检查此标志立即停止
};

} // namespace CLF::CLFNetwork
