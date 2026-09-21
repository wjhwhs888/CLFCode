// CLFWebPlugin.cpp — tools.web.dll 工厂与插件壳（2.3，2026-09-21）
// CLFPlugin + ICLFToolProvider 双继承；web_fetch 随域打包。
// 能力（CLFWebFetch）内部不携带任何凭据（S2-5 定案——与 CLFHttpClient 隔离，
// 防止向第三方 URL 泄漏 API key）。

#include <cstring>
#include <string>

#include "CLFPluginApi/CLFPluginApi.hpp"
#include "CLFPluginApi/CLFToolApi.hpp"
#include "CLFTools/CLFWebToolHandler.hpp"

namespace {

using namespace CLF::CLFPluginApi;

class CLFWebPlugin : public CLFPlugin,
                     public ICLFToolProvider {
public:
    explicit CLFWebPlugin(const CLFHostApi* host)
        : m_host(host) {
        m_services[0] = CLFServiceEntry{
            "tool.provider", static_cast<ICLFToolProvider*>(this)};
        m_services[1] = CLFServiceEntry{nullptr, nullptr};
    }

    const char* name() const override { return "tools.web"; }
    const char* version() const override { return "1.0.0"; }
    uint32_t hostApiVersion() const override { return CLF_PLUGIN_API_VERSION; }
    const char* const* requiresServices() const override { return kNoRequires; }
    const CLFServiceEntry* services() const override { return m_services; }
    bool init() override { return true; }
    void shutdown() override {
        if (m_host) {
            (void)m_host->apiVersion();
        }
    }

    int toolCount() const override { return 1; }
    const CLFToolMetaPOD* toolMeta(int index) const override {
        return index == 0 ? &kMeta : nullptr;
    }
    bool callTool(const char* name, const char* argsJson, void* ctx,
                  const CLFToolCallbacks* cb) override {
        try {
            std::string out;
            if (std::strcmp(name, "web_fetch") == 0) {
                out = CLF::CLFTools::webFetchToolHandler(argsJson);
            } else {
                out = std::string("{\"success\":false,\"error\":\"unknown tool: ") + name + "\"}";
            }
            if (cb && cb->onResult) {
                cb->onResult(ctx, out.c_str(), out.size());
            }
            return true;
        } catch (...) {
            if (cb && cb->onError) {
                cb->onError(ctx, "internal error");
            }
            return false;
        }
    }

private:
    const CLFHostApi* m_host;
    CLFServiceEntry m_services[2];

    static constexpr const char* const kNoRequires[] = {nullptr};
    static constexpr CLFToolMetaPOD kMeta{
        "web_fetch",
        "抓取 URL 内容。响应上限 1MB，正文按 head 8KB + tail 2KB 截断；"
        "二进制内容自动跳过。不会携带本机任何凭据",
        R"({
        "type": "object",
        "properties": {
            "url": {"type": "string", "description": "完整 URL，形如 https://host/path"},
            "method": {"type": "string", "description": "GET（默认）/ POST / HEAD"},
            "headers": {"type": "object", "description": "可选的额外请求头"},
            "body": {"type": "string", "description": "POST 请求体"},
            "timeout": {"type": "integer", "description": "超时秒数，默认 15，上限 60"}
        },
        "required": ["url"]
    })",
        /*risk=*/0, /*flags=*/ToolFlagNone, /*concludesTurn=*/0};
};

} // namespace

extern "C" CLF_PLUGIN_EXPORT
CLF::CLFPluginApi::CLFPlugin* CLFPluginCreate(const CLF::CLFPluginApi::CLFHostApi* host) {
    return new CLFWebPlugin(host);
}
extern "C" CLF_PLUGIN_EXPORT
void CLFPluginDestroy(CLF::CLFPluginApi::CLFPlugin* plugin) {
    delete plugin;
}
