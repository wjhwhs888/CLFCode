// CLFBuiltinTools.cpp — 内置工具 handler 实现与注册

#include "CLFTools/CLFBuiltinTools.hpp"

#include <ctime>
#include <nlohmann/json.hpp>

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFTools/CLFCommandExec.hpp"
#include "CLFTools/CLFFileOps.hpp"

namespace CLF::CLFTools {

// ============================================================================
// Handler 实现
// ============================================================================

namespace {

std::string getCurrentTimeHandler(const std::string& /*args*/) {
    std::time_t now = std::time(nullptr);
    std::tm localTime{};
    if (localtime_s(&localTime, &now) != 0) {
        return "[Error] Failed to get local time";
    }
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &localTime);
    return std::string(buf);
}

std::string echoHandler(const std::string& args) {
    return "Echo: " + args;
}

std::string readFileHandler(const std::string& args) {
    using json = nlohmann::json;
    json result;
    try {
        json params = json::parse(args);
        std::string path = params.value("path", "");
        auto fileResult = CLF::CLFTools::readFile(path);
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
        auto cmdResult = CLF::CLFTools::executeCommand(command, timeout);
        result["success"]  = (cmdResult.m_exitCode == 0);
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
    readFileTool.m_description = "读取指定路径的文件内容";
    readFileTool.m_parametersSchema = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "文件路径"}
        },
        "required": ["path"]
    })";
    readFileTool.m_handler = readFileHandler;
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
    execCmdTool.m_description = "执行 Shell 命令并返回输出";
    execCmdTool.m_risk        = CLF::CLFCore::CLFToolRisk::Command;
    execCmdTool.m_parametersSchema = R"({
        "type": "object",
        "properties": {
            "command": {"type": "string", "description": "要执行的命令"},
            "timeout": {"type": "integer", "description": "超时秒数，默认 30"}
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
}

} // namespace CLF::CLFTools
