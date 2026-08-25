// CLFBuiltinTools.cpp — 内置工具 handler 实现与注册

#include "CLFTools/CLFBuiltinTools.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <nlohmann/json.hpp>

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFTools/CLFCommandExec.hpp"
#include "CLFTools/CLFFileOps.hpp"
#include "CLFTools/CLFSearchContent.hpp"

namespace CLF::CLFTools {

// ============================================================================
// Handler 实现
// ============================================================================

// ---------------------------------------------------------------------------
// 内部辅助（声明见 hpp 的 detail 命名空间——暴露仅为单测可达）
// ---------------------------------------------------------------------------
namespace detail {

//判定路径是否位于工作区（cwd）之内
// 用 weakly_canonical 解析——它会跟随 symlink/junction，
// 否则 "workspace/link -> C:\Windows" 这类软链接可绕过边界。
// example:
//   std::string err;
//   if (!detail::isWithinWorkspace(path, err)) return err;
bool isWithinWorkspace(const std::string& path, std::string& outError) {
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::path root = fs::weakly_canonical(
        fs::u8path(CLF::CLFCore::CLFConfigLoader::getWorkingDir()), ec);
    if (ec) { outError = "无法解析工作区根目录"; return false; }

    fs::path target = fs::u8path(path);
    if (!target.is_absolute()) target = root / target;
    target = fs::weakly_canonical(target, ec);
    if (ec) { outError = "无法解析路径: " + path; return false; }

    // 必须逐段比较，不能用字符串前缀匹配——否则 "proj-evil" 会被
    // 误判为落在 "proj" 之内。
    auto rootIt = root.begin();
    auto tgtIt  = target.begin();
    for (; rootIt != root.end(); ++rootIt, ++tgtIt) {
        if (tgtIt == target.end() || *tgtIt != *rootIt) {
            outError = "路径超出工作区边界: " + path;
            return false;
        }
    }
    return true;
}

//判定命令的退出码是否应视为成功（S2-3 退出码白名单）
// grep/diff 一类用退出码 1 表示"无匹配 / 有差异"——那是正常结果而非失败，
// 一律按 exitCode!=0 判失败会让模型误以为命令出错。
// example:
//   exitCodeMeansSuccess("grep foo a.txt", 1);  // true
bool exitCodeMeansSuccess(const std::string& command, int exitCode) {
    if (exitCode == 0) return true;
    if (exitCode != 1) return false;   // 仅退出码 1 参与白名单判定

    // 取命令首 token → 去引号 → 去路径 → 去扩展名 → 转小写
    std::istringstream iss(command);
    std::string first;
    iss >> first;
    first.erase(std::remove(first.begin(), first.end(), '"'), first.end());
    if (const auto slash = first.find_last_of("/\\"); slash != std::string::npos) {
        first = first.substr(slash + 1);
    }
    if (const auto dot = first.rfind('.'); dot != std::string::npos) {
        first = first.substr(0, dot);
    }
    for (auto& c : first) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const char* name : {"grep", "rg", "findstr", "diff", "fc"}) {
        if (first == name) return true;
    }
    return false;
}

//按行切片（offset 为 0 基起始行，limit<=0 表示取到末尾）
// example:
//   sliceLines(content, 10, 20);  // 第 10-29 行
std::string sliceLines(const std::string& content, int offset, int limit) {
    if (offset <= 0 && limit <= 0) return content;
    std::istringstream iss(content);
    std::string line, out;
    int idx = 0, taken = 0;
    while (std::getline(iss, line)) {
        if (idx++ < offset) continue;
        if (limit > 0 && taken >= limit) break;
        out += line;
        out += '\n';
        ++taken;
    }
    return out;
}

} // namespace detail

namespace {

// 单文件读取上限，防超大文件撑爆内存（与 search 的 1MB 上限相互独立）
constexpr std::uintmax_t kMaxReadFileSize = 50ull * 1024 * 1024;

std::string getCurrentTimeHandler(const std::string& /*args*/) {
    std::time_t now = std::time(nullptr);
    std::tm localTime{};
    // 双平台（整体审查 P2-8）：Windows localtime_s / POSIX localtime_r
#ifdef _WIN32
    if (localtime_s(&localTime, &now) != 0) {
        return "[Error] Failed to get local time";
    }
#else
    if (localtime_r(&now, &localTime) == nullptr) {
        return "[Error] Failed to get local time";
    }
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &localTime);
    return std::string(buf);
}

std::string echoHandler(const std::string& args) {
    return "Echo: " + args;
}

// S2-1: 边界/大小/行范围三项均在 handler 层实施——CLFFileOps::readFile 还被
// previewEdit、SystemPromptBuilder 等内部路径调用，在底层加限制会误伤配置读取。
std::string readFileHandlerImpl(const std::string& args, bool allowAbsolute) {
    using json = nlohmann::json;
    namespace fs = std::filesystem;
    json result;
    try {
        json params      = json::parse(args);
        std::string path = params.value("path", "");
        const int offset = params.value("offset", 0);
        const int limit  = params.value("limit", 0);

        // ① 工作区边界（allow_absolute_read 为逃生口）
        if (!allowAbsolute) {
            std::string boundErr;
            if (!detail::isWithinWorkspace(path, boundErr)) {
                result["success"] = false;
                result["error"]   = boundErr
                    + "（如确需读取工作区外文件，请在配置中开启 agent.allow_absolute_read）";
                return result.dump();
            }
        }

        // ② 大小上限（目录时 file_size 置 ec，不会误判）
        std::error_code ec;
        const auto size = fs::file_size(fs::u8path(path), ec);
        if (!ec && size > kMaxReadFileSize) {
            result["success"] = false;
            result["error"]   = "文件过大（" + std::to_string(size / (1024 * 1024))
                              + "MB，上限 50MB）: " + path;
            return result.dump();
        }

        auto fileResult = CLF::CLFTools::readFile(path);
        result["success"] = fileResult.m_success;
        if (!fileResult.m_success) {
            result["error"] = fileResult.m_error;
            return result.dump();
        }

        // ③ 行范围切片
        result["content"] = detail::sliceLines(fileResult.m_content, offset, limit);
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"]   = std::string("Handler error: ") + e.what();
    }
    return result.dump();
}

std::string writeFileHandler(const std::string& args) {
    using json = nlohmann::json;
    json result;
    try {
        json params = json::parse(args);
        std::string path    = params.value("path", "");
        std::string content = params.value("content", "");
        auto fileResult = CLF::CLFTools::writeFile(path, content);
        result["success"] = fileResult.m_success;
        if (fileResult.m_success) {
            result["path"]    = path;
            result["written"] = content.size();
        } else {
            result["error"] = fileResult.m_error;
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"]   = std::string("Handler error: ") + e.what();
    }
    return result.dump();
}

std::string editFileHandler(const std::string& args) {
    using json = nlohmann::json;
    json result;
    try {
        json params   = json::parse(args);
        std::string path    = params.value("path", "");
        std::string oldStr  = params.value("old_string", "");
        std::string newStr  = params.value("new_string", "");
        auto fileResult = CLF::CLFTools::editFile(path, oldStr, newStr);
        result["success"] = fileResult.m_success;
        if (fileResult.m_success) {
            result["path"]    = path;
            result["written"] = fileResult.m_content.size();
        } else {
            result["error"] = fileResult.m_error;
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"]   = std::string("Handler error: ") + e.what();
    }
    return result.dump();
}

std::string listDirectoryHandler(const std::string& args) {
    using json = nlohmann::json;
    json result;
    try {
        json params = json::parse(args);
        std::string path = params.value("path", ".");
        auto fileResult = CLF::CLFTools::listDirectory(path);
        result["success"] = fileResult.m_success;
        if (fileResult.m_success) {
            result["content"] = fileResult.m_content;
        } else {
            result["error"] = fileResult.m_error;
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"]   = std::string("Handler error: ") + e.what();
    }
    return result.dump();
}

std::string executeCommandHandler(const std::string& args) {
    using json = nlohmann::json;
    json result;
    try {
        json params = json::parse(args);
        std::string command = params.value("command", "");
        int timeout = params.value("timeout", 30);
        std::string cwd = params.value("cwd", "");

        // cwd 须位于工作区内（复用 S2-1 边界校验）；仅约束该参数本身，
        // 命令文本里的 cd 不在管控范围内
        if (!cwd.empty()) {
            std::string boundErr;
            if (!detail::isWithinWorkspace(cwd, boundErr)) {
                result["success"] = false;
                result["error"]   = "cwd 无效：" + boundErr;
                return result.dump();
            }
        }

        auto cmdResult = CLF::CLFTools::executeCommand(command, timeout, cwd);
        result["success"]  = detail::exitCodeMeansSuccess(command, cmdResult.m_exitCode);
        result["exitCode"] = cmdResult.m_exitCode;
        result["stdout"]   = cmdResult.m_stdout;
        result["stderr"]   = cmdResult.m_stderr;
        if (cmdResult.m_timedOut) {
            result["timedOut"] = true;
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"]   = std::string("Handler error: ") + e.what();
    }
    return result.dump();
}

} // anonymous namespace

// ============================================================================
// 注册入口
// ============================================================================

void registerBuiltinTools(CLF::CLFCore::CLFAgentLoop& agent) {
    using CLF::CLFCore::CLFTool;

    // —— 文件操作 ——
    CLFTool readFileTool;
    readFileTool.m_name        = "read_file";
    readFileTool.m_description = "读取文件内容（限工作区内，单文件上限 50MB，支持按行范围读取）";
    readFileTool.m_parametersSchema = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "文件路径，相对工作区或绝对路径（须位于工作区内）"},
            "offset": {"type": "integer", "description": "起始行号（0 基），省略则从头读"},
            "limit": {"type": "integer", "description": "最多读取行数，省略或 <=0 表示读到末尾"}
        },
        "required": ["path"]
    })";
    // 按值捕获（非引用）：注册后该配置不再变化，无生命周期约束
    const bool allowAbsoluteRead = agent.getConfig().m_allowAbsoluteRead;
    readFileTool.m_handler = [allowAbsoluteRead](const std::string& args) {
        return readFileHandlerImpl(args, allowAbsoluteRead);
    };
    agent.registerTool(readFileTool);

    CLFTool writeFileTool;
    writeFileTool.m_name        = "write_file";
    writeFileTool.m_description = "将内容写入指定路径的文件（覆盖模式，原子写入）";
    writeFileTool.m_risk        = CLF::CLFCore::CLFToolRisk::Write;
    writeFileTool.m_parametersSchema = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "文件路径"},
            "content": {"type": "string", "description": "要写入的内容"}
        },
        "required": ["path", "content"]
    })";
    writeFileTool.m_handler = writeFileHandler;
    agent.registerTool(writeFileTool);

    CLFTool editFileTool;
    editFileTool.m_name        = "edit_file";
    editFileTool.m_description = "精确替换文件中的字符串（old_string 必须唯一匹配）";
    editFileTool.m_risk        = CLF::CLFCore::CLFToolRisk::Write;
    editFileTool.m_parametersSchema = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "文件路径"},
            "old_string": {"type": "string", "description": "要替换的原字符串（必须唯一匹配）"},
            "new_string": {"type": "string", "description": "替换后的新字符串"}
        },
        "required": ["path", "old_string", "new_string"]
    })";
    editFileTool.m_handler = editFileHandler;
    agent.registerTool(editFileTool);

    CLFTool listDirTool;
    listDirTool.m_name        = "list_directory";
    listDirTool.m_description = "列出指定目录下的文件和子目录";
    listDirTool.m_parametersSchema = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "目录路径，默认当前目录"}
        },
        "required": []
    })";
    listDirTool.m_handler = listDirectoryHandler;
    agent.registerTool(listDirTool);

    // —— 系统操作 ——
    CLFTool execCmdTool;
    execCmdTool.m_name        = "execute_command";
    execCmdTool.m_description =
        "执行 Shell 命令并返回输出。grep/rg/findstr/diff/fc 的退出码 1 视为成功（无匹配/有差异是正常结果）";
    execCmdTool.m_risk        = CLF::CLFCore::CLFToolRisk::Command;
    execCmdTool.m_parametersSchema = R"({
        "type": "object",
        "properties": {
            "command": {"type": "string", "description": "要执行的命令"},
            "timeout": {"type": "integer", "description": "超时秒数，默认 30"},
            "cwd": {"type": "string", "description": "工作目录（须位于工作区内），省略则用当前目录"}
        },
        "required": ["command"]
    })";
    execCmdTool.m_handler = executeCommandHandler;
    agent.registerTool(execCmdTool);

    CLFTool timeTool;
    timeTool.m_name        = "get_current_time";
    timeTool.m_description = "获取当前系统日期和时间";
    timeTool.m_parametersSchema = R"({
        "type": "object",
        "properties": {},
        "required": []
    })";
    timeTool.m_handler = getCurrentTimeHandler;
    agent.registerTool(timeTool);

    // —— 测试工具 ——
    CLFTool echoTool;
    echoTool.m_name        = "echo";
    echoTool.m_description = "回显输入内容，用于测试工具调用";
    echoTool.m_parametersSchema = R"({
        "type": "object",
        "properties": {
            "message": {
                "type": "string",
                "description": "要回显的消息"
            }
        },
        "required": ["message"]
    })";
    echoTool.m_handler = echoHandler;
    agent.registerTool(echoTool);

    // —— search_content ——
    CLFTool searchTool;
    searchTool.m_name        = "search_content";
    searchTool.m_description =
        "在目录中搜索文件内容（纯文本匹配）。省略 fileTypes 时只搜常见文本扩展名（不扫二进制）；"
        "跳过 .git/node_modules/build/cmake-build-* 等目录，跳过 >1MB 文件，结果上限 500 行";
    searchTool.m_parametersSchema = R"({
        "type": "object",
        "properties": {
            "pattern": {
                "type": "string",
                "description": "要搜索的文本（纯文本，非正则）"
            },
            "directory": {
                "type": "string",
                "description": "搜索根目录（相对于工作区）"
            },
            "fileTypes": {
                "type": "string",
                "description": "逗号分隔的扩展名过滤（如 .cpp,.h）；省略则使用默认文本扩展名白名单"
            }
        },
        "required": ["pattern", "directory"]
    })";
    searchTool.m_handler = [](const std::string& args) -> std::string {
        using json = nlohmann::json;
        json result;
        try {
            json params     = json::parse(args);
            std::string pattern   = params.value("pattern", "");
            std::string directory = params.value("directory", ".");
            std::string fileTypes = params.value("fileTypes", "");
            result["success"] = true;
            result["content"] = searchContent(pattern, directory, fileTypes);
        } catch (const std::exception& e) {
            result["success"] = false;
            result["error"]   = std::string("search_content failed: ") + e.what();
        }
        return result.dump();
    };
    agent.registerTool(searchTool);
}

} // namespace CLF::CLFTools
