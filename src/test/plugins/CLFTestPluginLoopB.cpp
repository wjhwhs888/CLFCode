// CLFTestPluginLoopB.cpp — 与 loopA 互相 requires 成环的变体（P15 依赖环用）。
// 纯服务插件形态（无工具）：CLFPlugin + CLFService 多继承，provides "svc.loopB"。

#include "TestPluginCommon.hpp"

namespace {
class LoopBPlugin : public CLF::CLFPluginApi::CLFPlugin,
                    public CLF::CLFPluginApi::CLFService {
public:
    explicit LoopBPlugin(const CLF::CLFPluginApi::CLFHostApi* host)
        : m_host(host) {
        m_services[0] = CLF::CLFPluginApi::CLFServiceEntry{
            "svc.loopB", static_cast<CLF::CLFPluginApi::CLFService*>(this)};
        m_services[1] = CLF::CLFPluginApi::CLFServiceEntry{nullptr, nullptr};
    }
    const char* name() const override { return "clf.teststub.loopB"; }
    const char* version() const override { return "1.0.0"; }
    uint32_t hostApiVersion() const override {
        return CLF::CLFPluginApi::CLF_PLUGIN_API_VERSION;
    }
    const char* const* requiresServices() const override { return kRequires; }
    const CLF::CLFPluginApi::CLFServiceEntry* services() const override {
        return m_services;
    }
    bool init() override { return true; }
    void shutdown() override { (void)m_host; }

private:
    const CLF::CLFPluginApi::CLFHostApi* m_host;
    CLF::CLFPluginApi::CLFServiceEntry m_services[2];
    static constexpr const char* const kRequires[] = {"svc.loopA", nullptr};
};
} // namespace

extern "C" CLF_PLUGIN_EXPORT
CLF::CLFPluginApi::CLFPlugin* CLFPluginCreate(const CLF::CLFPluginApi::CLFHostApi* host) {
    return new LoopBPlugin(host);
}
extern "C" CLF_PLUGIN_EXPORT
void CLFPluginDestroy(CLF::CLFPluginApi::CLFPlugin* plugin) {
    delete plugin;
}
