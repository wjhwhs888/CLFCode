// qa_CLFRetryPolicy.cpp — 重试策略分级单元测试（设计 S1-3）
// R1: extractHttpStatus 前缀解析（含响应体误判防护）
// R2: isFatalHttpError 致命码分类（含 404/405/409/413/422 新增项）
// R3: maxAttemptsForError 三档尝试上限
// R4: 行为变更回归钉子（404 不再重试 / 响应体内状态码不误判）

#include <boost/ut.hpp>

#include <string>

#include "CLFCore/CLFRetryPolicy.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFRetryPolicy;

const boost::ut::suite<"CLFRetryPolicy"> tests = [] {
    // ========== R1: extractHttpStatus ==========

    "R1a 纯状态码串"_test = [] {
        expect(CLFRetryPolicy::extractHttpStatus("HTTP 429") == 429_i);
        expect(CLFRetryPolicy::extractHttpStatus("HTTP 500") == 500_i);
    };

    "R1b 带响应体的状态码串（postJson 形态）"_test = [] {
        expect(CLFRetryPolicy::extractHttpStatus("HTTP 400: {\"error\":\"bad\"}") == 400_i);
        expect(CLFRetryPolicy::extractHttpStatus("HTTP 422: unprocessable") == 422_i);
    };

    "R1c 非 HTTP 错误返回 0"_test = [] {
        expect(CLFRetryPolicy::extractHttpStatus("Connection failed: timeout") == 0_i);
        expect(CLFRetryPolicy::extractHttpStatus("Stream connection failed: reset") == 0_i);
        expect(CLFRetryPolicy::extractHttpStatus("") == 0_i);
    };

    "R1d 畸形串不崩且返回 0"_test = [] {
        expect(CLFRetryPolicy::extractHttpStatus("HTTP ") == 0_i);
        expect(CLFRetryPolicy::extractHttpStatus("HTTP abc") == 0_i);
        expect(CLFRetryPolicy::extractHttpStatus("HTTP") == 0_i);
    };

    // R1e 是本次修复的关键：旧实现用 find，响应体里出现状态码字样会被误读
    "R1e 仅前缀匹配——响应体内的状态码不被误读"_test = [] {
        expect(CLFRetryPolicy::extractHttpStatus("Connection failed: proxy said HTTP 400") == 0_i);
        expect(CLFRetryPolicy::extractHttpStatus("HTTP 502: upstream returned HTTP 400") == 502_i);
    };

    // ========== R2: isFatalHttpError ==========

    "R2a 原有致命码仍致命"_test = [] {
        expect(CLFRetryPolicy::isFatalHttpError("HTTP 400: bad request"));
        expect(CLFRetryPolicy::isFatalHttpError("HTTP 401: unauthorized"));
        expect(CLFRetryPolicy::isFatalHttpError("HTTP 402: payment required"));
        expect(CLFRetryPolicy::isFatalHttpError("HTTP 403: forbidden"));
    };

    "R2b 新增致命码（本次扩展）"_test = [] {
        expect(CLFRetryPolicy::isFatalHttpError("HTTP 404: not found"));
        expect(CLFRetryPolicy::isFatalHttpError("HTTP 405: method not allowed"));
        expect(CLFRetryPolicy::isFatalHttpError("HTTP 409: conflict"));
        expect(CLFRetryPolicy::isFatalHttpError("HTTP 413: payload too large"));
        expect(CLFRetryPolicy::isFatalHttpError("HTTP 422: unprocessable entity"));
    };

    "R2c 可重试码不致命"_test = [] {
        expect(!CLFRetryPolicy::isFatalHttpError("HTTP 429: rate limited"));
        expect(!CLFRetryPolicy::isFatalHttpError("HTTP 500: internal error"));
        expect(!CLFRetryPolicy::isFatalHttpError("HTTP 502: bad gateway"));
        expect(!CLFRetryPolicy::isFatalHttpError("HTTP 503: unavailable"));
    };

    "R2d 网络类错误不致命（应走重试）"_test = [] {
        expect(!CLFRetryPolicy::isFatalHttpError("Connection failed: timeout"));
        expect(!CLFRetryPolicy::isFatalHttpError("Stream connection failed: reset"));
    };

    // ========== R3: maxAttemptsForError 三档 ==========

    "R3a 致命 → 1 次（不重试）"_test = [] {
        expect(CLFRetryPolicy::maxAttemptsForError("HTTP 400: bad") == 1_i);
        expect(CLFRetryPolicy::maxAttemptsForError("HTTP 401: no auth") == 1_i);
        expect(CLFRetryPolicy::maxAttemptsForError("HTTP 404: not found") == 1_i);
        expect(CLFRetryPolicy::maxAttemptsForError("HTTP 422: bad entity") == 1_i);
    };

    "R3b 限流与服务端错误 → kMaxRetries"_test = [] {
        expect(CLFRetryPolicy::maxAttemptsForError("HTTP 429: slow down")
               == _i(CLFRetryPolicy::kMaxRetries));
        expect(CLFRetryPolicy::maxAttemptsForError("HTTP 500: oops")
               == _i(CLFRetryPolicy::kMaxRetries));
        expect(CLFRetryPolicy::maxAttemptsForError("HTTP 599: weird")
               == _i(CLFRetryPolicy::kMaxRetries));
    };

    "R3c 其他 4xx → 2 次（重试 1 次即止）"_test = [] {
        expect(CLFRetryPolicy::maxAttemptsForError("HTTP 408: request timeout") == 2_i);
        expect(CLFRetryPolicy::maxAttemptsForError("HTTP 410: gone") == 2_i);
        expect(CLFRetryPolicy::maxAttemptsForError("HTTP 451: unavailable for legal") == 2_i);
    };

    "R3d 网络/超时 → kMaxRetries"_test = [] {
        expect(CLFRetryPolicy::maxAttemptsForError("Connection failed: timeout")
               == _i(CLFRetryPolicy::kMaxRetries));
        expect(CLFRetryPolicy::maxAttemptsForError("Stream connection failed: reset")
               == _i(CLFRetryPolicy::kMaxRetries));
    };

    // ========== R4: 行为变更钉子 ==========

    // 修复前 404 只被当作普通错误，会白白重试 3 次；修复后立即放弃
    "R4a 404 由「重试 3 次」变为「不重试」"_test = [] {
        expect(CLFRetryPolicy::isFatalHttpError("HTTP 404: not found"));
        expect(CLFRetryPolicy::maxAttemptsForError("HTTP 404: not found") == 1_i);
    };

    // 三档上限必须互不相等，否则分级失去意义
    "R4b 三档上限彼此有别"_test = [] {
        const int fatal  = CLFRetryPolicy::maxAttemptsForError("HTTP 400: bad");
        const int other4 = CLFRetryPolicy::maxAttemptsForError("HTTP 408: timeout");
        const int retry  = CLFRetryPolicy::maxAttemptsForError("HTTP 503: down");
        expect(fatal < other4);
        expect(other4 < retry);
        expect(retry == _i(CLFRetryPolicy::kMaxRetries));
    };

    "R4c kMaxRetries 常量未被意外改动"_test = [] {
        expect(CLFRetryPolicy::kMaxRetries == 3_i);
    };
};

int main() {}
