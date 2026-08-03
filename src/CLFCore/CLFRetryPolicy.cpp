// CLFRetryPolicy.cpp — HTTP 错误分类实现

#include "CLFCore/CLFRetryPolicy.hpp"

namespace CLF::CLFCore {

bool CLFRetryPolicy::isFatalHttpError(const std::string& err) {
    // 400 = 请求参数错误   401 = 认证失败   402/403 = 权限不足
    return err.find("HTTP 400") != std::string::npos
        || err.find("HTTP 401") != std::string::npos
        || err.find("HTTP 402") != std::string::npos
        || err.find("HTTP 403") != std::string::npos;
}

bool CLFRetryPolicy::isRetryableError(const std::string& err) {
    return err.find("HTTP 429") != std::string::npos
        || err.find("HTTP 5")   != std::string::npos
        || err.find("Connection failed") != std::string::npos
        || err.find("timeout")  != std::string::npos
        || err.find("Timeout")  != std::string::npos;
}

} // namespace CLF::CLFCore
