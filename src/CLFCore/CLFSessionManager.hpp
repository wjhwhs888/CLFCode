// CLFSessionManager.hpp — 会话文件管理
// 负责会话的保存/加载/列表/清理（doc/contextHistory/ 目录）
//
// example:
//   std::string dir = "doc/contextHistory";
//   std::string path = CLFSessionManager::save(messages, dir, true); // 存 incomplete
//   auto info = CLFSessionManager::list(dir, 10);                    // /history
//   std::string inc = CLFSessionManager::findIncomplete(dir);        // 崩溃恢复检测

#pragma once

#include <string>
#include <vector>

#include "CLFCore/CLFContext.hpp"

namespace CLF::CLFCore {

// 会话摘要（/history 列表用）
struct CLFSessionInfo {
    std::string m_path;      // 文件路径
    std::string m_title;     // 标题（首条 user 消息，截断 50 字符）
    std::string m_savedAt;   // 保存时间
    bool        m_incomplete = false; // 是否未完成会话
};

class CLFSessionManager {
public:
    // 保存会话到新文件（文件名带时间戳），返回文件路径
    // incomplete=true 时文件名带 _incomplete 后缀
    static std::string save(const std::vector<CLFMessage>& messages,
                            const std::string& dirPath,
                            bool incomplete);

    // 从文件加载消息（返回 false = 文件不存在/格式损坏）
    static bool load(const std::string& filePath,
                     std::vector<CLFMessage>& outMessages);

    // 列出会话（按修改时间倒序，limit 条）
    static std::vector<CLFSessionInfo> list(const std::string& dirPath, int limit);

    // 查找未完成会话（*_incomplete.json），返回路径（无则空串）
    static std::string findIncomplete(const std::string& dirPath);

    // 删除全部未完成会话文件，返回删除数量
    static int removeAllIncomplete(const std::string& dirPath);

    // 删除会话文件
    static bool remove(const std::string& filePath);

    // 将 incomplete 会话转正（去掉 _incomplete 后缀），返回新路径
    static std::string promote(const std::string& incompletePath);

    // 清理超过 maxAgeDays 天的会话文件，返回删除数量
    static int cleanupOld(const std::string& dirPath, int maxAgeDays);
};

} // namespace CLF::CLFCore
