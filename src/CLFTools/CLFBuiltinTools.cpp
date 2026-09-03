// CLFBuiltinTools.cpp — 内置工具 handler 实现与注册

#include "CLFTools/CLFBuiltinTools.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>   // A4a：withHandlerScaffold 的 std::function
#include <sstream>
#include <nlohmann/json.hpp>

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFTools/CLFCommandExec.hpp"
#include "CLFTools/CLFFileOps.hpp"
#include "CLFTools/CLFSearchContent.hpp"
#include "CLFTools/CLFWebFetch.hpp"
#include "CLFTypes/CLFTextUtil.hpp"   // A2：localNow

namespace CLF::CLFTools {
using CLF::CLFCore::CLFTextUtil;   // A2

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

// A4a：handler 脚手架收敛（设计-阶段1 §五 A4，2026-09-03）——
// 统一 parse / try-catch / dump 骨架（原 8 处同构样板）；body 内写业务与容错，
// 错误文案统一 "Handler error: "（qa 无文案断言，A4-1 取证）；
// todo_write 状态机不属本批（A4-2），保持原样不经此包装。
// example:
//   return withHandlerScaffold(args, [](const json& params, json& result) {
//       result["success"] = true;
//   });
std::string withHandlerScaffold(
    const std::string& args,
    const std::function<void(const nlohmann::json& params, nlohmann::json& result)>& body) {
    nlohmann::json result;
    try {
        nlohmann::json params = nlohmann::json::parse(args);
        body(params, result);
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"]   = std::string("Handler error: ") + e.what();
    }
    return result.dump();
}

} // namespace detail

namespace {

// 单文件读取上限，防超大文件撑爆内存（与 search 的 1MB 上限相互独立）
constexpr std::uintmax_t kMaxReadFileSize = 50ull * 1024 * 1024;

std::string getCurrentTimeHandler(const std::string& /*args*/) {
    // A2：平台 ifdef → CLFTextUtil::localNow（格式零变化）
    return CLFTextUtil::localNow("%Y-%m-%d %H:%M:%S");
}

std::string echoHandler(const std::string& args) {
    return "Echo: " + args;
}

// S2-1: 边界/大小/行范围三项均在 handler 层实施——CLFFileOps::readFile 还被
// previewEdit 等内部路径调用，在底层加限制会误伤配置读取。
// A4a：脚手架收敛至 detail::withHandlerScaffold（parse/try-catch/dump 统一）
std::string readFileHandlerImpl(const std::string& args, bool allowAbsolute) {
    return detail::withHandlerScaffold(args, [allowAbsolute](const nlohmann::json& params, nlohmann::json& result) {
        namespace fs = std::filesystem;
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
                return;
            }
        }

        // ② 大小上限（目录时 file_size 置 ec，不会误判）
        std::error_code ec;
        const auto size = fs::file_size(fs::u8path(path), ec);
        if (!ec && size > kMaxReadFileSize) {
            result["success"] = false;
            result["error"]   = "文件过大（" + std::to_string(size / (1024 * 1024))
                              + "MB，上限 50MB）: " + path;
            return;
        }

        auto fileResult = CLF::CLFTools::readFile(path);
        result["success"] = fileResult.m_success;
        if (!fileResult.m_success) {
            result["error"] = fileResult.m_error;
            return;
        }

        // ③ 行范围切片
        result["content"] = detail::sliceLines(fileResult.m_content, offset, limit);
    });
}

bool isValidTodoStatus(const std::string& s) {
    return s == "pending" || s == "in_progress" || s == "completed";
}

// S2-6: 待办清单。**这是首个需要捕获状态的工具**——其余 handler 都是无捕获
// lambda。捕获 agent 引用后 lambda 又存进 agent.m_tools，构成自引用，因此
// CLFAgentLoop 实例注册工具后不可拷贝/移动（否则悬垂）。
// 实现留在 anonymous（仅本 TU 可见），顶层同名薄壳在 §"内置工具注册" 前
std::string todoWriteHandlerImplInternal(const std::string& args,
                                         CLF::CLFCore::CLFAgentLoop& agent) {
    using json = nlohmann::json;
    using CLF::CLFCore::CLFTodoItem;
    json result;
    try {
        json params = json::parse(args);
        const std::string action = params.value("action", "list");

        auto renderList = [&agent]() {
            json arr = json::array();
            for (const auto& t : agent.getTodos()) {
                arr.push_back({{"id", t.m_id}, {"content", t.m_content},
                               {"status", t.m_status}});
            }
            return arr;
        };

        if (action == "list") {
            // list 不改数据——不置 m_todoDirty、不写快照（§6.4-D）
            result["success"] = true;
            result["todos"]   = renderList();
        } else if (action == "clear") {
            agent.setTodos({});
            // J3 接线：清单变化即时落盘（防崩溃丢进度，jsonl 文档 §3.2）+ 置脏标志
            agent.markTodosDirty();
            agent.appendTodoSnapshotNow();
            result["success"] = true;
            result["todos"]   = json::array();
        } else if (action == "create") {
            // 整表替换语义（对齐 dsh 的 whole-list replacement）
            std::vector<CLFTodoItem> items;
            int autoId = 1;
            if (params.contains("todos") && params["todos"].is_array()) {
                for (const auto& t : params["todos"]) {
                    if (!t.is_object()) continue;
                    CLFTodoItem item;
                    item.m_content = t.value("content", "");
                    if (item.m_content.empty()) continue;
                    item.m_id     = t.value("id", std::to_string(autoId++));
                    item.m_status = t.value("status", "pending");
                    if (!isValidTodoStatus(item.m_status)) item.m_status = "pending";
                    items.push_back(std::move(item));
                }
            }
            agent.setTodos(std::move(items));
            // J3 接线：create 即写快照 + 清面板隐藏标志（新清单重新显示面板，§3.7）
            agent.markTodosDirty();
            agent.appendTodoSnapshotNow();
            agent.setTodoPanelDone(false);
            result["success"] = true;
            result["todos"]   = renderList();
        } else if (action == "update") {
            const std::string id = params.value("id", "");
            auto items = agent.getTodos();   // 取副本后整体写回
            bool found = false;
            for (auto& t : items) {
                if (t.m_id != id) continue;
                found = true;
                if (params.contains("status") && params["status"].is_string()) {
                    const auto st = params["status"].get<std::string>();
                    if (!isValidTodoStatus(st)) {
                        result["success"] = false;
                        result["error"]   = "status 非法（应为 pending/in_progress/completed）: " + st;
                        return result.dump();
                    }
                    t.m_status = st;
                }
                if (params.contains("content") && params["content"].is_string()) {
                    t.m_content = params["content"].get<std::string>();
                }
            }
            if (!found) {
                result["success"] = false;
                result["error"]   = "未找到 id=" + id + " 的待办项";
                return result.dump();
            }
            agent.setTodos(std::move(items));
            // J3 接线：update 即写快照（每次状态变化落盘，崩溃进度保留到最近一步）
            // + 清面板隐藏标志——跨轮场景"新回合清空"后面板须随 update 重现
            // （dsh projection 语义：任何 todo/write 事件重建投影，§3.3）
            agent.markTodosDirty();
            agent.appendTodoSnapshotNow();
            agent.setTodoPanelDone(false);
            result["success"] = true;
            result["todos"]   = renderList();
        } else {
            result["success"] = false;
            result["error"]   = "未知 action（应为 create/update/list/clear）: " + action;
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"]   = std::string("Handler error: ") + e.what();
    }
    return result.dump();
}

// S2-5: 网络抓取。注意 CLFWebFetch 内部不携带任何凭据（详见其头文件说明）
// A4a：脚手架收敛（容错语义保留：url 必填 → 报错）
std::string webFetchHandler(const std::string& args) {
    return detail::withHandlerScaffold(args, [](const nlohmann::json& params, nlohmann::json& result) {
        CLFWebRequest req;
        req.m_url        = params.value("url", "");
        req.m_method     = params.value("method", "GET");
        req.m_body       = params.value("body", "");
        req.m_timeoutSec = params.value("timeout", 15);
        if (params.contains("headers") && params["headers"].is_object()) {
            for (auto it = params["headers"].begin(); it != params["headers"].end(); ++it) {
                if (it.value().is_string()) {
                    req.m_headers[it.key()] = it.value().get<std::string>();
                }
            }
        }
        if (req.m_url.empty()) {
            result["success"] = false;
            result["error"]   = "url is required";
            return;
        }

        const auto resp = CLF::CLFTools::webFetch(req);
        result["success"] = resp.m_success;
        if (!resp.m_success) {
            result["error"] = resp.m_error;
            return;
        }
        result["status"]  = resp.m_status;
        result["headers"] = resp.m_headers;
        result["body"]    = resp.m_body;
        if (resp.m_truncated) result["truncated"] = true;
        if (resp.m_binary)    result["binary"]    = true;
    });
}

// A4a：脚手架收敛（三文件工具同款）
std::string writeFileHandler(const std::string& args) {
    return detail::withHandlerScaffold(args, [](const nlohmann::json& params, nlohmann::json& result) {
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
    });
}

std::string editFileHandler(const std::string& args) {
    return detail::withHandlerScaffold(args, [](const nlohmann::json& params, nlohmann::json& result) {
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
    });
}

std::string listDirectoryHandler(const std::string& args) {
    return detail::withHandlerScaffold(args, [](const nlohmann::json& params, nlohmann::json& result) {
        std::string path = params.value("path", ".");
        auto fileResult = CLF::CLFTools::listDirectory(path);
        result["success"] = fileResult.m_success;
        if (fileResult.m_success) {
            result["content"] = fileResult.m_content;
        } else {
            result["error"] = fileResult.m_error;
        }
    });
}

// A4a：脚手架收敛（cwd 边界容错语义保留）
std::string executeCommandHandler(const std::string& args) {
    return detail::withHandlerScaffold(args, [](const nlohmann::json& params, nlohmann::json& result) {
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
                return;
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
    });
}

} // anonymous namespace

// hpp 暴露的单测入口（qa_CLFBuiltinTools B4）——实现即 anonymous 内的
// todoWriteHandlerImplInternal；注册 lambda（本 TU 顶层）经此消歧
std::string todoWriteHandlerImpl(const std::string& args,
                                 CLF::CLFCore::CLFAgentLoop& agent) {
    return todoWriteHandlerImplInternal(args, agent);
}

// ============================================================================
// 注册入口
// ============================================================================

void registerBuiltinTools(CLF::CLFCore::CLFAgentLoop& agent) {
    using CLF::CLFCore::CLFTool;

    // —— 文件操作 ——
    CLFTool readFileTool;
    readFileTool.m_name        = "read_file";
    readFileTool.m_description = "读取文件内容（限工作区内，单文件上限 50MB，支持按行范围读取）";
    readFileTool.m_isRead      = true;  // B1：统计 read 桶
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
    listDirTool.m_isRead      = true;  // B1：统计 read 桶（B1-4 口径定案：readCount 与 progressReads 统一含 list）
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

    // —— 协作 ——
    // 待办不独立落盘，随会话持久化（见 CLFTypes::CLFTodoItem 说明）
    CLFTool todoTool;
    todoTool.m_name        = "todo_write";
    todoTool.m_description =
        "维护当前会话的待办清单。create 为整表替换；随会话保存，/resume 后自动恢复。"
        "状态取值：pending / in_progress / completed。"
        "继续已有任务时用 update（按 id 改状态），不要用 create 重建；"
        "create 为整表替换，仅用于全新清单";
    todoTool.m_risk        = CLF::CLFCore::CLFToolRisk::Read;
    todoTool.m_parametersSchema = R"({
        "type": "object",
        "properties": {
            "action": {"type": "string", "description": "create（整表替换）/ update / list / clear"},
            "todos": {
                "type": "array",
                "description": "create 用：待办数组，元素含 content（必填）、可选 id 与 status",
                "items": {
                    "type": "object",
                    "properties": {
                        "id": {"type": "string"},
                        "content": {"type": "string"},
                        "status": {"type": "string"}
                    }
                }
            },
            "id": {"type": "string", "description": "update 用：目标待办的 id"},
            "status": {"type": "string", "description": "update 用：pending / in_progress / completed"},
            "content": {"type": "string", "description": "update 用：新的内容文本"}
        },
        "required": ["action"]
    })";
    // 唯一捕获 agent 引用的 handler——见 todoWriteHandlerImpl 的自引用说明
    todoTool.m_handler = [&agent](const std::string& args) {
        return todoWriteHandlerImpl(args, agent);
    };
    agent.registerTool(todoTool);

    // —— 上下文压缩（S3-1，2026-09-02）——
    // 模型可主动调用：生成摘要 → 落盘 summary 行 → 注入系统提示（与自动触发同路径）
    // 第二个捕获 agent 引用的 handler（自引用约束同 todo_write）
    CLFTool compressTool;
    compressTool.m_name        = "compress_context";
    compressTool.m_description =
        "将当前会话压缩为结构化摘要并注入系统提示（释放上下文窗口）。"
        "自动触发受配置 agent.context_compression 开关控制";
    compressTool.m_risk = CLF::CLFCore::CLFToolRisk::Read;
    compressTool.m_parametersSchema = R"({"type":"object","properties":{}})";
    compressTool.m_handler = [&agent](const std::string&) {
        return agent.compressContextNow();
    };
    agent.registerTool(compressTool);

    // —— 网络 ——
    // 风险级取 Read：GET/HEAD 本质是读取，与 read_file 同级。
    // POST 有远端副作用，由 CLFToolExecutor 动态升级为强制确认（同 S2-2 模式）。
    CLFTool webFetchTool;
    webFetchTool.m_name        = "web_fetch";
    webFetchTool.m_description =
        "抓取 URL 内容。响应上限 1MB，正文按 head 8KB + tail 2KB 截断；"
        "二进制内容自动跳过。不会携带本机任何凭据";
    webFetchTool.m_risk        = CLF::CLFCore::CLFToolRisk::Read;
    webFetchTool.m_parametersSchema = R"({
        "type": "object",
        "properties": {
            "url": {"type": "string", "description": "完整 URL，形如 https://host/path"},
            "method": {"type": "string", "description": "GET（默认）/ POST / HEAD"},
            "headers": {"type": "object", "description": "可选的额外请求头"},
            "body": {"type": "string", "description": "POST 请求体"},
            "timeout": {"type": "integer", "description": "超时秒数，默认 15，上限 60"}
        },
        "required": ["url"]
    })";
    webFetchTool.m_handler = webFetchHandler;
    agent.registerTool(webFetchTool);

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
    searchTool.m_isSearch    = true;  // B1：统计 search 桶
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
    // A4a：脚手架收敛（错误文案统一 "Handler error: "——qa 无文案断言）
    searchTool.m_handler = [](const std::string& args) -> std::string {
        return detail::withHandlerScaffold(args, [](const nlohmann::json& params, nlohmann::json& result) {
            std::string pattern   = params.value("pattern", "");
            std::string directory = params.value("directory", ".");
            std::string fileTypes = params.value("fileTypes", "");
            result["success"] = true;
            result["content"] = searchContent(pattern, directory, fileTypes);
        });
    };
    agent.registerTool(searchTool);
}

} // namespace CLF::CLFTools
