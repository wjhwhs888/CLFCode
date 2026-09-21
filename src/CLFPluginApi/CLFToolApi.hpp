// CLFToolApi.hpp — 工具插件域接口（clf_plugin_api，唯一跨 DLL 共享头之一）
// 阶段 2 分册 §3.1③ 细化：工具元数据 POD + 调用回调 + 提供者接口。
// 与进程内 CLFTool（CLFTypes.hpp）同构，但无 std::function / 无 STL。
//
// 调用语义（与现有 handler 等价，宿主侧唯一调用点 CLFToolExecutor.cpp）：
//   进程内：std::string m_content = it->m_handler(call.m_arguments);
//   跨边界：callTool(name, argsJson, ctx, cb) → cb->onResult 或 cb->onError
//   **两者产出的文本都走既有 formatToolResult 判定 ok/error——行为等价**
//
// 同步契约（2026-09-21）：callTool 为同步阻塞调用——回调必须在返回前完成；
// ctx 由宿主调用栈持有（CLFToolExecutor 的局部 std::string），
// 插件实现不得在返回后异步补调回调（阶段 3 集成插件的 IPC 须自行阻塞等待）；
// 失败/超时经 onError 文本表达，本 ABI 无取消通道
//
// example（宿主侧）：
//   std::string content;
//   CLFToolCallbacks cb{};
//   cb.onResult = [](void* ctx, const char* s, size_t n) {
//       static_cast<std::string*>(ctx)->assign(s, n);
//   };
//   cb.onError  = cb.onResult;              // 错误文本同样是结果文本
//   provider->callTool("read_file", args.c_str(), &content, &cb);

#pragma once
#include <cstddef>
#include "CLFPluginApi/CLFPluginApi.hpp"

namespace CLF::CLFPluginApi {

// ============================================================================
// 工具元数据（POD，跨边界安全）
// ============================================================================

// 能力标签位集（阶段 1 B1 的 m_isSearch/m_isRead 映射至此；写/命令分类不经标签——
// CLFToolRisk 即能力声明，B1 设计简化）
enum CLFToolFlags : int {
    ToolFlagNone   = 0,
    ToolFlagSearch = 1 << 0,   // 计入 searchCount 桶（search_content）
    ToolFlagRead   = 1 << 1,   // 计入 readCount/progressReads 桶（read_file / list_directory）
};

// 与进程内 CLFTool 字段一一对应；字符串生命周期 = 插件加载期间（宿主只读不持有）
struct CLFToolMetaPOD {
    const char* name;              // 工具名（全局唯一，管理器装配时查重）
    const char* description;
    const char* parametersSchema;  // JSON Schema 字符串
    int risk;                      // CLFToolRisk 编码（0=Read 1=Write 2=Command）
    int flags;                     // CLFToolFlags 位集
    int concludesTurn;             // 0/1（A5 先例；仅 handler 成功路径生效）
};

// ============================================================================
// 调用回调（宿主提供，经 callTool 参数传入）
// ============================================================================

struct CLFToolCallbacks {
    // 结果文本（成功路径；UTF-8，调用期间有效）。宿主负责走 formatToolResult
    void (*onResult)(void* ctx, const char* content, size_t len) = nullptr;
    // 错误文本（失败/异常被兜底；同样作为结果文本交给 formatToolResult）；
    // 实现侧调用前判空（可为 null）
    void (*onError)(void* ctx, const char* msg) = nullptr;
};

// ============================================================================
// 工具提供者（工具插件对宿主暴露的领域接口）
// ============================================================================

class ICLFToolProvider : public CLFService {
public:
    virtual ~ICLFToolProvider() = default;

    virtual int toolCount() const = 0;

    // index ∈ [0, toolCount())；返回指针由插件持有，生命周期 = 插件加载期间
    virtual const CLFToolMetaPOD* toolMeta(int index) const = 0;

    // 调用：argsJson 进，结果经 cb 出；返回 false = 调用未完成（onError 已推文本）
    virtual bool callTool(const char* name, const char* argsJson,
                          void* ctx, const CLFToolCallbacks* cb) = 0;
};

} // namespace CLF::CLFPluginApi
