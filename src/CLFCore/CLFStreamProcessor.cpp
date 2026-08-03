// CLFStreamProcessor.cpp — SSE 流式响应处理器实现

#include "CLFCore/CLFStreamProcessor.hpp"
#include "CLFCore/CLFStreamAccumulator.hpp"

#include <nlohmann/json.hpp>

namespace CLF::CLFCore {

class CLFStreamProcessor::Impl {
public:
    CLFStreamAccumulator acc;
    std::string lastChunk;
};

CLFStreamProcessor::CLFStreamProcessor()
    : m_impl(std::make_unique<Impl>()) {
}

CLFStreamProcessor::~CLFStreamProcessor() = default;

std::string CLFStreamProcessor::feedLine(const std::string& line) {
    if (m_hadError) return {};

    if (line.rfind("data: ", 0) != 0) return {};
    std::string payload = line.substr(6);

    if (payload == "[DONE]") {
        markDone();
        return {};
    }

    try {
        auto delta = nlohmann::json::parse(payload);
        if (delta.contains("error")) {
            m_hadError = true;
            m_errorMsg = delta["error"].dump();
            return {};
        }
        if (delta.contains("choices") && !delta["choices"].empty()) {
            const auto& choice = delta["choices"][0];
            if (choice.contains("delta")) {
                std::string chunk = m_impl->acc.feedDelta(choice["delta"]);
                m_impl->lastChunk = chunk;
                return chunk;
            }
            if (choice.contains("finish_reason")) {
                m_impl->acc.feedDelta(choice);
            }
        }
    } catch (const nlohmann::json::exception&) {}
    return {};
}

void CLFStreamProcessor::markDone() {
    m_impl->acc.markDone();
}

std::string CLFStreamProcessor::getContent() const {
    return m_impl->acc.getContent();
}

std::vector<CLFToolCall> CLFStreamProcessor::getToolCalls() const {
    return m_impl->acc.getToolCalls();
}

std::string CLFStreamProcessor::getFinishReason() const {
    return m_impl->acc.getFinishReason();
}

} // namespace CLF::CLFCore
