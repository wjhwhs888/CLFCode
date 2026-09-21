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
#include "CLFCapabilities/FileOps/CLFFileOps.hpp"
#include "CLFCapabilities/FileOps/CLFFileOpsHandlers.hpp"   // 2.2a：4 工具 handler 共享实现
#include "CLFTools/CLFCommandToolHandler.hpp"   // 2.3：exitCodeMeansSuccess 共享实现
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
// 2.2a：实现转调能力域参数化版（工作区根 = ConfigLoader 取——本层补根，
// 判定逻辑单点；qa 钉子签名不变）
// example:
//   std::string err;
//   if (!detail::isWithinWorkspace(path, err)) return err;
bool isWithinWorkspace(const std::string& path, std::string& outError) {
    return CLF::CLFCore::CLFTextUtil::isWithinWorkspaceOf(
        CLF::CLFCore::CLFConfigLoader::getWorkingDir(), path, outError);
}

//判定命令的退出码是否应视为成功（S2-3 退出码白名单）
// 2.3：实现迁共享 CLFCommandToolHandler（双消费者）；qa 钉子签名不变
// example:
//   exitCodeMeansSuccess("grep foo a.txt", 1);  // true
bool exitCodeMeansSuccess(const std::string& command, int exitCode) {
    return CLF::CLFTools::exitCodeMeansSuccess(command, exitCode);
}

//按行切片（offset 为 0 基起始行，limit<=0 表示取到末尾）
// 2.2a：实现归位 CLFTextUtil（单点逻辑；qa 钉子签名不变）
// example:
//   sliceLines(content, 10, 20);  // 第 10-29 行
std::string sliceLines(const std::string& content, int offset, int limit) {
    return CLF::CLFCore::CLFTextUtil::sliceLines(content, offset, limit);
}

// A4a：handler 脚手架（2.2a：实现迁至能力域共享——CLFBuiltinTools 与插件
// 双消费；本处 using 引入，调用点零改）
using CLF::CLFCapabilities::withHandlerScaffold;

} // namespace detail

namespace {

std::string getCurrentTimeHandler(const std::string& /*args*/) {
    // A2：平台 ifdef → CLFTextUtil::localNow（格式零变化）
    return CLFTextUtil::localNow("%Y-%m-%d %H:%M:%S");
}

std::string echoHandler(const std::string& args) {
    return "Echo: " + args;
}

// read_file 的 handler 实现已迁至能力域共享实现 CLFCapabilities::readFileToolHandler
// （2.2a，注册处见下——workspaceRoot 传 ConfigLoader 值，主程序行为零变化）

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

// web_fetch / execute_command 的 handler 实现已迁共享实现
// （2.3：CLFWebToolHandler / CLFCommandToolHandler——CLFBuiltinTools 与插件
// 双消费；注册处见下）

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

    // —— 文件操作（2.2b：read/write/edit/list 4 工具已随 FileOps 域迁插件——
    // 经 registerPluginTools 装配注册；共享 handler 保留为插件消费者）——

    // —— 系统操作（2.3：execute_command 已随 Command 域迁插件——经
    // registerPluginTools 装配注册；共享 handler 保留为插件消费者）——

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

    // —— 网络（2.3：web_fetch 已随 Web 域迁插件——经 registerPluginTools
    // 装配注册；共享 handler 保留为插件消费者）——

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

    // —— search_content（2.3：已随 Search 域迁插件——经 registerPluginTools
    // 装配注册；共享 handler 保留为插件消费者）——
}

} // namespace CLF::CLFTools
