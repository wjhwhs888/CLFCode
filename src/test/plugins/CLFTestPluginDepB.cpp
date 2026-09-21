// CLFTestPluginDepB.cpp — provides "svc.b" 的变体（P18 依赖满足正向拓扑用：
// depA requires "svc.b"，依赖方后于提供方 init——拓扑序错则 depA 的 init 拿不到）。

#include "TestPluginCommon.hpp"

namespace {
class DepBPlugin : public CLF::CLFPluginApi::CLFPlugin,
                   public CLF::CLFPluginApi::CLFService {
public:
    explicit DepBPlugin(const CLF::CLFPluginApi::CLFHostApi* host)
        : m_host(host) {
        m_services[0] = CLF::CLFPluginApi::CLFServiceEntry{
            "svc.b", static_cast<CLF::CLFPluginApi::CLFService*>(this)};
        m_services[1] = CLF::CLFPluginApi::CLFServiceEntry{nullptr, nullptr};
    }
    const char* name() const override { return "clf.teststub.depB"; }
    const char* version() const override { return "1.0.0"; }
    uint32_t hostApiVersion() const override {
        return CLF::CLFPluginApi::CLF_PLUGIN_API_VERSION;
    }
    const char* const* requiresServices() const override { return kNoRequires; }
    const CLF::CLFPluginApi::CLFServiceEntry* services() const override {
        return m_services;
    }
    bool init() override { return true; }
    void shutdown() override { (void)m_host; }

private:
    const CLF::CLFPluginApi::CLFHostApi* m_host;
    CLF::CLFPluginApi::CLFServiceEntry m_services[2];
    static constexpr const char* const kNoRequires[] = {nullptr};
};
} // namespace

extern "C" CLF_PLUGIN_EXPORT
CLF::CLFPluginApi::CLFPlugin* CLFPluginCreate(const CLF::CLFPluginApi::CLFHostApi* host) {
    return new DepBPlugin(host);
}
extern "C" CLF_PLUGIN_EXPORT
void CLFPluginDestroy(CLF::CLFPluginApi::CLFPlugin* plugin) {
    delete plugin;
}
