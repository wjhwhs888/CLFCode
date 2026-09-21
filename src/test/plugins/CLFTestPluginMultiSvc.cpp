// CLFTestPluginMultiSvc.cpp — 单插件实现两个服务接口的变体（P13 多继承
// CLFService 子对象约束用）：tool.provider + test.second

#include "TestPluginCommon.hpp"

namespace {
class MultiSvcPlugin : public clftest::TestToolProvider,
                       public clftest::ITestSecondService {
public:
    explicit MultiSvcPlugin(const CLF::CLFPluginApi::CLFHostApi* host)
        : TestToolProvider(host) {
        // 每个服务表项须指向其对应接口视角的 CLFService 基类子对象——
        // 多继承下 this 到各基类子对象偏移不同（编译期已知），禁止复用同一指针
        m_svcs[0] = CLF::CLFPluginApi::CLFServiceEntry{
            "tool.provider", static_cast<CLF::CLFPluginApi::ICLFToolProvider*>(this)};
        m_svcs[1] = CLF::CLFPluginApi::CLFServiceEntry{
            "test.second", static_cast<clftest::ITestSecondService*>(this)};
        m_svcs[2] = CLF::CLFPluginApi::CLFServiceEntry{nullptr, nullptr};
    }

    const char* name() const override { return "clf.teststub.multisvc"; }
    const CLF::CLFPluginApi::CLFServiceEntry* services() const override { return m_svcs; }
    const char* describe() const override { return "second service ok"; }

private:
    CLF::CLFPluginApi::CLFServiceEntry m_svcs[3];
};
} // namespace

extern "C" CLF_PLUGIN_EXPORT
CLF::CLFPluginApi::CLFPlugin* CLFPluginCreate(const CLF::CLFPluginApi::CLFHostApi* host) {
    return new MultiSvcPlugin(host);
}
extern "C" CLF_PLUGIN_EXPORT
void CLFPluginDestroy(CLF::CLFPluginApi::CLFPlugin* plugin) {
    delete plugin;
}
