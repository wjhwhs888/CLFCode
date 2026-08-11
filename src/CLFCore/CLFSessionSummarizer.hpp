// CLFSessionSummarizer.hpp — 会话摘要生成器
// 通过 API（降级为规则）将会话对话压缩为结构化摘要
//
// example:
//   CLFSessionSummarizer summarizer(httpClient, config);
//   CLFSessionSummary summary = summarizer.generate(messages);
//   if (!summary.isEmpty()) {
//       context.addMessage("system", summary.toSystemMessage());
//   }

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "CLFTypes/CLFTypes.hpp"
#include "CLFCore/CLFProtocolAdapter.hpp"

namespace CLF::CLFNetwork { class ICLFHttpClient; }
namespace CLF::CLFCore {

class CLFSessionSummarizer {
public:
    // 依赖注入：HTTP 客户端 + 配置（只读引用，调用方保证生命周期）
    CLFSessionSummarizer(
        std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> httpClient,
        const CLFAgentConfig& config);

    // 核心方法：从消息列表生成摘要
    // API 成功 → 结构化摘要；API 失败 → 自动降级为规则提取
    CLFSessionSummary generate(const std::vector<CLFMessage>& messages);

    // 供外部判断：是否应走摘要流程（m_contextCompression 开启 + httpClient 可用）
    bool isEnabled() const;

private:
    // API 方式：构造请求 → 调 API → 解析结构化响应 → 填充摘要
    CLFSessionSummary generateViaApi(const std::vector<CLFMessage>& messages);

    // 降级方式：规则提取第一条 user 消息、工具调用名、最后提问
    CLFSessionSummary buildFallback(const std::vector<CLFMessage>& messages) const;

    // 解析 API 返回的摘要文本 → CLFSessionSummary
    CLFSessionSummary parseSummaryResponse(const std::string& responseText) const;

    std::shared_ptr<CLF::CLFNetwork::ICLFHttpClient> m_httpClient;
    const CLFAgentConfig& m_config;
    CLFProtocolAdapter m_protocolAdapter;  // 复用已有请求构造
};

} // namespace CLF::CLFCore
