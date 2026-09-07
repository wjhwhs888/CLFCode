// CLFContextWindow.cpp — 消息窗口截断策略实现
// C2b 拆分：截断逻辑自 CLFContext::getMessages 原样搬移（行为保真）

#include "CLFCore/CLFContextWindow.hpp"

#include <algorithm>

#include "CLFTypes/CLFTextUtil.hpp"

namespace CLF::CLFCore {

std::vector<CLFMessage> CLFContextWindow::apply(
    const std::vector<CLFMessage>& messages) const {
    std::vector<CLFMessage> result;
    std::vector<CLFMessage> nonSystem;
    int tokenCount = 0;

    for (const auto& msg : messages) {
        if (msg.m_role == "system") {
            result.push_back(msg);
            tokenCount += CLFTextUtil::estimateTokensForMessage(msg);
        } else {
            nonSystem.push_back(msg);
        }
    }

    std::vector<CLFMessage> truncated;
    for (auto it = nonSystem.rbegin(); it != nonSystem.rend(); ++it) {
        int msgTokens = CLFTextUtil::estimateTokensForMessage(*it);
        if (tokenCount + msgTokens > m_maxTokens && !truncated.empty()) {
            break;
        }
        tokenCount += msgTokens;
        truncated.push_back(*it);
    }

    std::reverse(truncated.begin(), truncated.end());
    result.insert(result.end(), truncated.begin(), truncated.end());
    return result;
}

} // namespace CLF::CLFCore
