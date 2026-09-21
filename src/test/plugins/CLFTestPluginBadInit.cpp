// CLFTestPluginBadInit.cpp — init() 返回 false 的变体（P9 init 失败隔离用）

#include "TestPluginCommon.hpp"

namespace {
class BadInitPlugin : public clftest::TestToolProvider {
public:
    using TestToolProvider::TestToolProvider;
    const char* name() const override { return "clf.teststub.badinit"; }
    bool init() override { return false; }
};
} // namespace

extern "C" CLF_PLUGIN_EXPORT
CLF::CLFPluginApi::CLFPlugin* CLFPluginCreate(const CLF::CLFPluginApi::CLFHostApi* host) {
    return new BadInitPlugin(host);
}
extern "C" CLF_PLUGIN_EXPORT
void CLFPluginDestroy(CLF::CLFPluginApi::CLFPlugin* plugin) {
    delete plugin;
}
