// CLFCommandPlugin.cpp — tools.command.dll 工厂与插件壳（2.3，2026-09-21）
// CLFPlugin + ICLFToolProvider 双继承；execute_command 随域打包。
// cwd 边界校验经 host->config 取根（2.2b 定案的校验归属模式：保持 handler 层
// 校验语义 S2-1）。多继承子对象约束见 2.1 §1.2。

#include <cstring>
#include <string>

#include "CLFPluginApi/CLFPluginApi.hpp"
#include "CLFPluginApi/CLFToolApi.hpp"
#include "CLFTools/CLFCommandToolHandler.hpp"

namespace {

using namespace CLF::CLFPluginApi;

class CLFCommandPlugin : public CLFPlugin,
                         public ICLFToolProvider {
public:
    explicit CLFCommandPlugin(const CLFHostApi* host)
        : m_host(host) {
        m_services[0] = CLFServiceEntry{
            "tool.provider", static_cast<ICLFToolProvider*>(this)};
        m_services[1] = CLFServiceEntry{nullptr, nullptr};
    }

    // —— CLFPlugin ——
    const char* name() const override { return "tools.command"; }
    const char* version() const override { return "1.0.0"; }
    uint32_t hostApiVersion() const override { return CLF_PLUGIN_API_VERSION; }
    const char* const* requiresServices() const override { return kNoRequires; }
    const CLFServiceEntry* services() const override { return m_services; }
    bool init() override {
        // 2.3：cwd 校验的 workspaceRoot 经 host->config（同 fileops 2.2b 模式）；
        // 缺失 → 空串 = 跳过校验
        const char* root = m_host ? m_host->config(name(), "workspace_root") : nullptr;
        m_workspaceRoot  = root ? root : "";
        // 命令执行层 §10.2（2026-09-23）：超时配置经宿主级键读取并缓存为成员
        // （照 m_workspaceRoot 先例——不在每次 callTool 里跨边界查询）；
        // 缺失/非法 → 内置默认 120/600（宿主静态缓存同值）
        const char* defSec = m_host ? m_host->config(name(), "command_default_timeout_sec") : nullptr;
        const char* maxSec = m_host ? m_host->config(name(), "command_max_timeout_sec") : nullptr;
        m_defaultTimeoutSec = parsePositiveInt(defSec, 120);
        m_maxTimeoutSec     = parsePositiveInt(maxSec, 600);
        return true;
    }
    void shutdown() override {
        if (m_host) {
            (void)m_host->apiVersion();
        }
    }

    // —— ICLFToolProvider ——
    int toolCount() const override { return 1; }
    const CLFToolMetaPOD* toolMeta(int index) const override {
        return index == 0 ? &kMeta : nullptr;
    }
    bool callTool(const char* name, const char* argsJson, void* ctx,
                  const CLFToolCallbacks* cb) override {
        try {
            std::string out;
            if (std::strcmp(name, "execute_command") == 0) {
                // 取消查询组装（中断时效性 A 批步骤 5）：ABI cb.isCancelled → 轻量
                // 查询下传 handler → 执行器。ctx 不透明只回传；同步契约下
                // ctx/cb 在调用栈存活期内有效（callTool 返回前 handler 已返回）
                std::function<bool()> cancelQuery;
                if (cb && cb->isCancelled) {
                    cancelQuery = [cb, ctx] { return cb->isCancelled(ctx); };
                }
                out = CLF::CLFTools::executeCommandToolHandler(
                    argsJson, m_workspaceRoot, cancelQuery,
                    m_defaultTimeoutSec, m_maxTimeoutSec);
            } else {
                out = std::string("{\"success\":false,\"error\":\"unknown tool: ") + name + "\"}";
            }
            if (cb && cb->onResult) {
                cb->onResult(ctx, out.c_str(), out.size());
            }
            return true;
        } catch (...) {   // 异常零逸出
            if (cb && cb->onError) {
                cb->onError(ctx, "internal error");
            }
            return false;
        }
    }

private:
    // 正整数解析（config 字符串 → int）；空/非法 → 回退内置默认
    static int parsePositiveInt(const char* s, int fallback) {
        if (!s || !s[0]) return fallback;
        try {
            const int v = std::stoi(s);
            return v > 0 ? v : fallback;
        } catch (...) {
            return fallback;
        }
    }

    const CLFHostApi* m_host;
    CLFServiceEntry m_services[2];
    std::string m_workspaceRoot;   // init 经 host->config 取（空 = 跳过 cwd 校验）
    int m_defaultTimeoutSec = 120; // 命令执行层 §10.2：init 缓存（默认 120）
    int m_maxTimeoutSec     = 600; // 配置上限（默认 600；handler 层 min clamp）

    static constexpr const char* const kNoRequires[] = {nullptr};
    // 元数据与 CLFBuiltinTools 静态注册同文案同值
    static constexpr CLFToolMetaPOD kMeta{
        "execute_command",
        "执行 Shell 命令并返回输出。grep/rg/findstr/diff/fc 的退出码 1 视为成功（无匹配/有差异是正常结果）",
        R"({
        "type": "object",
        "properties": {
            "command": {"type": "string", "description": "要执行的命令"},
            "timeout": {"type": "integer", "description": "超时秒数，默认 120（上限 600 可配置）"},
            "cwd": {"type": "string", "description": "工作目录（须位于工作区内），省略则用当前目录"}
        },
        "required": ["command"]
    })",
        /*risk=*/2, /*flags=*/ToolFlagNone, /*concludesTurn=*/0};
};

} // namespace

extern "C" CLF_PLUGIN_EXPORT
CLF::CLFPluginApi::CLFPlugin* CLFPluginCreate(const CLF::CLFPluginApi::CLFHostApi* host) {
    return new CLFCommandPlugin(host);
}
extern "C" CLF_PLUGIN_EXPORT
void CLFPluginDestroy(CLF::CLFPluginApi::CLFPlugin* plugin) {
    delete plugin;
}
