// CLFMessageCodec.hpp — 消息 JSON 编解码器
// 合并 CLFContext::serialize/restore 与 CLFSessionManager 的 JSON 序列化重复
// 统一的消息 ↔ JSON 双向转换
//
// example:
//   std::string json = CLFMessageCodec::serialize(messages);
//   auto messages = CLFMessageCodec::parse(jsonStr);

#pragma once

#include <string>
#include <vector>

#include "CLFCore/CLFTypes.hpp"

namespace CLF::CLFCore {

class CLFMessageCodec {
public:
    // 序列化消息数组为 JSON（含 version/messageCount/saved_at/title 元字段）
    // savedAt / title 为空时不写入对应字段
    static std::string serialize(const std::vector<CLFMessage>& messages,
                                  const std::string& savedAt = "",
                                  const std::string& title = "");

    // 从 JSON 解析消息数组（拦截 nlohmann 异常，失败返回空）
    static std::vector<CLFMessage> parse(const std::string& jsonData);

    // 从 JSON 解析为可选格式（带 version/saved_at/title 解析）
    static std::vector<CLFMessage> parseFull(const std::string& jsonData,
                                              int* outVersion = nullptr,
                                              std::string* outSavedAt = nullptr,
                                              std::string* outTitle = nullptr);
};

} // namespace CLF::CLFCore
