// CLFSessionManager.cpp — 会话文件管理实现
// 消息序列化 → 委托 CLFMessageCodec

#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFMessageCodec.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
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
            if (title.size() > 50) {
                // UTF-8 边界安全截断（不劈半多字节字符）
                size_t cut = 47;
                while (cut > 0
                       && (static_cast<unsigned char>(title[cut]) & 0xC0) == 0x80)
                    --cut;
                title = title.substr(0, cut) + "...";
            }
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
    if (out.size() > 80) {
        size_t cut = 77;
        while (cut > 0
               && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
            --cut;
        out = out.substr(0, cut) + "...";
    }
    return out;
}

// 追加一行：锁 → ios::app 打开 → 写 → flush → 关闭（每次独立打开，不缓存流对象）
// tag 仅用于日志定位（header/turn/todo_snapshot/complete/summary）
bool appendLine(const std::string& jsonlPath, const std::string& line, const char* tag) {
    if (jsonlPath.empty() || line.empty()) return false;
    // jsonl 追加互斥（防御性，设计-会话追加式保存.jsonl §3.8）：
    // 当前接线全在 asyncSubmit 工作线程串行，不依赖此锁的正确性；防未来并发调用方。
    // ⚠️ 必须函数内 magic static 而非文件级对象——boost::ut 在静态析构阶段运行测试，
    // 文件级 std::mutex 跨 TU 析构顺序未定义（S2-4 项目级教训，第三次踩坑）
    static std::mutex appendMutex;
    std::lock_guard<std::mutex> lock(appendMutex);

    std::error_code ec;
    fs::path p = fs::u8path(jsonlPath);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path(), ec);
    }

    std::ofstream file(fs::u8path(jsonlPath), std::ios::app | std::ios::binary);
    if (!file.is_open()) {
        CLFLogger::instance().warn("[JsonlAppend] cannot open (" + std::string(tag)
                                   + "): " + jsonlPath);
        return false;
    }
    file << line << "\n";
    file.flush();  // 立即落盘（防崩溃丢失，设计 §3.3）
    if (file.fail()) {
        CLFLogger::instance().warn("[JsonlAppend] write failed, disk full? ("
                                   + std::string(tag) + "): " + jsonlPath);
        return false;
    }
    return true;
}

// 判断文件名后缀（u8string 已按 UTF-8 输出，后缀比较不受 ANSI 代码页影响）
bool endsWithSuffix(const std::string& name, const std::string& suffix) {
    return name.size() >= suffix.size()
        && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // anonymous namespace

// ============================================================================
// jsonl 会话文件命名/复制 helpers（2026-09-02，设计 §3.9）
// ============================================================================

std::string CLFSessionManager::timestampNow() {
    return timestampStr();
}

std::string CLFSessionManager::makeSessionId() {
    // 时间戳紧凑（去分隔符）+ 4 位随机 hex（header 自我标识；不承担关键定位职责）
    std::string compact;
    for (const char c : timestampStr()) {
        if (c != '-' && c != '_') compact += c;
    }
    char rnd[8];
    std::snprintf(rnd, sizeof(rnd), "_%04x",
                  std::random_device{}() & 0xFFFFu);
    return compact + rnd;
}

std::string CLFSessionManager::makeNewSessionPath(const std::string& dirPath,
                                                  const std::string& firstInput,
                                                  const std::string& suffix) {
    // 标题 = firstInput 首行、换行转空格、UTF-8 边界安全截断、文件名安全化
    std::string title = firstInput;
    for (auto& c : title)
        if (c == '\n' || c == '\r') c = ' ';
    if (title.size() > 50) {
        size_t cut = 47;
        while (cut > 0
               && (static_cast<unsigned char>(title[cut]) & 0xC0) == 0x80)
            --cut;
        title = title.substr(0, cut) + "...";
    }
    title = sanitizeFilename(title);
    if (!suffix.empty()) title += suffix;

    std::error_code ec;
    fs::create_directories(fs::u8path(dirPath), ec);
    std::string finalPath = dirPath + "/" + timestampStr() + "_" + title + ".jsonl";
    // 冲突处理：同名加序号（照 save 归档模式）
    for (int n = 2; fs::exists(fs::u8path(finalPath), ec); ++n) {
        finalPath = dirPath + "/" + timestampStr() + "_" + title + "-"
                  + std::to_string(n) + ".jsonl";
    }
    return finalPath;
}

bool CLFSessionManager::copyLines(const std::string& srcPath, const std::string& dstPath) {
    // 逐行复制（resume 续写：源文件冻结快照 → 新续写文件；header 原样，session_id 延续）
    std::ifstream src(fs::u8path(srcPath), std::ios::binary);
    if (!src.is_open()) {
        CLFLogger::instance().warn("[CopyLines] cannot open source: " + srcPath);
        return false;
    }
    std::ofstream dst(fs::u8path(dstPath), std::ios::out | std::ios::app | std::ios::binary);
    if (!dst.is_open()) {
        CLFLogger::instance().warn("[CopyLines] cannot open dest: " + dstPath);
        return false;
    }
    std::string line;
    while (std::getline(src, line)) {
        dst << line << "\n";
    }
    dst.flush();
    if (dst.fail()) {
        CLFLogger::instance().warn("[CopyLines] write failed: " + dstPath);
        return false;
    }
    return true;
}

// ============================================================================
// save — 原子写入 latest.json 或归档为时间戳.json
// ============================================================================

std::string CLFSessionManager::save(const std::vector<CLFMessage>& messages,
                                     const std::string& dirPath,
                                     bool finalize,
                                     const std::vector<std::string>& skills,
                                     const CLFSessionSummary* summary,
                                     const std::vector<CLFTodoItem>& todos) {
    std::error_code ec;
    fs::create_directories(fs::u8path(dirPath), ec);

    // === 归档模式：latest.json → 时间戳.json ===
    if (finalize) {
        std::string latestPath = dirPath + "/latest.json";
        if (!fs::exists(fs::u8path(latestPath), ec)) {
            CLFLogger::instance().debug("[Save] finalize skipped: latest.json not found");
            return "";
        }

        std::string title = extractTitle(messages);
        std::string safeTitle = sanitizeFilename(title);
        std::string finalPath = dirPath + "/" + timestampStr() + "_" + safeTitle + ".json";

        // 冲突处理：如果已存在同名文件，加序号
        for (int n = 2; fs::exists(fs::u8path(finalPath), ec); ++n) {
            finalPath = dirPath + "/" + timestampStr() + "_" + safeTitle + "-"
                      + std::to_string(n) + ".json";
        }

        CLFLogger::instance().debug("[Save] finalizing: latest.json → " + finalPath);
        fs::rename(fs::u8path(latestPath), fs::u8path(finalPath), ec);
        if (ec) {
            CLFLogger::instance().warn("[Save] finalize rename failed: " + ec.message());
            return "";
        }
        CLFLogger::instance().info("[Save] finalized: " + finalPath);
        return finalPath;
    }

    // === 覆盖模式：原子写入 latest.json ===
    std::string json = CLFMessageCodec::serialize(
        messages, timestampStr(), extractTitle(messages), skills, summary, todos);
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
        std::ofstream file(fs::u8path(tmpPath),
                           std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file.is_open()) {
            CLFLogger::instance().warn("[Save] cannot open tmp file: " + tmpPath);
            return "";
        }
        file << json;
        file.close();
        if (file.fail()) {
            CLFLogger::instance().warn("[Save] write failed (disk full?): " + tmpPath);
            fs::remove(fs::u8path(tmpPath), ec);
            return "";
        }
    }

    // ② 原子 rename（同文件系统上保证原子性：要么旧文件在，要么新文件在，不会出现半截文件）
    CLFLogger::instance().debug("[Save] rename: " + tmpPath + " → " + finalPath);
    fs::rename(fs::u8path(tmpPath), fs::u8path(finalPath), ec);
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
                              CLFSessionSummary* outSummary,
                              std::vector<CLFTodoItem>* outTodos) {
    CLFLogger::instance().debug("[Load] opening: " + filePath);

    std::ifstream file(fs::u8path(filePath));
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
                                              outSkills, outSummary, outTodos);

    if (outMessages.empty()) {
        // 损坏文件 → 备份为 .bak，不崩溃
        // ⚠️ rename 前必须先关闭文件（sharing violation 隐患，与 loadJsonl 同根因）
        file.close();
        std::error_code ec;
        std::string bakPath = filePath + ".bak";
        fs::rename(fs::u8path(filePath), fs::u8path(bakPath), ec);
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

std::vector<CLFSessionInfo> CLFSessionManager::list(const std::string& dirPath, int limit,
                                                    const std::string* activeFilePath) {
    std::vector<CLFSessionInfo> result;
    std::error_code ec;

    if (!fs::exists(fs::u8path(dirPath), ec) || !fs::is_directory(fs::u8path(dirPath), ec))
        return result;

    const bool hasActive = (activeFilePath != nullptr && !activeFilePath->empty());

    // 旧 latest.json（覆盖式时代遗留，jsonl 方案不再写入）：
    // 仅当无活跃文件时保持现状语义（最前 + [当前]）；有活跃文件时作普通归档参与排序
    std::string latestPath = dirPath + "/latest.json";
    if (fs::exists(fs::u8path(latestPath), ec)
        && fs::is_regular_file(fs::u8path(latestPath), ec)) {
        CLFSessionInfo info;
        info.m_path     = latestPath;
        info.m_isLatest = !hasActive;

        try {
            std::ifstream file(fs::u8path(latestPath));
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

    // 收集归档：.json（排除 latest.json/_incomplete）+ .jsonl（2026-09-02 新格式）
    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(fs::u8path(dirPath), ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().u8string();
        if (name.size() < 5) continue;
        if (!endsWithSuffix(name, ".json") && !endsWithSuffix(name, ".jsonl")) continue;
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
        info.m_path = entry.path().u8string();
        // [当前] 重定义：活跃文件路径匹配（jsonl 时代）；不再依赖 latest.json 存在性
        info.m_isLatest = hasActive && (info.m_path == *activeFilePath);

        try {
            std::ifstream file(fs::u8path(info.m_path));
            std::ostringstream oss;
            oss << file.rdbuf();
            std::string title;
            if (endsWithSuffix(info.m_path, ".jsonl")) {
                // jsonl：title 取第一行 header 的 title 字段
                std::ifstream lineFile(fs::u8path(info.m_path));
                std::string firstLine;
                if (std::getline(lineFile, firstLine)) {
                    CLFMessageCodec::parseHeaderLine(
                        nlohmann::json::parse(firstLine), &title, &info.m_savedAt);
                }
            } else {
                CLFMessageCodec::parseFull(oss.str(), nullptr, &info.m_savedAt, &title);
            }
            info.m_title = title.empty() ? "(untitled)" : title;
        } catch (...) {
            // u8string：中文会话标题的 fallback 不再乱码
            info.m_title = entry.path().stem().u8string();
        }
        if (info.m_title.empty() || info.m_title == "(untitled)") {
            if (endsWithSuffix(info.m_path, ".json")) {
                // u8path：窄字符 ifstream 打开按 ANSI 代码页解释路径，中文路径打开失败
                std::ifstream file(fs::u8path(info.m_path));
                std::ostringstream oss2;
                oss2 << file.rdbuf();
                auto msgs = CLFMessageCodec::parse(oss2.str());
                info.m_title = extractTitle(msgs);
            } else {
                info.m_title = entry.path().stem().u8string();
            }
        }
        result.push_back(std::move(info));
    }

    return result;
}

// ============================================================================
// jsonl 追加式保存（设计-会话追加式保存.jsonl.md §3.9，2026-09-02）
// ============================================================================

bool CLFSessionManager::appendHeader(const std::string& jsonlPath, const std::string& line) {
    return appendLine(jsonlPath, line, "header");
}

bool CLFSessionManager::appendTurn(const std::string& jsonlPath, const std::string& line) {
    return appendLine(jsonlPath, line, "turn");
}

bool CLFSessionManager::appendTodoSnapshot(const std::string& jsonlPath, const std::string& line) {
    return appendLine(jsonlPath, line, "todo_snapshot");
}

bool CLFSessionManager::appendComplete(const std::string& jsonlPath, const std::string& line) {
    return appendLine(jsonlPath, line, "complete");
}

bool CLFSessionManager::appendSummary(const std::string& jsonlPath, const std::string& line) {
    return appendLine(jsonlPath, line, "summary");
}

bool CLFSessionManager::loadJsonl(const std::string& filePath,
                                  std::vector<CLFMessage>& outMessages,
                                  std::vector<std::string>* outSkills,
                                  CLFSessionSummary* outSummary,
                                  std::vector<CLFTodoItem>* outTodos,
                                  std::vector<CLFTodoItem>* outCompleteTodos,
                                  CLFSessionInfo* outHeaderInfo) {
    CLFLogger::instance().debug("[LoadJsonl] opening: " + filePath);

    std::ifstream file(fs::u8path(filePath));
    if (!file.is_open()) {
        CLFLogger::instance().warn("[LoadJsonl] not found or cannot open: " + filePath);
        return false;
    }

    if (outSkills)         outSkills->clear();
    if (outSummary)        *outSummary = {};
    if (outTodos)          outTodos->clear();
    if (outCompleteTodos)  outCompleteTodos->clear();

    std::vector<CLFMessage>  messages;
    std::vector<CLFTodoItem> latestSnapshot;   // 最后一条可解析 todo_snapshot
    bool                     hasSnapshot = false;
    std::vector<CLFTodoItem> lastTurnTodos;    // 最后带 todos 的 turn 行（旧版/未用快照兼容）
    CLFSessionSummary        latestSummary;
    std::vector<CLFTodoItem> lastComplete;     // 最后一条 complete 行（回显用，J6）
    CLFSessionInfo           headerInfo;
    headerInfo.m_path = filePath;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        nlohmann::json obj;
        try {
            obj = nlohmann::json::parse(line);
        } catch (const nlohmann::json::exception&) {
            // 损坏/撕裂行：跳过，不整体失败（设计 §6 边界表）
            CLFLogger::instance().warn("[LoadJsonl] skip corrupted line: " + filePath);
            continue;
        }

        const std::string type = obj.value("type", "");
        if (type == "header") {
            if (outHeaderInfo || outSkills) {
                std::vector<std::string> skills;
                CLFMessageCodec::parseHeaderLine(
                    obj, &headerInfo.m_title, &headerInfo.m_savedAt,
                    nullptr, nullptr, outSkills ? &skills : nullptr);
                if (outSkills) *outSkills = std::move(skills);
            }
        } else if (type == "turn") {
            std::vector<CLFMessage>  turnMsgs;
            std::vector<CLFTodoItem> turnTodos;
            if (CLFMessageCodec::parseTurnLine(obj, turnMsgs, &turnTodos)) {
                for (auto& m : turnMsgs) messages.push_back(std::move(m));
                if (!turnTodos.empty()) lastTurnTodos = std::move(turnTodos);
            } else {
                CLFLogger::instance().warn("[LoadJsonl] skip invalid turn line: " + filePath);
            }
        } else if (type == "todo_snapshot") {
            std::vector<CLFTodoItem> todos;
            if (CLFMessageCodec::parseTodoSnapshotLine(obj, todos)) {
                // 覆盖语义：每读到一条可解析快照就更新（含 clear 产生的空快照——
                // hasSnapshot 置 true 而非看空非空，保证"清单被清空"状态正确恢复）
                latestSnapshot = std::move(todos);
                hasSnapshot    = true;
            } else {
                CLFLogger::instance().warn("[LoadJsonl] skip invalid todo_snapshot line: " + filePath);
            }
        } else if (type == "complete") {
            std::vector<CLFTodoItem> todos;
            if (CLFMessageCodec::parseCompleteLine(obj, todos)) {
                if (!todos.empty()) lastComplete = std::move(todos);
            }
        } else if (type == "summary") {
            CLFSessionSummary s;
            if (CLFMessageCodec::parseSummaryLine(obj, s)) {
                latestSummary = s;   // 取最后一条可解析摘要
            }
        }
        // 未知 type：静默跳过（向前兼容未来行类型）
    }

    if (messages.empty()) {
        // 无任何有效消息行 → 按损坏文件处理：备份 .bak，不崩溃（照 load 的损坏保护）
        // ⚠️ rename 前必须先关闭文件——MSVC fstream 默认共享模式不含 FILE_SHARE_DELETE，
        // 文件仍打开时 MoveFile 会失败（sharing violation），.bak 备份静默丢失
        file.close();
        std::error_code ec;
        std::string bakPath = filePath + ".bak";
        fs::rename(fs::u8path(filePath), fs::u8path(bakPath), ec);
        CLFLogger::instance().warn("[LoadJsonl] no valid lines, backed up: " + bakPath);
        return false;
    }

    outMessages = std::move(messages);
    if (outSummary)       *outSummary       = latestSummary;
    if (outTodos)         *outTodos         = hasSnapshot ? latestSnapshot : lastTurnTodos;
    if (outCompleteTodos) *outCompleteTodos = lastComplete;
    if (outHeaderInfo)    *outHeaderInfo    = headerInfo;

    CLFLogger::instance().info("[LoadJsonl] success: " + filePath + ", "
                               + std::to_string(outMessages.size()) + " msgs");
    return true;
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

int CLFSessionManager::cleanupOld(const std::string& dirPath, int maxAgeDays,
                                  const std::string* activeFilePath) {
    std::error_code ec;
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) return 0;

    auto cutoff = fs::file_time_type::clock::now()
                - std::chrono::hours(24 * maxAgeDays);
    int removed = 0;

    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().u8string();
        if (name.size() < 5) continue;
        // .json 与 .jsonl 一并清理（2026-09-02，设计 §6 边界表）
        if (!endsWithSuffix(name, ".json") && !endsWithSuffix(name, ".jsonl")) continue;
        // 不要清理 latest.json（旧版兼容期的当前会话文件）
        if (name == "latest.json") continue;
        // 活跃会话文件绝不删除（清理是删除动作，与 list 的"不排除仅标记"语义相反）
        if (activeFilePath && !activeFilePath->empty()
            && entry.path().u8string() == *activeFilePath) continue;

        if (entry.last_write_time(ec) < cutoff) {
            if (fs::remove(entry.path(), ec)) ++removed;
        }
    }
    return removed;
}

} // namespace CLF::CLFCore
