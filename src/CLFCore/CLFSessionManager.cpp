// CLFSessionManager.cpp — 会话文件管理实现

#include "CLFCore/CLFSessionManager.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace CLF::CLFCore {

namespace {

// 当前时间字符串 "YYYY-MM-DD_HH-MM-SS"
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

// 从消息提取标题（首条 user 消息，截断 50 字符）
std::string extractTitle(const std::vector<CLFMessage>& messages) {
    for (const auto& msg : messages) {
        if (msg.m_role == "user" && !msg.m_content.empty()) {
            std::string title = msg.m_content;
            // 压缩换行
            for (auto& c : title) {
                if (c == '\n' || c == '\r') c = ' ';
            }
            if (title.size() > 50) {
                title = title.substr(0, 47) + "...";
            }
            return title;
        }
    }
    return "(empty session)";
}

// 从 JSON 解析消息数组（与 CLFContext::restore 同一格式）
std::vector<CLFMessage> parseMessages(const nlohmann::json& data) {
    std::vector<CLFMessage> result;
    if (!data.contains("messages") || !data["messages"].is_array()) {
        return result;
    }
    for (const auto& m : data["messages"]) {
        if (!m.contains("role") || !m.contains("content")) continue;

        CLFMessage msg;
        msg.m_role    = m["role"].get<std::string>();
        msg.m_content = m["content"].get<std::string>();
        if (m.contains("tool_call_id") && m["tool_call_id"].is_string()) {
            msg.m_toolCallId = m["tool_call_id"].get<std::string>();
        }
        if (m.contains("name") && m["name"].is_string()) {
            msg.m_name = m["name"].get<std::string>();
        }
        if (m.contains("tool_calls") && m["tool_calls"].is_array()) {
            for (const auto& tc : m["tool_calls"]) {
                CLFToolCall call;
                call.m_id        = tc.value("id", "");
                call.m_name      = tc.value("name", "");
                call.m_arguments = tc.value("arguments", "");
                msg.m_toolCalls.push_back(std::move(call));
            }
        }
        result.push_back(std::move(msg));
    }
    return result;
}

} // anonymous namespace

std::string CLFSessionManager::save(const std::vector<CLFMessage>& messages,
                                    const std::string& dirPath,
                                    bool incomplete) {
    std::error_code ec;
    fs::create_directories(dirPath, ec);

    std::string fileName = timestampStr() + (incomplete ? "_incomplete.json" : ".json");
    std::string filePath = dirPath + "/" + fileName;

    nlohmann::json data;
    data["version"]   = 1;
    data["saved_at"]  = timestampStr();
    data["title"]     = extractTitle(messages);

    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& msg : messages) {
        nlohmann::json m;
        m["role"]    = msg.m_role;
        m["content"] = msg.m_content;

        if (!msg.m_toolCalls.empty()) {
            nlohmann::json tcs = nlohmann::json::array();
            for (const auto& tc : msg.m_toolCalls) {
                nlohmann::json tcJson;
                tcJson["id"]        = tc.m_id;
                tcJson["name"]      = tc.m_name;
                tcJson["arguments"] = tc.m_arguments;
                tcs.push_back(std::move(tcJson));
            }
            m["tool_calls"] = std::move(tcs);
        }
        if (!msg.m_toolCallId.empty()) m["tool_call_id"] = msg.m_toolCallId;
        if (!msg.m_name.empty())       m["name"] = msg.m_name;
        msgs.push_back(std::move(m));
    }
    data["messages"] = std::move(msgs);

    std::ofstream file(filePath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return "";
    }
    file << data.dump(2);
    return filePath;
}

bool CLFSessionManager::load(const std::string& filePath,
                             std::vector<CLFMessage>& outMessages) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    try {
        nlohmann::json data = nlohmann::json::parse(file);
        outMessages = parseMessages(data);
        return !outMessages.empty();
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

std::vector<CLFSessionInfo> CLFSessionManager::list(const std::string& dirPath, int limit) {
    std::vector<CLFSessionInfo> result;
    std::error_code ec;

    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) {
        return result;
    }

    // 收集所有 .json 会话文件（不含 _incomplete）
    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.size() < 5) continue;
        if (name.substr(name.size() - 5) != ".json") continue;
        if (name.find("_incomplete") != std::string::npos) continue;
        entries.push_back(entry);
    }

    // 按修改时间倒序
    std::sort(entries.begin(), entries.end(),
        [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.last_write_time() > b.last_write_time();
        });

    int count = 0;
    for (const auto& entry : entries) {
        if (count++ >= limit) break;

        CLFSessionInfo info;
        info.m_path = entry.path().string();

        // 读取标题
        try {
            std::ifstream file(info.m_path);
            nlohmann::json data = nlohmann::json::parse(file);
            info.m_title = data.value("title", "(untitled)");
            info.m_savedAt = data.value("saved_at", "");
        } catch (...) {
            info.m_title = entry.path().stem().string();
        }
        result.push_back(std::move(info));
    }

    return result;
}

std::string CLFSessionManager::findIncomplete(const std::string& dirPath) {
    std::error_code ec;
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) {
        return "";
    }

    // 取最新的 _incomplete 文件
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

bool CLFSessionManager::remove(const std::string& filePath) {
    std::error_code ec;
    return fs::remove(filePath, ec);
}

std::string CLFSessionManager::promote(const std::string& incompletePath) {
    fs::path p(incompletePath);
    std::string name = p.filename().string();
    if (name.find("_incomplete.json") == std::string::npos) {
        return incompletePath; // 不是 incomplete 文件
    }

    std::string newName = name.substr(0, name.size() - std::string("_incomplete.json").size())
                        + ".json";
    fs::path newPath = p.parent_path() / newName;

    std::error_code ec;
    fs::rename(p, newPath, ec);
    return ec ? "" : newPath.string();
}

int CLFSessionManager::cleanupOld(const std::string& dirPath, int maxAgeDays) {
    std::error_code ec;
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) {
        return 0;
    }

    auto cutoff = fs::file_time_type::clock::now()
                - std::chrono::hours(24 * maxAgeDays);
    int removed = 0;

    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.size() < 5) continue;
        if (name.substr(name.size() - 5) != ".json") continue;

        if (entry.last_write_time(ec) < cutoff) {
            if (fs::remove(entry.path(), ec)) {
                ++removed;
            }
        }
    }
    return removed;
}

} // namespace CLF::CLFCore
