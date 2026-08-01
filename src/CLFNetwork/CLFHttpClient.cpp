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

    auto cli = std::make_shared<httplib::Client>(m_baseUrl);
    cli->set_connection_timeout(10, 0);
    cli->set_read_timeout(m_timeoutSec, 0);
    {
        std::lock_guard<std::mutex> lock(m_cliMutex);
        m_activeCli = cli;
    }

    httplib::Headers headers = {
        {"Authorization", "Bearer " + m_apiKey},
        {"Content-Type", "application/json"}
    };

    auto res = cli->Post(path, headers, jsonBody, "application/json");

    {
        std::lock_guard<std::mutex> lock(m_cliMutex);
        m_activeCli.reset();
    }

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

    auto cli = std::make_shared<httplib::Client>(m_baseUrl);
    cli->set_connection_timeout(10, 0);
    cli->set_read_timeout(m_timeoutSec, 0);
    {
        std::lock_guard<std::mutex> lock(m_cliMutex);
        m_activeCli = cli;
    }

    httplib::Headers headers = {
        {"Authorization", "Bearer " + m_apiKey},
        {"Content-Type", "application/json"}
    };

    // SSE 行缓冲：网络 chunk 边界可能切开一行数据，需缓冲尾部跨块拼接
    std::string lineBuffer;

    auto res = cli->Post(
        path, headers, jsonBody, "application/json",
        [&](const char* data, size_t dataLen) {
            lineBuffer.append(data, dataLen);

            size_t pos = 0;
            while (true) {
                size_t end = lineBuffer.find('\n', pos);
                if (end == std::string::npos) {
                    break;
                }
                std::string line = lineBuffer.substr(pos, end - pos);
                pos = end + 1;

                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    onLine(line);
                }
            }

            if (pos > 0) {
                lineBuffer.erase(0, pos);
            }
            return true;
        }
    );

    // 流结束后，冲刷缓冲中的最后一行（无 \n 结尾的最终数据）
    if (!lineBuffer.empty()) {
        std::string last = lineBuffer;
        if (!last.empty() && last.back() == '\r') {
            last.pop_back();
        }
        if (!last.empty()) {
            onLine(last);
        }
        lineBuffer.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_cliMutex);
        m_activeCli.reset();
    }

    if (!res) {
        result.m_error = "Stream connection failed: " + httplib::to_string(res.error());
        return result;
    }

    result.m_statusCode = res->status;
    if (res->status < 200 || res->status >= 300) {
        result.m_error = "HTTP " + std::to_string(res->status);
    }
    return result;
}

void CLFHttpClient::setTimeout(int seconds) {
    m_timeoutSec = seconds;
}

void CLFHttpClient::abort() {
    std::lock_guard<std::mutex> lock(m_cliMutex);
    if (m_activeCli) {
        m_activeCli->stop();
    }
}

} // namespace CLF::CLFNetwork
