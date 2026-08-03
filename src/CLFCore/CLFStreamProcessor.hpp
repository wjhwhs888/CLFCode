// CLFStreamProcessor.hpp — SSE 流式响应处理器
// 管理 CLFStreamAccumulator 生命周期，处理逐行 SSE 数据
//
// example:
//   CLFStreamProcessor processor;
//   httpClient->postJsonStream(path, body,
//       [&](const std::string& line) { processor.feedLine(line); });
//   auto parsed = processor.finish();

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "CLFCore/CLFTypes.hpp"

namespace CLF::CLFCore {

class CLFStreamAccumulator;

class CLFStreamProcessor {
public:
    CLFStreamProcessor();
    ~CLFStreamProcessor();

    // 处理一行 SSE 数据（"data: {json}" 或 "data: [DONE]"）
    // 返回需要实时输出的文本 chunk
    std::string feedLine(const std::string& line);

    // 完成累积，最终化 tool_calls
    void markDone();

    // 获取累积结果
    std::string getContent() const;
    std::vector<CLFToolCall> getToolCalls() const;
    std::string getFinishReason() const;

    // 是否有错误
    bool hadError() const { return m_hadError; }
    void setError(const std::string& msg) { m_hadError = true; m_errorMsg = msg; }
    const std::string& errorMsg() const { return m_errorMsg; }

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_hadError = false;
    std::string m_errorMsg;
};

} // namespace CLF::CLFCore
