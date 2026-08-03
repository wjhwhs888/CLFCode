// CLFErrorCodes.hpp — 结构化错误码
// 替代字符串子串匹配，供 CLFRetryPolicy / CLFHttpClient / CLFProtocolAdapter 统一使用

#pragma once

namespace CLF::CLFCore {

enum class CLFError {
    OK = 0,
    NetworkTimeout,    // 网络超时
    NetworkConnect,    // 连接失败
    HttpStatus,        // HTTP 非 2xx
    HttpAuth,          // 认证失败 (401)
    HttpPermission,    // 权限不足 (402/403)
    HttpRateLimit,     // 限流 (429)
    HttpServerError,   // 服务端错误 (5xx)
    HttpBadRequest,    // 请求参数错误 (400)
    StreamAborted,     // 流式连接中止
    JsonParse,         // JSON 解析失败
    JsonTypeError,     // JSON 字段类型错误
    InvalidUtf8,       // 无效 UTF-8
    CommandTimeout,    // 命令执行超时
    CommandFailed,     // 命令执行失败
    FileNotFound,      // 文件未找到
    FileAccessDenied,  // 文件访问拒绝
    SecurityBlocked,   // 安全策略阻断
    ToolNotFound,      // 工具未注册
    InternalError      // 内部错误
};

// 从 HTTP 状态码推导错误类型
inline CLFError httpStatusToError(int statusCode) {
    if (statusCode == 400) return CLFError::HttpBadRequest;
    if (statusCode == 401) return CLFError::HttpAuth;
    if (statusCode == 402 || statusCode == 403) return CLFError::HttpPermission;
    if (statusCode == 429) return CLFError::HttpRateLimit;
    if (statusCode >= 500) return CLFError::HttpServerError;
    return CLFError::HttpStatus;
}

} // namespace CLF::CLFCore
