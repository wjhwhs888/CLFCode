// CLFSessionManager.cpp — 会话文件管理实现
// 消息序列化 → 委托 CLFMessageCodec

#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFMessageCodec.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace CLF::CLFCore {

namespace {

std::string timestampStr() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm);
    return std::string(buf);
}

std::string extractTitle(const std::vector<CLFMessage>& messages) {
    for (const auto& msg : messages) {
        if (msg.m_role == "user" && !msg.m_content.empty()) {
            std::string title = msg.m_content;
            for (auto& c : title)
                if (c == '\n' || c == '\r') c = ' ';
            if (title.size() > 50) title = title.substr(0, 47) + "...";
            return title;
        }
    }
    return "(empty session)";
}

// 文件名安全化：替换路径分隔符和特殊字符为下划线
std::string sanitizeFilename(const std::string& input) {
    std::string out = input;
    for (auto& c : out) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"'  || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    if (out.size() > 80) out = out.substr(0, 77) + "...";
    return out;
}

} // anonymous namespace

// ============================================================================
// save — 原子写入 latest.json 或归档为时间戳.json
// ============================================================================

std::string CLFSessionManager::save(const std::vector<CLFMessage>& messages,
                                     const std::string& dirPath,
                                     bool finalize,
                                     const std::vector<std::string>& skills,
                                     const CLFSessionSummary* summary) {
    std::error_code ec;
    fs::create_directories(dirPath, ec);

    // === 归档模式：latest.json → 时间戳.json ===
    if (finalize) {
        std::string latestPath = dirPath + "/latest.json";
        if (!fs::exists(latestPath, ec)) {
            CLFLogger::instance().debug("[Save] finalize skipped: latest.json not found");
            return "";
        }

        std::string title = extractTitle(messages);
        std::string safeTitle = sanitizeFilename(title);
        std::string finalPath = dirPath + "/" + timestampStr() + "_" + safeTitle + ".json";

        // 冲突处理：如果已存在同名文件，加序号
        for (int n = 2; fs::exists(finalPath, ec); ++n) {
            finalPath = dirPath + "/" + timestampStr() + "_" + safeTitle + "-"
                      + std::to_string(n) + ".json";
        }

        CLFLogger::instance().debug("[Save] finalizing: latest.json → " + finalPath);
        fs::rename(latestPath, finalPath, ec);
        if (ec) {
            CLFLogger::instance().warn("[Save] finalize rename failed: " + ec.message());
            return "";
        }
        CLFLogger::instance().info("[Save] finalized: " + finalPath);
        return finalPath;
    }

    // === 覆盖模式：原子写入 latest.json ===
    std::string json = CLFMessageCodec::serialize(
        messages, timestampStr(), extractTitle(messages), skills, summary);
    if (json.empty()) {
        CLFLogger::instance().warn("[Save] serialize returned empty JSON");
        return "";
    }

    std::string tmpPath   = dirPath + "/latest.tmp";
    std::string finalPath = dirPath + "/latest.json";

    CLFLogger::instance().debug("[Save] writing: " + tmpPath + ", "
                                + std::to_string(json.size()) + " bytes, "
                                + std::to_string(messages.size()) + " msgs");

    // ① 写临时文件
    {
        std::ofstream file(tmpPath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file.is_open()) {
            CLFLogger::instance().warn("[Save] cannot open tmp file: " + tmpPath);
            return "";
        }
        file << json;
        file.close();
        if (file.fail()) {
            CLFLogger::instance().warn("[Save] write failed (disk full?): " + tmpPath);
            fs::remove(tmpPath, ec);
            return "";
        }
    }

    // ② 原子 rename（同文件系统上保证原子性：要么旧文件在，要么新文件在，不会出现半截文件）
    CLFLogger::instance().debug("[Save] rename: " + tmpPath + " → " + finalPath);
    fs::rename(tmpPath, finalPath, ec);
    if (ec) {
        CLFLogger::instance().warn("[Save] rename failed: " + ec.message());
        return "";
    }

    CLFLogger::instance().info("[Save] latest.json updated: "
                               + std::to_string(messages.size()) + " msgs, "
                               + std::to_string(json.size() / 1024) + " KB");
    return finalPath;
}

// ============================================================================
// load — 加载 + 损坏保护（备份 .bak，不崩溃）
// ============================================================================

bool CLFSessionManager::load(const std::string& filePath,
                              std::vector<CLFMessage>& outMessages,
                              std::vector<std::string>* outSkills,
                              CLFSessionSummary* outSummary) {
    CLFLogger::instance().debug("[Load] opening: " + filePath);

    std::ifstream file(filePath);
    if (!file.is_open()) {
        CLFLogger::instance().warn("[Load] not found or cannot open: " + filePath);
        return false;
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    std::string content = oss.str();

    if (content.empty()) {
        CLFLogger::instance().warn("[Load] empty file: " + filePath);
        return false;
    }

    outMessages = CLFMessageCodec::parseFull(oss.str(), nullptr, nullptr, nullptr,
                                              outSkills, outSummary);

    if (outMessages.empty()) {
        // 损坏文件 → 备份为 .bak，不崩溃
        std::error_code ec;
        std::string bakPath = filePath + ".bak";
        fs::rename(filePath, bakPath, ec);
        CLFLogger::instance().warn("[Load] corrupted, backed up: " + bakPath);
        return false;
    }

    CLFLogger::instance().info("[Load] success: " + filePath + ", "
                               + std::to_string(outMessages.size()) + " msgs"
                               + (outSkills ? ", " + std::to_string(outSkills->size()) + " skills" : ""));
    return true;
}

// ============================================================================
// list — 最新会话排最前，latest.json 标记 [当前]
// ============================================================================

std::vector<CLFSessionInfo> CLFSessionManager::list(const std::string& dirPath, int limit) {
    std::vector<CLFSessionInfo> result;
    std::error_code ec;

    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) return result;

    // 先检查 latest.json（放在列表最前面）
    std::string latestPath = dirPath + "/latest.json";
    if (fs::exists(latestPath, ec) && fs::is_regular_file(latestPath, ec)) {
        CLFSessionInfo info;
        info.m_path     = latestPath;
        info.m_isLatest = true;

        try {
            std::ifstream file(latestPath);
            std::ostringstream oss;
            oss << file.rdbuf();
            std::string title;
            CLFMessageCodec::parseFull(oss.str(), nullptr, &info.m_savedAt, &title);
            info.m_title = title.empty() ? "(当前会话)" : title;
        } catch (...) {
            info.m_title = "(当前会话)";
        }
        if (info.m_title.empty() || info.m_title == "(untitled)") {
            info.m_title = "(当前会话)";
        }
        result.push_back(std::move(info));
    }

    // 收集已归档的 .json 文件（排除 latest.json 和 _incomplete）
    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.size() < 5) continue;
        if (name.substr(name.size() - 5) != ".json") continue;
        if (name.find("_incomplete") != std::string::npos) continue;
        if (name == "latest.json") continue;  // 已处理
        entries.push_back(entry);
    }

    std::sort(entries.begin(), entries.end(),
        [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.last_write_time() > b.last_write_time();
        });

    int remaining = limit - static_cast<int>(result.size());
    int count = 0;
    for (const auto& entry : entries) {
        if (count++ >= remaining) break;

        CLFSessionInfo info;
        info.m_path = entry.path().string();

        try {
            std::ifstream file(info.m_path);
            std::ostringstream oss;
            oss << file.rdbuf();
            std::string title;
            CLFMessageCodec::parseFull(oss.str(), nullptr, &info.m_savedAt, &title);
            info.m_title = title.empty() ? "(untitled)" : title;
        } catch (...) {
            info.m_title = entry.path().stem().string();
        }
        if (info.m_title.empty() || info.m_title == "(untitled)") {
            std::ifstream file(info.m_path);
            std::ostringstream oss;
            oss << file.rdbuf();
            auto msgs = CLFMessageCodec::parse(oss.str());
            info.m_title = extractTitle(msgs);
        }
        result.push_back(std::move(info));
    }

    return result;
}

// ============================================================================
// 旧版兼容方法（保留以支持测试，新代码不应使用）
// ============================================================================

std::string CLFSessionManager::findIncomplete(const std::string& dirPath) {
    std::error_code ec;
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) return "";

    std::string newest;
    fs::file_time_type newestTime{};
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.find("_incomplete.json") == std::string::npos) continue;

        auto time = entry.last_write_time(ec);
        if (newest.empty() || time > newestTime) {
            newest = entry.path().string();
            newestTime = time;
        }
    }
    return newest;
}

int CLFSessionManager::removeAllIncomplete(const std::string& dirPath) {
    std::error_code ec;
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) return 0;

    int removed = 0;
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.find("_incomplete.json") != std::string::npos) {
            if (fs::remove(entry.path(), ec)) ++removed;
        }
    }
    return removed;
}

std::string CLFSessionManager::promote(const std::string& incompletePath) {
    fs::path p(incompletePath);
    std::string name = p.filename().string();
    if (name.find("_incomplete.json") == std::string::npos) return incompletePath;

    std::string newName = name.substr(0, name.size() - std::string("_incomplete.json").size())
                        + ".json";
    fs::path newPath = p.parent_path() / newName;

    std::error_code ec;
    fs::rename(p, newPath, ec);
    return ec ? "" : newPath.string();
}

// ============================================================================
// migrateLegacyIncomplete — 启动时迁移旧版 _incomplete.json
// ============================================================================

void CLFSessionManager::migrateLegacyIncomplete(const std::string& dirPath) {
    std::error_code ec;
    if (!fs::exists(dirPath, ec)) return;

    std::string newestPath;
    fs::file_time_type newestTime;
    int count = 0;

    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.find("_incomplete.json") == std::string::npos) continue;

        ++count;
        auto t = entry.last_write_time(ec);
        if (newestPath.empty() || t > newestTime) {
            // 删除之前找到的"最新"（有更新的了）
            if (!newestPath.empty()) fs::remove(newestPath, ec);
            newestPath = entry.path().string();
            newestTime = t;
        } else {
            // 更旧的直接删除
            fs::remove(entry.path(), ec);
        }
    }

    if (!newestPath.empty()) {
        std::string target = dirPath + "/latest.json";
        // 如果已有 latest.json，保留它，删除旧的 incomplete
        if (fs::exists(target, ec)) {
            fs::remove(newestPath, ec);
            CLFLogger::instance().info("[Migrate] removed legacy incomplete (latest.json already exists)");
        } else {
            fs::rename(newestPath, target, ec);
            CLFLogger::instance().info("[Migrate] legacy _incomplete → latest.json ("
                                       + std::to_string(count) + " files processed)");
        }
    }
}

// ============================================================================
// remove / cleanupOld
// ============================================================================

bool CLFSessionManager::remove(const std::string& filePath) {
    std::error_code ec;
    return fs::remove(filePath, ec);
}

int CLFSessionManager::cleanupOld(const std::string& dirPath, int maxAgeDays) {
    std::error_code ec;
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) return 0;

    auto cutoff = fs::file_time_type::clock::now()
                - std::chrono::hours(24 * maxAgeDays);
    int removed = 0;

    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.size() < 5) continue;
        if (name.substr(name.size() - 5) != ".json") continue;
        // 不要清理 latest.json
        if (name == "latest.json") continue;

        if (entry.last_write_time(ec) < cutoff) {
            if (fs::remove(entry.path(), ec)) ++removed;
        }
    }
    return removed;
}

} // namespace CLF::CLFCore
