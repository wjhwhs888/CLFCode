// CLFHttpClient.hpp — HTTP 通信客户端
// 封装 cpp-httplib，支持同步和流式 API 请求
// ICLFHttpClient 为抽象接口（支持 Mock 测试注入）

#pragma once

#include <functional>
#include <memory>
#include <string>

namespace CLF::CLFNetwork {

struct CLFHttpResponse {
    int         m_statusCode = 0;
    std::string m_body;
    std::string m_error;
};

// HTTP 客户端抽象接口（L2 集成测试用 Mock 替换）
class ICLFHttpClient {
public:
    virtual ~ICLFHttpClient() = default;

    // 同步 POST JSON 请求
    virtual CLFHttpResponse postJson(const std::string& path, const std::string& jsonBody) = 0;

    // 流式 POST 请求（每行 SSE 数据触发回调）
    virtual CLFHttpResponse postJsonStream(
        const std::string& path,
        const std::string& jsonBody,
        std::function<void(const std::string& line)> onLine) = 0;

    // 设置请求超时（秒）
    virtual void setTimeout(int seconds) = 0;
};

class CLFHttpClient : public ICLFHttpClient {
public:
    CLFHttpClient(const std::string& baseUrl, const std::string& apiKey);

    CLFHttpResponse postJson(const std::string& path, const std::string& jsonBody) override;

    CLFHttpResponse postJsonStream(
        const std::string& path,
        const std::string& jsonBody,
        std::function<void(const std::string& line)> onLine) override;

    void setTimeout(int seconds) override;

private:
    std::string m_baseUrl;
    std::string m_apiKey;
    int         m_timeoutSec = 30;
};

} // namespace CLF::CLFNetwork
