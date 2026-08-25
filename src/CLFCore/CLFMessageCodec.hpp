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

#include "CLFTypes/CLFTypes.hpp"

namespace CLF::CLFCore {

class CLFMessageCodec {
public:
    // 序列化消息数组为 JSON（含 version/messageCount/saved_at/title 元字段）
    // skills: 已加载的知识库名称列表，非空时写入 "skills" 数组
    // summary: 会话摘要，非空且 m_valid 时写入 "summary" 对象
    // savedAt / title 为空时不写入对应字段
    // todos: 待办清单（S2-6），非空时写入 "todos" 数组
    static std::string serialize(const std::vector<CLFMessage>& messages,
                                  const std::string& savedAt = "",
                                  const std::string& title = "",
                                  const std::vector<std::string>& skills = {},
                                  const CLFSessionSummary* summary = nullptr,
                                  const std::vector<CLFTodoItem>& todos = {});

    // 从 JSON 解析消息数组（拦截 nlohmann 异常，失败返回空）
    static std::vector<CLFMessage> parse(const std::string& jsonData);

    // 从 JSON 解析为可选格式（带 version/saved_at/title/skills/summary 解析）
    // outSkills / outSummary: 解析对应字段（nullptr = 不读取）
    static std::vector<CLFMessage> parseFull(const std::string& jsonData,
                                              int* outVersion = nullptr,
                                              std::string* outSavedAt = nullptr,
                                              std::string* outTitle = nullptr,
                                              std::vector<std::string>* outSkills = nullptr,
                                              CLFSessionSummary* outSummary = nullptr,
                                              std::vector<CLFTodoItem>* outTodos = nullptr);
};

} // namespace CLF::CLFCore
