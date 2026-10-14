// CLFPluginTools.cpp — 插件工具装配注册（2.2b，2026-09-21）
// 从插件管理器收集 tool.provider 提供者的工具元数据（CLFToolMetaPOD）→
// 装配 CLFTool → AgentLoop 注册。core 2 工具（todo_write/compress_context）
// 保持静态注册（registerBuiltinTools）。
//
// handler 捕获 manager 引用、**调用时经 getService 查询**（不缓存服务指针——
// §1.2 禁跨 unload 缓存；卸载后查询 nullptr → 兜底错误文本，
// 分册验证点 4 "停用后模型再调 → 明确错误"的自然实现）。
// example:
//   CLF::CLFTools::registerPluginTools(agent, pluginManager);

#include "CLFTools/CLFBuiltinTools.hpp"

#include <atomic>
#include <string>

#include <nlohmann/json.hpp>

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFDangerousCommandDetector.hpp"   // 分册 3：execute_command 逐参数判定
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFPluginManager.hpp"
#include "CLFPluginApi/CLFToolApi.hpp"

namespace CLF::CLFTools {

using CLF::CLFCore::CLFLogger;

namespace {

// 兜底错误 JSON（与 formatToolResult 的 error 判定同构——模型看到错误自兜底）
std::string providerUnavailableError(const std::string& toolName) {
    return std::string(R"({"success":false,"error":"工具提供者不可用（插件已停用）: )") +
           toolName + "\"}";
}

// 宿主内部调用上下文（2026-09-23 中断时效性 A 批）：结果收集器 + 中断标志。
// 经 ABI ctx 传给插件——插件侧义务不变（只原样回传，不得解引用）。
// error 标志（2026-10-14 实机实抓修复）：插件 catch 路径经 onError 回传
// "internal error" 裸文本——此前直接作为结果入库被 formatToolResult
// 误判成功（非 JSON catch 分支 → ok=true）。修复：onError 触发即失败，
// 返回结构化失败 JSON（模型可见错误语义自兜底）
struct CLFToolCallCtx {
    std::string*            out;
    const std::atomic<bool>* interrupt;
    bool                    error = false;
};

// ABI isCancelled 回调：读宿主中断标志（可为空指针 = 宿主未提供取消 → false）
bool cbIsCancelled(void* ctx) {
    const auto* c = static_cast<CLFToolCallCtx*>(ctx);
    return c->interrupt && c->interrupt->load();
}

// 并发安全声明（分册 3 第三批，设计 §2.1 首批清单）：宿主侧按名字打标——
// 插件 ABI 元数据不携带并发语义（纯调度元数据不进 ABI/描述/schema——
// dsh §2.3"不泄漏给模型"）。返回 nullptr = 独占（fail-closed）
std::function<bool(const std::string&)> concurrencyDeclaration(const std::string& name) {
    // 读类/搜索/网络（无副作用无共享状态）→ 恒并行
    if (name == "list_directory" || name == "read_file"
        || name == "search_content" || name == "web_fetch")
        return [](const std::string&) { return true; };
    // execute_command：逐参数判定——命令命中危险模式（写特征）→ 独占、
    // 其余并行。复用 CLFDangerousCommandDetector 静态判定的反向
    // （设计 §2.1）；参数解析失败 → 独占（fail-closed）
    if (name == "execute_command")
        return [](const std::string& argsJson) {
            try {
                auto args = nlohmann::json::parse(argsJson);
                return !CLF::CLFCore::CLFDangerousCommandDetector::isDangerous(
                    args.value("command", ""), {});
            } catch (...) {
                return false;
            }
        };
    return nullptr;   // 写类（write_file/edit_file）等 → 独占
}

} // namespace

void registerPluginTools(CLF::CLFCore::CLFAgentLoop& agent,
                         CLF::CLFCore::CLFPluginManager& manager) {
    using CLF::CLFCore::CLFTool;
    using CLF::CLFPluginApi::CLFToolCallbacks;
    using CLF::CLFPluginApi::ICLFToolProvider;

    int registered = 0;
    for (auto* provider : manager.toolProviders()) {
        for (int i = 0; i < provider->toolCount(); ++i) {
            const auto* meta = provider->toolMeta(i);
            if (!meta) {
                continue;
            }
            // 工具名查重（防御：与 core 静态注册冲突 → 跳过 + warn）
            const std::string toolName = meta->name;
            bool dup = false;
            for (const auto& t : agent.getTools()) {
                if (t.m_name == toolName) {
                    dup = true;
                    break;
                }
            }
            if (dup) {
                CLFLogger::instance().warn(std::string("[Plugin] tool '") + toolName +
                                           "' already registered, skip");
                continue;
            }

            CLFTool tool;
            tool.m_name             = toolName;
            tool.m_description      = meta->description;
            tool.m_parametersSchema = meta->parametersSchema;
            tool.m_risk             = static_cast<CLF::CLFCore::CLFToolRisk>(meta->risk);
            tool.m_isRead           = (meta->flags & CLF::CLFPluginApi::ToolFlagRead) != 0;
            tool.m_isSearch         = (meta->flags & CLF::CLFPluginApi::ToolFlagSearch) != 0;
            tool.m_concludesTurn    = meta->concludesTurn != 0;
            // 分册 3 第三批：并发安全声明（设计 §2.1 首批——宿主侧打标）
            tool.m_isConcurrencySafe = concurrencyDeclaration(toolName);
            // 调用时查询：**按工具名精确路由**——遍历提供者找"声明了该工具名"的
            // provider（不笼统取 getService("tool.provider") 的字典序首个——
            // 2026-09-21 实机实抓：多提供者场景 read_file 被路由到 teststub
            // 回显参数，工具结果错乱）。不缓存服务指针（§1.2）；
            // 卸载后无提供者 → 兜底错误（验证点 4）
            // 中断标志装配时捕获（A 批步骤 2）：经 ABI isCancelled 下传——插件
            // 侧义务不变（ctx 原样回传）；指针随 AgentLoop 生命周期有效
            const std::atomic<bool>* flag = agent.interruptFlag();
            tool.m_handler = [&manager, toolName, flag](const std::string& args) -> std::string {
                for (auto* provider : manager.toolProviders()) {
                    for (int idx = 0; idx < provider->toolCount(); ++idx) {
                        const auto* m = provider->toolMeta(idx);
                        if (m && std::string(m->name) == toolName) {
                            CLFToolCallCtx ctx{};
                            std::string content;
                            ctx.out       = &content;
                            ctx.interrupt = flag;
                            CLFToolCallbacks cb{};
                            cb.onResult = [](void* ctx, const char* s, size_t n) {
                                static_cast<CLFToolCallCtx*>(ctx)->out->append(s, n);
                            };
                            // onError 两参签名（不可直接赋 onResult——2.2a 实抓）
                            cb.onError = [](void* ctx, const char* s) {
                                auto* c = static_cast<CLFToolCallCtx*>(ctx);
                                c->error = true;
                                c->out->append(s);
                            };
                            cb.isCancelled = cbIsCancelled;
                            provider->callTool(toolName.c_str(), args.c_str(), &ctx, &cb);
                            // 插件异常路径（onError 触发）：结构化失败 JSON——
                            // 模型可见错误语义自兜底（此前裸文本被误判成功，
                            // 2026-10-14 实机实抓 "internal error" 入库修复）
                            if (ctx.error) {
                                return nlohmann::json{
                                    {"success", false},
                                    {"error", content},
                                }.dump();
                            }
                            return content;
                        }
                    }
                }
                return providerUnavailableError(toolName);
            };
            agent.registerTool(std::move(tool));
            ++registered;
        }
    }
    CLFLogger::instance().info(std::string("[Plugin] registered ") +
                               std::to_string(registered) + " tool(s) from plugins");
}

} // namespace CLF::CLFTools
