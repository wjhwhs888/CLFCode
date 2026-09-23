// CLFWebFetch.cpp — 网络抓取实现（独立于 CLFHttpClient，不携带任何凭据）

#include "CLFTools/CLFWebFetch.hpp"

#include <algorithm>
#include <httplib.h>

namespace CLF::CLFTools {

namespace detail {

bool splitUrl(const std::string& url, std::string& outBase, std::string& outPath) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    if (schemeEnd + 3 >= url.size()) return false;

    const auto pathStart = url.find('/', schemeEnd + 3);
    if (pathStart == std::string::npos) {
        outBase = url;
        outPath = "/";
    } else {
        outBase = url.substr(0, pathStart);
        outPath = url.substr(pathStart);
    }
    return !outBase.empty();
}

std::string headTailCapBytes(const std::string& s, size_t headBytes, size_t tailBytes) {
    if (s.size() <= headBytes + tailBytes) return s;

    // head 切点：若落在多字节字符中间（continuation byte 形如 10xxxxxx），
    // 向前回退到该字符起始，保证前半段是完整的 UTF-8 序列
    size_t h = headBytes;
    while (h > 0 && (static_cast<unsigned char>(s[h]) & 0xC0) == 0x80) --h;

    // tail 起点：同理向后前进到下一个字符起始
    size_t t = s.size() - tailBytes;
    while (t < s.size() && (static_cast<unsigned char>(s[t]) & 0xC0) == 0x80) ++t;
    if (t < h) t = h;   // 极端参数下防止区间反转

    return s.substr(0, h)
         + "\n...[中间省略 " + std::to_string(t - h) + " 字节]...\n"
         + s.substr(t);
}

bool looksBinary(const std::string& s) {
    // 只探测前 8KB：足以识别常见二进制格式，又不必扫全量
    const size_t probe = std::min<size_t>(s.size(), 8192);
    return s.find('\0') < probe;
}

} // namespace detail

namespace {

constexpr size_t kMaxResponseBytes = 1024 * 1024;  // 1MB 读取上限
constexpr size_t kHeadBytes        = 8 * 1024;
constexpr size_t kTailBytes        = 2 * 1024;
constexpr int    kMinTimeoutSec    = 1;
constexpr int    kMaxTimeoutSec    = 60;

// 响应头 → 文本（仅取常用字段，避免噪声）
std::string formatHeaders(const httplib::Headers& headers) {
    std::string out;
    for (const auto& [key, value] : headers) {
        out += key;
        out += ": ";
        out += value;
        out += '\n';
        if (out.size() > 2048) { out += "...[响应头已截断]\n"; break; }
    }
    return out;
}

} // anonymous namespace

CLFWebResponse webFetch(const CLFWebRequest& request,
                        const std::function<bool()>& isCancelled) {
    CLFWebResponse result;

    // A2 入口检查（2026-09-23 中断时效性 §十二）：请求发出前已取消 → 立即返回
    if (isCancelled && isCancelled()) {
        result.m_error = "请求被用户中断";
        return result;
    }

    std::string base, path;
    if (!detail::splitUrl(request.m_url, base, path)) {
        result.m_error = "URL 无效（需形如 https://host/path）: " + request.m_url;
        return result;
    }

    int timeout = request.m_timeoutSec;
    timeout = std::max(kMinTimeoutSec, std::min(kMaxTimeoutSec, timeout));

    httplib::Client cli(base);
    cli.set_connection_timeout(timeout, 0);
    cli.set_read_timeout(timeout, 0);
    cli.set_follow_location(true);

    // ⚠ 此处**不注入任何 Authorization/凭据**，只带调用方显式给的头
    httplib::Headers headers;
    for (const auto& [key, value] : request.m_headers) {
        headers.emplace(key, value);
    }

    std::string method = request.m_method;
    std::transform(method.begin(), method.end(), method.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    // A2：content receiver 累积 body（1MB 上限随读随裁）+ 每块取消检查——
    // 传输期可中断（返回 false 中止接收）；等首字节窗口由 read_timeout 兜底
    std::string body;
    bool interrupted = false;
    auto receiver = [&](const char* data, size_t len) -> bool {
        if (isCancelled && isCancelled()) {
            interrupted = true;
            return false;
        }
        if (body.size() < kMaxResponseBytes) {
            const size_t room = kMaxResponseBytes - body.size();
            body.append(data, std::min(len, room));
            if (len > room) result.m_truncated = true;
        } else if (len > 0) {
            result.m_truncated = true;
        }
        return true;
    };

    httplib::Result res(nullptr, httplib::Error::Unknown);
    if (method == "GET") {
        res = cli.Get(path, headers, receiver);
    } else if (method == "HEAD") {
        res = cli.Head(path, headers);   // HEAD 无响应体，无 receiver 重载
    } else if (method == "POST") {
        res = cli.Post(path, headers, request.m_body,
                       request.m_body.empty() ? "text/plain" : "application/json",
                       receiver);
    } else {
        result.m_error = "不支持的 method（仅 GET/POST/HEAD）: " + request.m_method;
        return result;
    }

    if (interrupted) {
        result.m_error = "请求被用户中断";
        return result;
    }
    if (!res) {
        result.m_error = "请求失败: " + httplib::to_string(res.error());
        return result;
    }

    result.m_success = true;
    result.m_status  = res->status;
    result.m_headers = formatHeaders(res->headers);

    if (detail::looksBinary(body)) {
        result.m_binary = true;
        result.m_body   = "[二进制内容，已跳过（" + std::to_string(body.size()) + " 字节）]";
        return result;
    }

    const std::string capped = detail::headTailCapBytes(body, kHeadBytes, kTailBytes);
    if (capped.size() != body.size()) result.m_truncated = true;
    result.m_body = capped;
    return result;
}

} // namespace CLF::CLFTools
