// CLFRetryPolicy.cpp — HTTP 错误分类实现

#include "CLFCore/CLFRetryPolicy.hpp"

#include <cctype>

namespace CLF::CLFCore {

int CLFRetryPolicy::extractHttpStatus(const std::string& err) {
    // 只认前缀形态："HTTP 429" / "HTTP 400: {...}"。
    // 用 find 会把响应体里的 "HTTP 500" 之类文本误当成状态码，故限定前缀。
    static const std::string kPrefix = "HTTP ";
    if (err.rfind(kPrefix, 0) != 0) return 0;

    size_t i    = kPrefix.size();
    int    code = 0;
    size_t digitStart = i;
    while (i < err.size() && std::isdigit(static_cast<unsigned char>(err[i]))) {
        code = code * 10 + (err[i] - '0');
        ++i;
    }
    return (i > digitStart) ? code : 0;
}

bool CLFRetryPolicy::isFatalHttpError(const std::string& err) {
    switch (extractHttpStatus(err)) {
        case 400:  // 请求参数错误
        case 401:  // 认证失败
        case 402:  // 需付费
        case 403:  // 权限不足
        case 404:  // 路径不存在
        case 405:  // 方法不允许
        case 409:  // 冲突
        case 413:  // 请求体过大
        case 422:  // 语义错误
            return true;
        default:
            return false;
    }
}

int CLFRetryPolicy::maxAttemptsForError(const std::string& err) {
    const int code = extractHttpStatus(err);

    if (isFatalHttpError(err)) return 1;                       // 重试无意义
    if (code == 429) return kMaxRetries;                       // 限流：退避后有望成功
    if (code >= 500 && code < 600) return kMaxRetries;         // 服务端错误：可能是瞬时的
    if (code >= 400 && code < 500) return 2;                   // 其他 4xx：给一次机会即止
    return kMaxRetries;                                        // 连接失败/超时等非 HTTP 错误
}

} // namespace CLF::CLFCore
