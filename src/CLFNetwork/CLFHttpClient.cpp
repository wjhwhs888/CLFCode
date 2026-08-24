// CLFHttpClient.cpp — HTTP 客户端实现

#include "CLFNetwork/CLFHttpClient.hpp"

#include <httplib.h>

namespace CLF::CLFNetwork {
namespace {

// RAII 守卫：析构时自动清理 m_activeCli（即使回调抛异常也安全）
class ActiveCliGuard {
public:
    ActiveCliGuard(std::mutex& m, std::shared_ptr<httplib::Client>& activeCli)
        : m_mutex(m), m_activeCli(activeCli) {}
    ~ActiveCliGuard() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_activeCli.reset();
    }
    ActiveCliGuard(const ActiveCliGuard&) = delete;
    ActiveCliGuard& operator=(const ActiveCliGuard&) = delete;
private:
    std::mutex& m_mutex;
    std::shared_ptr<httplib::Client>& m_activeCli;
};

} // anonymous namespace

CLFHttpClient::CLFHttpClient(const std::string& baseUrl, const std::string& apiKey)
    : m_baseUrl(baseUrl)
    , m_apiKey(apiKey) {
}

CLFHttpResponse CLFHttpClient::postJson(const std::string& path, const std::string& jsonBody) {
    CLFHttpResponse result;
    m_aborted = false;  // 新请求开始，重置中断标志（与 postJsonStream 对称）

    auto cli = std::make_shared<httplib::Client>(m_baseUrl);
    cli->set_connection_timeout(10, 0);
    cli->set_read_timeout(m_timeoutSec, 0);
    {
        std::lock_guard<std::mutex> lock(m_cliMutex);
        m_activeCli = cli;
    }
    ActiveCliGuard guard(m_cliMutex, m_activeCli);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + m_apiKey},
        {"Content-Type", "application/json"}
    };

    auto res = cli->Post(path, headers, jsonBody, "application/json");

    if (!res) {
        // abort() 会 stop() 活动连接，httplib 表现为连接失败——
        // 标记来源以便上层区分"用户中断"与"网络故障"，避免无谓重试
        result.m_wasAborted = m_aborted;
        result.m_error = "Connection failed: " + httplib::to_string(res.error());
        return result;
    }

    result.m_statusCode = res->status;
    result.m_body       = res->body;

    if (res->status < 200 || res->status >= 300) {
        result.m_error = "HTTP " + std::to_string(res->status) + ": " + res->body;
    }

    result.m_wasAborted = m_aborted;
    return result;
}

CLFHttpResponse CLFHttpClient::postJsonStream(
    const std::string& path,
    const std::string& jsonBody,
    std::function<void(const std::string& line)> onLine
) {
    CLFHttpResponse result;
    m_aborted = false;  // 新请求开始，重置中断标志

    auto cli = std::make_shared<httplib::Client>(m_baseUrl);
    cli->set_connection_timeout(10, 0);
    cli->set_read_timeout(m_timeoutSec, 0);
    {
        std::lock_guard<std::mutex> lock(m_cliMutex);
        m_activeCli = cli;
    }
    ActiveCliGuard guard(m_cliMutex, m_activeCli);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + m_apiKey},
        {"Content-Type", "application/json"}
    };

    // SSE 行缓冲：网络 chunk 边界可能切开一行数据，需缓冲尾部跨块拼接
    std::string lineBuffer;
    size_t cursor = 0;  // 游标替代逐行 erase，避免 O(n²)

    auto res = cli->Post(
        path, headers, jsonBody, "application/json",
        [&](const char* data, size_t dataLen) {
            // 中断检查：abort() 设置 m_aborted，立即停止接收
            if (m_aborted) return false;

            lineBuffer.append(data, dataLen);

            while (true) {
                size_t end = lineBuffer.find('\n', cursor);
                if (end == std::string::npos) {
                    break;
                }
                std::string line = lineBuffer.substr(cursor, end - cursor);
                cursor = end + 1;

                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    onLine(line);
                }
                // 每条 SSE 行处理后复查中断标志
                if (m_aborted) return false;
            }

            // 消费后裁剪已处理前缀，防止 buffer 无限增长
            if (cursor > 4096) {
                lineBuffer.erase(0, cursor);
                cursor = 0;
            }
            return !m_aborted;
        }
    );

    // 流结束后，冲刷缓冲中的最后一行（无 \n 结尾的最终数据）
    if (cursor < lineBuffer.size()) {
        std::string last = lineBuffer.substr(cursor);
        if (!last.empty() && last.back() == '\r') {
            last.pop_back();
        }
        if (!last.empty()) {
            onLine(last);
        }
    }
    lineBuffer.clear();

    if (!res) {
        // 中断时接收回调返回 false，httplib 同样表现为连接失败——同 postJson，标记来源
        result.m_wasAborted = m_aborted;
        result.m_error = "Stream connection failed: " + httplib::to_string(res.error());
        return result;
    }

    result.m_statusCode = res->status;
    if (res->status < 200 || res->status >= 300) {
        result.m_error = "HTTP " + std::to_string(res->status);
    }
    result.m_wasAborted = m_aborted;
    return result;
}

void CLFHttpClient::setTimeout(int seconds) {
    m_timeoutSec = seconds;
}

void CLFHttpClient::abort() {
    m_aborted = true;
    std::lock_guard<std::mutex> lock(m_cliMutex);
    if (m_activeCli) {
        m_activeCli->stop();
    }
}

} // namespace CLF::CLFNetwork
