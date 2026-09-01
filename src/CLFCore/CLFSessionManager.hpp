// CLFSessionManager.hpp — 会话文件管理
// 负责会话的保存/加载/列表/清理（doc/contextHistory/ 目录）
//
// 保存模型：
//   每轮回合后 → latest.json（原子写入 .tmp → rename）
//   /exit /clear → latest.json 重命名为 时间戳.json（归档）
//   关窗/崩溃 → latest.json 保留，内容为最后一条完整对话
//
// example:
//   std::string dir = "doc/contextHistory";
//   std::string path = CLFSessionManager::save(messages, dir, false); // 存 latest
//   std::string path = CLFSessionManager::save(messages, dir, true);  // 归档
//   CLFSessionManager::migrateLegacyIncomplete(dir);                  // 启动时迁移旧文件

#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "CLFTypes/CLFTypes.hpp"

namespace CLF::CLFCore {

// 会话摘要（/history 列表用）
struct CLFSessionInfo {
    std::string m_path;      // 文件路径
    std::string m_title;     // 标题（首条 user 消息，截断 50 字符）
    std::string m_savedAt;   // 保存时间
    bool        m_isLatest = false; // 是否为当前会话（latest.json）
};

class CLFSessionManager {
public:
    // 保存会话
    // finalize=false: 保存到 latest.json（原子写入 .tmp → rename，每轮回合后调用）
    // finalize=true:  重命名 latest.json → 时间戳.json（/exit 和 /clear 时调用）
    // skills: 已加载的知识库名称列表，写入 JSON 的 skills 字段
    // summary: 会话摘要，写入 JSON 的 summary 对象（nullptr 或 m_valid=false 时跳过）
    // 返回文件路径，失败返回空串
    // todos: 待办清单（S2-6），随会话一起持久化
    static std::string save(const std::vector<CLFMessage>& messages,
                            const std::string& dirPath,
                            bool finalize,
                            const std::vector<std::string>& skills = {},
                            const CLFSessionSummary* summary = nullptr,
                            const std::vector<CLFTodoItem>& todos = {});

    // 从文件加载消息
    // 返回 false = 文件不存在/格式损坏/内容为空
    // 损坏时自动备份为 .bak，不崩溃
    // outSkills / outSummary: 解析对应字段（nullptr = 不读取）
    static bool load(const std::string& filePath,
                     std::vector<CLFMessage>& outMessages,
                     std::vector<std::string>* outSkills = nullptr,
                     CLFSessionSummary* outSummary = nullptr,
                     std::vector<CLFTodoItem>* outTodos = nullptr);

    // 列出会话（按修改时间倒序，limit 条）
    // activeFilePath：当前活跃会话文件路径（nullptr = 无活跃文件，如启动时）
    //   [当前] 标记（m_isLatest=true）重定义（设计-会话追加式保存.jsonl §3.9，2026-09-02）：
    //   - 活跃文件路径匹配某个 .jsonl → 该文件标 [当前]
    //   - activeFilePath 为空且目录存在旧 latest.json → latest.json 标 [当前]（旧版兼容期）
    //   - 两者都有时以活跃文件为准，latest.json 作普通归档参与排序
    static std::vector<CLFSessionInfo> list(const std::string& dirPath, int limit,
                                            const std::string* activeFilePath = nullptr);

    // —— jsonl 追加式保存（设计-会话追加式保存.jsonl.md §3.2/§3.9，2026-09-02）——
    // line 为已序列化的完整行文本（不含换行，由 CLFMessageCodec 行函数产出）
    // 每个 append*：static mutex（防御性，§3.8）→ ios::app 打开 → 写 → flush → 关闭
    // 返回 false = 打开/写入失败（warn 日志，不抛异常）；空行直接返回 false
    // example:
    //   CLFSessionManager::appendHeader(path, CLFMessageCodec::serializeHeaderLine(...));
    static bool appendHeader(const std::string& jsonlPath, const std::string& line);
    static bool appendTurn(const std::string& jsonlPath, const std::string& line);
    static bool appendTodoSnapshot(const std::string& jsonlPath, const std::string& line);
    static bool appendComplete(const std::string& jsonlPath, const std::string& line);
    static bool appendSummary(const std::string& jsonlPath, const std::string& line);

    // 逐行解析 jsonl 会话文件（设计 §3.4.2，2026-09-02）
    // turn 行 messages 按行序并入 outMessages；header 行元数据 → outHeaderInfo
    // 恢复取值优先级（outTodos）：最后一条可解析 todo_snapshot > 最后带 todos 的 turn 行
    // outSummary：最后一条可解析 summary 行；outCompleteTodos：最后一条 complete 行的清单（回显用，J6）
    // 损坏行/不完整尾行：跳过（warn 日志），不整体失败
    // 返回 false = 文件不存在/打开失败
    static bool loadJsonl(const std::string& filePath,
                          std::vector<CLFMessage>& outMessages,
                          std::vector<std::string>* outSkills = nullptr,
                          CLFSessionSummary* outSummary = nullptr,
                          std::vector<CLFTodoItem>* outTodos = nullptr,
                          std::vector<CLFTodoItem>* outCompleteTodos = nullptr,
                          CLFSessionInfo* outHeaderInfo = nullptr);

    // —— 旧版兼容（保留以支持测试，新代码不应使用） ——
    static std::string findIncomplete(const std::string& dirPath);
    static int removeAllIncomplete(const std::string& dirPath);
    static std::string promote(const std::string& incompletePath);

    // —— 迁移 ——
    // 将旧版 _incomplete.json 迁移为 latest.json（保留最新的一个，删除其余）
    static void migrateLegacyIncomplete(const std::string& dirPath);

    // —— 清理 ——
    static bool remove(const std::string& filePath);
    // 清理过期会话（.json 与 .jsonl 一并处理）
    // activeFilePath：活跃会话文件——清理是删除动作，绝不能删正在写的会话（设计 §6 边界表）
    static int cleanupOld(const std::string& dirPath, int maxAgeDays,
                          const std::string* activeFilePath = nullptr);
};

} // namespace CLF::CLFCore
