// CLFTestPluginSvc1.cpp — 与 svc2 声明同名服务 "svc.dup" 的变体（P14 多提供方
// 歧义用：文件名字典序 clf.teststub.svc1 < clf.teststub.svc2 → 首个 = 本插件）。
// describe() 返回值供 qa 侧断言"取到的是 svc1 的实例"。

#include "TestPluginCommon.hpp"

namespace {
class Svc1Plugin : public CLF::CLFPluginApi::CLFPlugin,
                   public clftest::ITestSecondService {
public:
    explicit Svc1Plugin(const CLF::CLFPluginApi::CLFHostApi* host)
        : m_host(host) {
        m_services[0] = CLF::CLFPluginApi::CLFServiceEntry{
            "svc.dup", static_cast<clftest::ITestSecondService*>(this)};
        m_services[1] = CLF::CLFPluginApi::CLFServiceEntry{nullptr, nullptr};
    }
    const char* name() const override { return "clf.teststub.svc1"; }
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
    const char* describe() const override { return "svc1"; }

private:
    const CLF::CLFPluginApi::CLFHostApi* m_host;
    CLF::CLFPluginApi::CLFServiceEntry m_services[2];
    static constexpr const char* const kNoRequires[] = {nullptr};
};
} // namespace

extern "C" CLF_PLUGIN_EXPORT
CLF::CLFPluginApi::CLFPlugin* CLFPluginCreate(const CLF::CLFPluginApi::CLFHostApi* host) {
    return new Svc1Plugin(host);
}
extern "C" CLF_PLUGIN_EXPORT
void CLFPluginDestroy(CLF::CLFPluginApi::CLFPlugin* plugin) {
    delete plugin;
}
