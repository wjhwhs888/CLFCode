// CLFSessionManager.cpp — 会话文件管理实现
// 消息序列化 → 委托 CLFMessageCodec

#include "CLFCore/CLFSessionManager.hpp"
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

} // anonymous namespace

std::string CLFSessionManager::save(const std::vector<CLFMessage>& messages,
                                    const std::string& dirPath,
                                    bool incomplete) {
    std::error_code ec;
    fs::create_directories(dirPath, ec);

    std::string suffix = incomplete ? "_incomplete.json" : ".json";
    std::string baseName = timestampStr();
    std::string filePath = dirPath + "/" + baseName + suffix;
    for (int n = 2; fs::exists(filePath, ec); ++n) {
        filePath = dirPath + "/" + baseName + "-" + std::to_string(n) + suffix;
    }

    // 委托 CLFMessageCodec 序列化
    std::string json = CLFMessageCodec::serialize(
        messages, timestampStr(), extractTitle(messages));

    std::ofstream file(filePath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) return "";
    file << json;
    return filePath;
}

bool CLFSessionManager::load(const std::string& filePath,
                             std::vector<CLFMessage>& outMessages) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    std::ostringstream oss;
    oss << file.rdbuf();
    outMessages = CLFMessageCodec::parse(oss.str());
    return !outMessages.empty();
}

std::vector<CLFSessionInfo> CLFSessionManager::list(const std::string& dirPath, int limit) {
    std::vector<CLFSessionInfo> result;
    std::error_code ec;

    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) return result;

    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.size() < 5) continue;
        if (name.substr(name.size() - 5) != ".json") continue;
        if (name.find("_incomplete") != std::string::npos) continue;
        entries.push_back(entry);
    }

    std::sort(entries.begin(), entries.end(),
        [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.last_write_time() > b.last_write_time();
        });

    int count = 0;
    for (const auto& entry : entries) {
        if (count++ >= limit) break;

        CLFSessionInfo info;
        info.m_path = entry.path().string();

        // 只读 title（轻量解析）
        std::string title;
        try {
            std::ifstream file(info.m_path);
            std::ostringstream oss;
            oss << file.rdbuf();
            CLFMessageCodec::parseFull(oss.str(), nullptr, &info.m_savedAt, &title);
            info.m_title = title.empty() ? "(untitled)" : title;
        } catch (...) {
            info.m_title = entry.path().stem().string();
        }
        if (info.m_title.empty() || info.m_title == "(untitled)") {
            // fallback：尝试 extractTitle 逻辑
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

bool CLFSessionManager::remove(const std::string& filePath) {
    std::error_code ec;
    return fs::remove(filePath, ec);
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

        if (entry.last_write_time(ec) < cutoff) {
            if (fs::remove(entry.path(), ec)) ++removed;
        }
    }
    return removed;
}

} // namespace CLF::CLFCore
