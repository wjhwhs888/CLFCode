// CLFRetryPolicy.hpp — HTTP 错误分类与重试策略
// 判断 API 请求错误是否致命（不应重试）或可重试（限流/服务端错误/超时），
// 并按错误类别给出最大总尝试次数
//
// example:
//   if (CLFRetryPolicy::isFatalHttpError(err)) return error;
//   if (++consecutiveErrors >= CLFRetryPolicy::maxAttemptsForError(err)) giveUp();

#pragma once

#include <string>

namespace CLF::CLFCore {

class CLFRetryPolicy {
public:
    // 可重试类错误的最大总尝试次数（首次 + 2 次重试）
    static constexpr int kMaxRetries = 3;

    // 致命错误（请求本身有问题，重试无意义）
    // 400 参数错误 / 401 认证失败 / 402·403 权限不足 / 404 路径不存在 /
    // 405 方法不允许 / 409 冲突 / 413 请求过大 / 422 语义错误
    static bool isFatalHttpError(const std::string& err);

    // 按错误类别返回最大总尝试次数（含首次尝试）
    // 致命 = 1（不重试）；其他 4xx = 2（重试 1 次）；429 / 5xx / 网络超时 = kMaxRetries
    static int maxAttemptsForError(const std::string& err);

    // 从错误串提取 HTTP 状态码，非 HTTP 错误（连接失败/超时）返回 0
    // 仅匹配 "HTTP <code>" 前缀，避免响应体内的数字被误读为状态码
    static int extractHttpStatus(const std::string& err);
};

} // namespace CLF::CLFCore
