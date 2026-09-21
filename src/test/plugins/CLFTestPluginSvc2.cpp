// CLFTestPluginSvc2.cpp — 与 svc1 声明同名服务 "svc.dup" 的变体（P14 多提供方
// 歧义用：文件名字典序排在 svc1 之后 → 查询应取 svc1 的实例，本插件仅作第二提供方）。
// describe() 返回值供 qa 侧断言"取到的是 svc1 的实例"（改名复测顺序稳定）。

#include "TestPluginCommon.hpp"

namespace {
class Svc2Plugin : public CLF::CLFPluginApi::CLFPlugin,
                   public clftest::ITestSecondService {
public:
    explicit Svc2Plugin(const CLF::CLFPluginApi::CLFHostApi* host)
        : m_host(host) {
        m_services[0] = CLF::CLFPluginApi::CLFServiceEntry{
            "svc.dup", static_cast<clftest::ITestSecondService*>(this)};
        m_services[1] = CLF::CLFPluginApi::CLFServiceEntry{nullptr, nullptr};
    }
    const char* name() const override { return "clf.teststub.svc2"; }
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
    const char* describe() const override { return "svc2"; }

private:
    const CLF::CLFPluginApi::CLFHostApi* m_host;
    CLF::CLFPluginApi::CLFServiceEntry m_services[2];
    static constexpr const char* const kNoRequires[] = {nullptr};
};
} // namespace

extern "C" CLF_PLUGIN_EXPORT
CLF::CLFPluginApi::CLFPlugin* CLFPluginCreate(const CLF::CLFPluginApi::CLFHostApi* host) {
    return new Svc2Plugin(host);
}
extern "C" CLF_PLUGIN_EXPORT
void CLFPluginDestroy(CLF::CLFPluginApi::CLFPlugin* plugin) {
    delete plugin;
}
