// CLFHttpClient.hpp — HTTP 通信客户端
// 封装 cpp-httplib，支持同步和流式 API 请求

#pragma once

#include <functional>
#include <string>

namespace CLF::CLFNetwork {

struct CLFHttpResponse {
    int         m_statusCode = 0;
    std::string m_body;
    std::string m_error;
};

class CLFHttpClient {
public:
    CLFHttpClient(const std::string& baseUrl, const std::string& apiKey);

    // 同步 POST JSON 请求
    CLFHttpResponse postJson(const std::string& path, const std::string& jsonBody);

    // 流式 POST 请求（每行 SSE 数据触发回调）
    CLFHttpResponse postJsonStream(
        const std::string& path,
        const std::string& jsonBody,
        std::function<void(const std::string& line)> onLine
    );

    // 设置请求超时（秒）
    void setTimeout(int seconds);

private:
    std::string m_baseUrl;
    std::string m_apiKey;
    int         m_timeoutSec = 30;
};

} // namespace CLF::CLFNetwork
