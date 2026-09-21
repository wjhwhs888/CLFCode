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

#include <string>

#include "CLFCore/CLFAgentLoop.hpp"
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
            // 调用时查询：unload 后 getService nullptr → 兜底错误（验证点 4）
            tool.m_handler = [&manager, toolName](const std::string& args) -> std::string {
                auto* svc = manager.getService("tool.provider");
                if (!svc) {
                    return providerUnavailableError(toolName);
                }
                std::string content;
                CLFToolCallbacks cb{};
                cb.onResult = [](void* ctx, const char* s, size_t n) {
                    static_cast<std::string*>(ctx)->append(s, n);
                };
                // onError 两参签名（不可直接赋 onResult——2.2a 实抓）
                cb.onError = [](void* ctx, const char* s) {
                    static_cast<std::string*>(ctx)->append(s);
                };
                static_cast<ICLFToolProvider*>(svc)->callTool(
                    toolName.c_str(), args.c_str(), &content, &cb);
                return content;
            };
            agent.registerTool(std::move(tool));
            ++registered;
        }
    }
    CLFLogger::instance().info(std::string("[Plugin] registered ") +
                               std::to_string(registered) + " tool(s) from plugins");
}

} // namespace CLF::CLFTools
