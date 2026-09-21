// CLFTestPlugin.cpp — 测试插件主 stub（2.1 §4.2；也是 2.2a 插件开发样板）
// 提供 tool.provider 服务（1 个假工具 test_echo，直接回显 args）；
// shutdown 调 host->apiVersion()（P17 析构顺序检测锚点）。

#include "TestPluginCommon.hpp"

extern "C" CLF_PLUGIN_EXPORT
CLF::CLFPluginApi::CLFPlugin* CLFPluginCreate(const CLF::CLFPluginApi::CLFHostApi* host) {
    return new clftest::TestToolProvider(host);
}
extern "C" CLF_PLUGIN_EXPORT
void CLFPluginDestroy(CLF::CLFPluginApi::CLFPlugin* plugin) {
    delete plugin;
}
