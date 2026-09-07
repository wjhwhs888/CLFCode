// CLFContextWindow.hpp — 消息窗口截断策略（C2b：P0-8 后半收尾，2026-09-07）
// 自 CLFContext::getMessages 抽取：CLFContext 收窄为纯容器后，
// "发 API 前按 token 预算从尾部保留"的窗口策略独立于此。
// 规则：system 消息永不截断；非 system 消息从尾部保留（token 预算）。
//
// example:
//   CLFContextWindow window(config.m_maxContextWindow);
//   auto messages = window.apply(context.getMessages());  // 发 API 前调用

#pragma once

#include <vector>

#include "CLFTypes/CLFTypes.hpp"

namespace CLF::CLFCore {

class CLFContextWindow {
public:
    explicit CLFContextWindow(int maxTokens) : m_maxTokens(maxTokens) {}

    // 窗口截断：system 全保留（前置）；非 system 从尾部按 token 预算保留，
    // 超预算的旧消息丢弃（保证新消息优先，头部截断不丢尾部）
    std::vector<CLFMessage> apply(const std::vector<CLFMessage>& messages) const;

private:
    int m_maxTokens;
};

} // namespace CLF::CLFCore
