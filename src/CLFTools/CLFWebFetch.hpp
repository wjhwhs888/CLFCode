// CLFWebFetch.hpp — 网络抓取工具（模型可用）
//
// ⚠ 刻意不复用 CLFNetwork::CLFHttpClient：后者为调用 LLM 服务而设计，
//   **恒定携带 Authorization: Bearer <apiKey>**，且只有 POST、baseUrl 为单主机模型。
//   拿它去抓任意第三方 URL 会把 API key 泄漏出去。此处独立封装，不带任何凭据。
//
// example:
//   auto r = CLF::CLFTools::webFetch({"https://example.com", "GET"});
//   if (r.m_success) use(r.m_body);

#pragma once

#include <map>
#include <string>

namespace CLF::CLFTools {

struct CLFWebRequest {
    std::string m_url;
    std::string m_method = "GET";              // GET / POST / HEAD
    std::map<std::string, std::string> m_headers;
    std::string m_body;                        // 仅 POST 使用
    int         m_timeoutSec = 15;             // clamp 到 1..60
};

struct CLFWebResponse {
    bool        m_success = false;
    int         m_status  = 0;
    std::string m_headers;   // 已截断的响应头文本
    std::string m_body;      // 已截断的响应体（二进制则为提示语）
    std::string m_error;
    bool        m_truncated = false;
    bool        m_binary    = false;
};

//发起 HTTP 请求并返回截断后的响应
// 约束：响应读取上限 1MB；正文按 head 8KB + tail 2KB 截断（UTF-8 边界安全）；
//      检测到 NUL 字节则判定为二进制并跳过正文
// example:
//   CLFWebRequest req; req.m_url = "https://example.com";
//   auto resp = webFetch(req);
CLFWebResponse webFetch(const CLFWebRequest& request);

// ============================================================================
// 内部辅助——暴露仅为单测可达
// ============================================================================
namespace detail {

//拆分 URL 为 "scheme://host[:port]" 与 "/path?query"
// example:
//   splitUrl("https://a.com/x?y=1", base, path);  // base="https://a.com", path="/x?y=1"
bool splitUrl(const std::string& url, std::string& outBase, std::string& outPath);

//字节级 head/tail 截断，切点回退到 UTF-8 字符边界
// 注意：CLFTypes 的 headTailCapWithMarker 是 vector<T> 模板（按元素个数），
//      此处需要的是按**字节数**截断字符串，二者语义不同，不可复用
// example:
//   headTailCapBytes(body, 8192, 2048);
std::string headTailCapBytes(const std::string& s, size_t headBytes, size_t tailBytes);

//粗略判定内容是否为二进制（探测区内出现 NUL 字节）
// example:
//   if (looksBinary(body)) skip();
bool looksBinary(const std::string& s);

} // namespace detail

} // namespace CLF::CLFTools
