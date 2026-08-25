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
    // latest.json 排在最前面，标记 m_isLatest=true
    static std::vector<CLFSessionInfo> list(const std::string& dirPath, int limit);

    // —— 旧版兼容（保留以支持测试，新代码不应使用） ——
    static std::string findIncomplete(const std::string& dirPath);
    static int removeAllIncomplete(const std::string& dirPath);
    static std::string promote(const std::string& incompletePath);

    // —— 迁移 ——
    // 将旧版 _incomplete.json 迁移为 latest.json（保留最新的一个，删除其余）
    static void migrateLegacyIncomplete(const std::string& dirPath);

    // —— 清理 ——
    static bool remove(const std::string& filePath);
    static int cleanupOld(const std::string& dirPath, int maxAgeDays);
};

} // namespace CLF::CLFCore
