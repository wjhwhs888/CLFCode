# ============================================================================
# 测试插件注册（2.1 §4.2：测试插件 = 插件开发样板；输出 plugins_test——
# 与生产插件目录隔离，2026-09-21 实抓：混入生产目录致主程序加载 teststub
# 工具路由错乱）
# ============================================================================
# 主 stub（P2-P7/P12/P17/P19 等）+ 10 变体（P8-P11/P13-P16/P18）
clf_add_plugin(clf_plugin_teststub         clf.teststub         PLUGIN_DIR plugins_test SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPlugin.cpp)
clf_add_plugin(clf_plugin_teststub_badinit clf.teststub.badinit PLUGIN_DIR plugins_test SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginBadInit.cpp)
clf_add_plugin(clf_plugin_teststub_badabi  clf.teststub.badabi  PLUGIN_DIR plugins_test SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginBadAbi.cpp)
clf_add_plugin(clf_plugin_teststub_multisvc clf.teststub.multisvc PLUGIN_DIR plugins_test SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginMultiSvc.cpp)
clf_add_plugin(clf_plugin_teststub_loopA   clf.teststub.loopA   PLUGIN_DIR plugins_test SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginLoopA.cpp)
clf_add_plugin(clf_plugin_teststub_loopB   clf.teststub.loopB   PLUGIN_DIR plugins_test SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginLoopB.cpp)
clf_add_plugin(clf_plugin_teststub_svc1    clf.teststub.svc1    PLUGIN_DIR plugins_test SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginSvc1.cpp)
clf_add_plugin(clf_plugin_teststub_svc2    clf.teststub.svc2    PLUGIN_DIR plugins_test SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginSvc2.cpp)
clf_add_plugin(clf_plugin_teststub_depA    clf.teststub.depA    PLUGIN_DIR plugins_test SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginDepA.cpp)
clf_add_plugin(clf_plugin_teststub_depB    clf.teststub.depB    PLUGIN_DIR plugins_test SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginDepB.cpp)
clf_add_plugin(clf_plugin_teststub_nosym   clf.teststub.nosym   PLUGIN_DIR plugins_test SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginNoSym.cpp)
