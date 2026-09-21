// CLFSearchPlugin.cpp — tools.search.dll 工厂与插件壳（2.3，2026-09-21）
// CLFPlugin + ICLFToolProvider 双继承；search_content 随域打包（零 core 依赖）。

#include <cstring>
#include <string>

#include "CLFPluginApi/CLFPluginApi.hpp"
#include "CLFPluginApi/CLFToolApi.hpp"
#include "CLFTools/CLFSearchToolHandler.hpp"

namespace {

using namespace CLF::CLFPluginApi;

class CLFSearchPlugin : public CLFPlugin,
                        public ICLFToolProvider {
public:
    explicit CLFSearchPlugin(const CLFHostApi* host)
        : m_host(host) {
        m_services[0] = CLFServiceEntry{
            "tool.provider", static_cast<ICLFToolProvider*>(this)};
        m_services[1] = CLFServiceEntry{nullptr, nullptr};
    }

    const char* name() const override { return "tools.search"; }
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
            if (std::strcmp(name, "search_content") == 0) {
                out = CLF::CLFTools::searchContentToolHandler(argsJson);
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
        "search_content",
        "在目录中搜索文件内容（纯文本匹配）。省略 fileTypes 时只搜常见文本扩展名（不扫二进制）；"
        "跳过 .git/node_modules/build/cmake-build-* 等目录，跳过 >1MB 文件，结果上限 500 行",
        R"({
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
    })",
        /*risk=*/0, /*flags=*/ToolFlagSearch, /*concludesTurn=*/0};
};

} // namespace

extern "C" CLF_PLUGIN_EXPORT
CLF::CLFPluginApi::CLFPlugin* CLFPluginCreate(const CLF::CLFPluginApi::CLFHostApi* host) {
    return new CLFSearchPlugin(host);
}
extern "C" CLF_PLUGIN_EXPORT
void CLFPluginDestroy(CLF::CLFPluginApi::CLFPlugin* plugin) {
    delete plugin;
}
