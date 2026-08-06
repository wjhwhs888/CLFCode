// CLFRetryPolicy.hpp — HTTP 错误分类与重试策略
// 判断 API 请求错误是否致命（不应重试）或可重试（限流/服务端错误/超时）
//
// example:
//   if (CLFRetryPolicy::isFatalHttpError(err)) return error;

#pragma once

#include <string>

namespace CLF::CLFCore {

class CLFRetryPolicy {
public:
    static constexpr int kMaxRetries = 3;

    // 致命错误（请求/认证/权限问题，重试无意义）
    static bool isFatalHttpError(const std::string& err);
};

} // namespace CLF::CLFCore
