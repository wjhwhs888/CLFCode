// ICLFHttpClient.hpp — HTTP 客户端抽象接口 + 响应类型
// CLFHttpClient 依赖此接口; CLFAgentLoop / CLFThinkingIndicator 仅依赖此接口

#pragma once

#include <functional>
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

    // 中止正在进行的请求（线程安全，可从其他线程调用）
    virtual void abort() = 0;
};

} // namespace CLF::CLFNetwork
