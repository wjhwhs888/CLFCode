// CLFHttpClient.cpp — HTTP 客户端实现

#include "CLFNetwork/CLFHttpClient.hpp"

#include <httplib.h>

namespace CLF::CLFNetwork {

CLFHttpClient::CLFHttpClient(const std::string& baseUrl, const std::string& apiKey)
    : m_baseUrl(baseUrl)
    , m_apiKey(apiKey) {
}

CLFHttpResponse CLFHttpClient::postJson(const std::string& path, const std::string& jsonBody) {
    CLFHttpResponse result;

    // 解析 base URL
    httplib::Client cli(m_baseUrl);
    cli.set_connection_timeout(m_timeoutSec, 0);
    cli.set_read_timeout(m_timeoutSec, 0);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + m_apiKey},
        {"Content-Type", "application/json"}
    };

    auto res = cli.Post(path, headers, jsonBody, "application/json");

    if (!res) {
        result.m_error = "Connection failed: " + httplib::to_string(res.error());
        return result;
    }

    result.m_statusCode = res->status;
    result.m_body       = res->body;

    if (res->status < 200 || res->status >= 300) {
        result.m_error = "HTTP " + std::to_string(res->status) + ": " + res->body;
    }

    return result;
}

CLFHttpResponse CLFHttpClient::postJsonStream(
    const std::string& path,
    const std::string& jsonBody,
    std::function<void(const std::string& line)> onLine
) {
    CLFHttpResponse result;

    httplib::Client cli(m_baseUrl);
    cli.set_connection_timeout(m_timeoutSec, 0);
    cli.set_read_timeout(m_timeoutSec, 0);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + m_apiKey},
        {"Content-Type", "application/json"}
    };

    auto res = cli.Post(
        path, headers, jsonBody, "application/json",
        [&](const char* data, size_t dataLen) {
            // SSE 分行处理
            std::string chunk(data, dataLen);
            size_t pos = 0;
            while (pos < chunk.size()) {
                size_t end = chunk.find('\n', pos);
                if (end == std::string::npos) {
                    end = chunk.size();
                }
                std::string line = chunk.substr(pos, end - pos);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    onLine(line);
                }
                pos = end + 1;
            }
            return true;
        }
    );

    if (!res) {
        result.m_error = "Stream connection failed: " + httplib::to_string(res.error());
        return result;
    }

    result.m_statusCode = res->status;
    return result;
}

void CLFHttpClient::setTimeout(int seconds) {
    m_timeoutSec = seconds;
}

} // namespace CLF::CLFNetwork
