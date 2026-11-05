// qa_CLFHttpClient.cpp — HTTP 客户端请求头回归（2026-11-05 实机实抓）
//
// 实抓现象：DeepSeek 网关返回
//   415 "Expected request with `Content-Type: application/json`"
// 根因：httplib 的 detail::set_header 是 unordered_multimap::emplace（无条件
// 追加），而 CLFHttpClient 在 headers 里手动放了一份 Content-Type，Post 的
// content_type 参数又放一份 → 实际发出两个 Content-Type 头。旧网关容忍，
// 新网关（2026-11-03 起）拒绝。
//
// 回归钉：本地回环 httplib::Server 记录实际收到的请求头，断言 postJson /
// postJsonStream 两条路径的 Content-Type 头数量恒为 1 且值正确。
// 不依赖外网；main 期顺序执行（与 qa_CLFToolScheduler 同款，避免静态期线程
// 与 CRT 初始化竞态）。

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <httplib.h>

#include "CLFNetwork/CLFHttpClient.hpp"

using CLF::CLFNetwork::CLFHttpClient;

namespace {

int g_checks = 0;
int g_failures = 0;

#define QA_EXPECT(cond)                                                        \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

// 回环探针：记录服务端实际收到的 Content-Type 头数量与值
class ProbeServer {
public:
    bool start() {
        m_server.Post("/v1/chat/completions",
            [this](const httplib::Request& req, httplib::Response& res) {
                m_contentTypeCount.store(
                    static_cast<int>(req.get_header_value_count("Content-Type")));
                m_contentTypeValue = req.get_header_value("Content-Type");
                m_body = req.body;
                res.set_content("{\"ok\":true}", "application/json");
            });
        m_port = m_server.bind_to_any_port("127.0.0.1");
        if (m_port <= 0) return false;
        m_thread = std::thread([this] { m_server.listen_after_bind(); });
        m_server.wait_until_ready();
        return m_server.is_running();
    }

    void stop() {
        m_server.stop();
        if (m_thread.joinable()) m_thread.join();
    }

    std::string baseUrl() const {
        return "http://127.0.0.1:" + std::to_string(m_port);
    }
    int contentTypeCount() const { return m_contentTypeCount.load(); }
    const std::string& contentTypeValue() const { return m_contentTypeValue; }
    const std::string& body() const { return m_body; }

private:
    httplib::Server m_server;
    std::atomic<int> m_contentTypeCount{-1};
    std::string m_contentTypeValue;
    std::string m_body;
    int m_port = 0;
    std::thread m_thread;
};

// postJson（非流式）路径：Content-Type 头恰好一份
void testPostJsonSingleContentType() {
    ProbeServer probe;
    QA_EXPECT(probe.start());

    CLFHttpClient client(probe.baseUrl(), "test-key");
    auto response = client.postJson("/v1/chat/completions", "{\"hello\":1}");

    QA_EXPECT(response.m_statusCode == 200);
    QA_EXPECT(probe.contentTypeCount() == 1);
    QA_EXPECT(probe.contentTypeValue() == "application/json");
    QA_EXPECT(probe.body() == "{\"hello\":1}");
    QA_EXPECT(response.m_body == "{\"ok\":true}");

    probe.stop();
}

// postJsonStream（流式）路径：Content-Type 头恰好一份，SSE 行回调可达
void testPostJsonStreamSingleContentType() {
    ProbeServer probe;
    QA_EXPECT(probe.start());

    CLFHttpClient client(probe.baseUrl(), "test-key");
    std::string receivedLine;
    auto response = client.postJsonStream(
        "/v1/chat/completions", "{\"stream\":true}",
        [&](const std::string& line) { receivedLine = line; });

    QA_EXPECT(response.m_statusCode == 200);
    QA_EXPECT(probe.contentTypeCount() == 1);
    QA_EXPECT(probe.contentTypeValue() == "application/json");
    QA_EXPECT(probe.body() == "{\"stream\":true}");
    QA_EXPECT(receivedLine == "{\"ok\":true}");

    probe.stop();
}

} // namespace

int main() {
    struct TestCase {
        const char* name;
        void (*fn)();
    };
    const TestCase tests[] = {
        {"H1 postJson 只发一份 Content-Type", testPostJsonSingleContentType},
        {"H2 postJsonStream 只发一份 Content-Type", testPostJsonStreamSingleContentType},
    };

    for (const auto& t : tests) {
        int before = g_failures;
        t.fn();
        std::printf("[%s] %s\n", g_failures == before ? "PASS" : "FAIL", t.name);
    }
    std::printf("qa_CLFHttpClient: %d tests, %d checks, %d failures\n",
                static_cast<int>(sizeof(tests) / sizeof(tests[0])),
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
