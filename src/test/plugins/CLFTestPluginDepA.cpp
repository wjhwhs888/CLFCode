// CLFTestPluginDepA.cpp — requires "svc.b" 的变体（P18 依赖满足正向拓扑用）。
// init() 锚点：getService("svc.b") 非空才返回 true——依赖方后于提供方 init
// （拓扑序错误则 init 失败 → 被禁用，qa 断言加载成功即证明拓扑序正确）。

#include "TestPluginCommon.hpp"

namespace {
class DepAPlugin : public CLF::CLFPluginApi::CLFPlugin {
public:
    explicit DepAPlugin(const CLF::CLFPluginApi::CLFHostApi* host)
        : m_host(host) {}
    const char* name() const override { return "clf.teststub.depA"; }
    const char* version() const override { return "1.0.0"; }
    uint32_t hostApiVersion() const override {
        return CLF::CLFPluginApi::CLF_PLUGIN_API_VERSION;
    }
    const char* const* requiresServices() const override { return kRequires; }
    const CLF::CLFPluginApi::CLFServiceEntry* services() const override {
        return kNoServices;
    }
    bool init() override { return m_host->getService("svc.b") != nullptr; }
    void shutdown() override {}

private:
    const CLF::CLFPluginApi::CLFHostApi* m_host;
    static constexpr const char* const kRequires[] = {"svc.b", nullptr};
    static constexpr CLF::CLFPluginApi::CLFServiceEntry kNoServices[] = {{nullptr, nullptr}};
};
} // namespace

extern "C" CLF_PLUGIN_EXPORT
CLF::CLFPluginApi::CLFPlugin* CLFPluginCreate(const CLF::CLFPluginApi::CLFHostApi* host) {
    return new DepAPlugin(host);
}
extern "C" CLF_PLUGIN_EXPORT
void CLFPluginDestroy(CLF::CLFPluginApi::CLFPlugin* plugin) {
    delete plugin;
}
