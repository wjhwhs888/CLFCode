// CLFSessionFileCtx.cpp — 会话文件上下文实现
// C2 拆分：逻辑自 CLFAgentLoop（beginSessionFile/appendTurnLine/
// appendSummaryLineNow/restoreSession 回显段）原样搬移

#include "CLFCore/CLFSessionFileCtx.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFMessageCodec.hpp"
#include "CLFCore/CLFSessionManager.hpp"

namespace CLF::CLFCore {

namespace fs = std::filesystem;

void CLFSessionFileCtx::setActiveSessionFile(const std::string& jsonlPath) {
    std::lock_guard<std::mutex> lock(m_sessionCtxMutex);
    m_activeSessionFile = jsonlPath;
}

std::string CLFSessionFileCtx::getActiveSessionFile() const {
    std::lock_guard<std::mutex> lock(m_sessionCtxMutex);
    return m_activeSessionFile;
}

std::string CLFSessionFileCtx::beginSessionFile(const std::string& firstInput,
                                                const std::string& modelName,
                                                const std::vector<std::string>& loadedSkills) {
    const std::string resumedFrom = m_resumedFrom;   // 本地副本（末尾清）
    const bool isContinuation = !resumedFrom.empty();

    // 续写：新文件以源文件标题命名（读源 header 的 title，失败用源文件 stem）
    std::string titleForName = firstInput;
    if (isContinuation) {
        titleForName.clear();
        std::ifstream src(fs::u8path(resumedFrom));
        std::string firstLine;
        if (std::getline(src, firstLine)) {
            try {
                const nlohmann::json obj = nlohmann::json::parse(firstLine);
                CLFMessageCodec::parseHeaderLine(obj, &titleForName);
            } catch (...) {}
        }
        if (titleForName.empty()) {
            titleForName = fs::u8path(resumedFrom).stem().u8string();
        }
    }

    const std::string path = CLFSessionManager::makeNewSessionPath(
        m_historyDir, titleForName, isContinuation ? "续" : "");
    if (path.empty()) return "";

    if (isContinuation) {
        // 复制源文件全部行（header 原样——session_id 延续语义；源文件冻结在 resume 时点）
        if (!CLFSessionManager::copyLines(resumedFrom, path)) {
            CLFLogger::instance().warn("[SessionFile] copy failed: "
                                       + resumedFrom + " -> " + path);
            return "";
        }
        // 生命周期定案（§八 补丁 4）：续写文件创建后清 m_resumedFrom，
        // 此后轮次按普通语义（新回合清面板）
        m_resumedFrom.clear();
    } else {
        // 全新文件：header（含 skills 快照——S2-6 起随会话持久化的载体）
        const std::string header = CLFMessageCodec::serializeHeaderLine(
            titleForName, CLFSessionManager::timestampNow(),
            CLFSessionManager::makeSessionId(),
            modelName, loadedSkills);
        if (!CLFSessionManager::appendHeader(path, header)) {
            CLFLogger::instance().warn("[SessionFile] header write failed: " + path);
            return "";
        }
    }

    setActiveSessionFile(path);
    CLFLogger::instance().info("[SessionFile] created: " + path
        + (isContinuation ? std::string(" (continuation of ") + resumedFrom + ")"
                          : std::string()));
    return path;
}

std::string CLFSessionFileCtx::appendTurn(const std::vector<CLFMessage>& msgs,
                                          const std::vector<CLFTodoItem>* todosPtr) {
    const std::string path = getActiveSessionFile();
    if (path.empty()) {
        CLFLogger::instance().debug("[AppendTurn] skipped: no active session file");
        return "";
    }
    if (msgs.size() <= m_turnStartMsgCount) {
        CLFLogger::instance().debug("[AppendTurn] skipped: no new messages");
        return "";
    }

    // 本轮新增消息差集（user + assistant + tool，全量字段照现有序列化）
    std::vector<CLFMessage> newMsgs(msgs.begin() + m_turnStartMsgCount, msgs.end());
    m_turnStartMsgCount = msgs.size();

    const std::string line = CLFMessageCodec::serializeTurnLine(
        newMsgs, CLFSessionManager::timestampNow(), todosPtr);
    if (line.empty()) {
        CLFLogger::instance().warn("[AppendTurn] serialize failed");
        return "";
    }
    if (!CLFSessionManager::appendTurn(path, line)) {
        CLFLogger::instance().warn("[AppendTurn] append failed: " + path);
        return "";
    }
    return path;
}

void CLFSessionFileCtx::appendSummaryLine(const CLFSessionSummary& summary) {
    const std::string path = getActiveSessionFile();
    if (path.empty() || !summary.m_valid) return;
    const std::string line = CLFMessageCodec::serializeSummaryLine(
        summary, CLFSessionManager::timestampNow());
    if (!CLFSessionManager::appendSummary(path, line)) {
        CLFLogger::instance().warn("[Summary] append failed: " + path);
    }
}

void CLFSessionFileCtx::collectEchoLines(const std::string& filePath,
                                         const std::vector<CLFMessage>& messages,
                                         std::vector<CLFSessionEchoLine>& outEcho) const {
    // J3: 按扩展名分流——.jsonl 走追加式解析，.json 走覆盖式（旧归档永久兼容）
    const bool isJsonl = filePath.size() >= 6
                      && filePath.compare(filePath.size() - 6, 6, ".jsonl") == 0;
    if (isJsonl) {
        // J6: jsonl 行级回显（设计-会话追加式保存.jsonl §3.6）——
        // turn 行 → User/Assistant 行 + 尾随 TodoRound 行；
        // complete 行 → TodoComplete 行；todo_snapshot/summary/header 行不回显
        std::ifstream file(fs::u8path(filePath));
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            nlohmann::json obj;
            try { obj = nlohmann::json::parse(line); } catch (...) { continue; }
            const std::string type = obj.value("type", "");

            if (type == JsonlType::kTurn) {
                std::vector<CLFMessage>  turnMsgs;
                std::vector<CLFTodoItem> turnTodos;
                if (!CLFMessageCodec::parseTurnLine(obj, turnMsgs, &turnTodos)) continue;
                for (const auto& msg : turnMsgs) {
                    if (msg.m_role == "user") {
                        outEcho.push_back({CLFSessionEchoLine::Kind::User,
                                           msg.m_content, {}});
                    } else if (msg.m_role == "assistant" && !msg.m_content.empty()) {
                        outEcho.push_back({CLFSessionEchoLine::Kind::Assistant,
                                           msg.m_content, {}});
                    }
                    // tool 消息跳过（终端不需要显示）
                }
                if (!turnTodos.empty())
                    outEcho.push_back({CLFSessionEchoLine::Kind::TodoRound,
                                       "", std::move(turnTodos)});
            } else if (type == JsonlType::kComplete) {
                std::vector<CLFTodoItem> completeLine;
                if (!CLFMessageCodec::parseCompleteLine(obj, completeLine)) continue;
                outEcho.push_back({CLFSessionEchoLine::Kind::TodoComplete,
                                   "", std::move(completeLine)});
            }
        }
    } else {
        for (const auto& msg : messages) {
            if (msg.m_role == "user") {
                outEcho.push_back({CLFSessionEchoLine::Kind::User, msg.m_content, {}});
            } else if (msg.m_role == "assistant" && !msg.m_content.empty()) {
                outEcho.push_back({CLFSessionEchoLine::Kind::Assistant, msg.m_content, {}});
            }
            // tool / system 消息跳过（终端不需要显示）
        }
    }
}

} // namespace CLF::CLFCore
