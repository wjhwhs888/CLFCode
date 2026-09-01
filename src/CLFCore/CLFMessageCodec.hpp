// CLFMessageCodec.hpp — 消息 JSON 编解码器
// 合并 CLFContext::serialize/restore 与 CLFSessionManager 的 JSON 序列化重复
// 统一的消息 ↔ JSON 双向转换
//
// example:
//   std::string json = CLFMessageCodec::serialize(messages);
//   auto messages = CLFMessageCodec::parse(jsonStr);

#pragma once

#include <nlohmann/json.hpp>

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

    // —— jsonl 行编解码（设计-会话追加式保存.jsonl.md §3.2/§3.9，2026-09-02）——
    // 每行一个自包含 JSON 对象，"type" 字段区分（header/turn/todo_snapshot/complete/summary）
    // 序列化失败返回空串（调用方 warn + 跳过）；解析失败或 type 不匹配返回 false
    // 行解析收 nlohmann::json 对象（loadJsonl 逐行 parse 后分发，避免二次解析）
    // example:
    //   std::string line = CLFMessageCodec::serializeTurnLine(msgs, ts, &todos);
    //   std::vector<CLFMessage> out; bool ok = CLFMessageCodec::parseTurnLine(obj, out);
    static std::string serializeHeaderLine(const std::string& title,
                                           const std::string& startedAt,
                                           const std::string& sessionId,
                                           const std::string& model,
                                           const std::vector<std::string>& skills = {});
    static std::string serializeTurnLine(const std::vector<CLFMessage>& messages,
                                         const std::string& ts,
                                         const std::vector<CLFTodoItem>* todos = nullptr);
    static std::string serializeTodoSnapshot(const std::vector<CLFTodoItem>& todos,
                                             const std::string& ts);
    static std::string serializeCompleteLine(const std::vector<CLFTodoItem>& todos,
                                             const std::string& ts);
    static std::string serializeSummaryLine(const CLFSessionSummary& summary,
                                            const std::string& ts);

    static bool parseTurnLine(const nlohmann::json& obj,
                              std::vector<CLFMessage>& outMessages,
                              std::vector<CLFTodoItem>* outTodos = nullptr,
                              std::string* outTs = nullptr);
    static bool parseTodoSnapshotLine(const nlohmann::json& obj,
                                      std::vector<CLFTodoItem>& outTodos);
    static bool parseCompleteLine(const nlohmann::json& obj,
                                  std::vector<CLFTodoItem>& outTodos);
    static bool parseSummaryLine(const nlohmann::json& obj,
                                 CLFSessionSummary& outSummary);
    static bool parseHeaderLine(const nlohmann::json& obj,
                                std::string* outTitle = nullptr,
                                std::string* outStartedAt = nullptr,
                                std::string* outSessionId = nullptr,
                                std::string* outModel = nullptr,
                                std::vector<std::string>* outSkills = nullptr);
};

} // namespace CLF::CLFCore
