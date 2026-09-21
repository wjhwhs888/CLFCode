// CLFTestPluginBadAbi.cpp — hostApiVersion() 返回 999 的变体（P8 版本闸门用；
// P16 reload 失败复用）

#include "TestPluginCommon.hpp"

namespace {
class BadAbiPlugin : public clftest::TestToolProvider {
public:
    using TestToolProvider::TestToolProvider;
    const char* name() const override { return "clf.teststub.badabi"; }
    uint32_t hostApiVersion() const override { return 999; }
};
} // namespace

extern "C" CLF_PLUGIN_EXPORT
CLF::CLFPluginApi::CLFPlugin* CLFPluginCreate(const CLF::CLFPluginApi::CLFHostApi* host) {
    return new BadAbiPlugin(host);
}
extern "C" CLF_PLUGIN_EXPORT
void CLFPluginDestroy(CLF::CLFPluginApi::CLFPlugin* plugin) {
    delete plugin;
}
