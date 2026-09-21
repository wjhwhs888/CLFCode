# ============================================================================
# 测试插件注册（2.1 §4.2：测试插件 = 插件开发样板；输出 plugins_test——
# 与生产插件目录隔离，2026-09-21 实抓：混入生产目录致主程序加载 teststub
# 工具路由错乱）
# ============================================================================

# 测试插件注册封装：clf_add_plugin + POST_BUILD 清理生产目录历史残留——
# 输出目录变更的旧产物不会被 CMake 自动删除（2026-09-21 Release 实抓：
# 重构前旧规则的 teststub 残留 bin/Release/plugins/ 致主程序误加载 10 个插件）
function(clf_add_test_plugin TARGET_NAME OUTPUT_BASENAME)
    clf_add_plugin(${TARGET_NAME} ${OUTPUT_BASENAME}
        PLUGIN_DIR plugins_test ${ARGN})
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove -f
            "${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/plugins/${OUTPUT_BASENAME}.dll"
        COMMAND ${CMAKE_COMMAND} -E remove -f
            "${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/plugins/${OUTPUT_BASENAME}.pdb"
        COMMAND ${CMAKE_COMMAND} -E remove -f
            "${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/plugins/${OUTPUT_BASENAME}.ilk")
endfunction()

# 主 stub（P2-P7/P12/P17/P19 等）+ 10 变体（P8-P11/P13-P16/P18）
clf_add_test_plugin(clf_plugin_teststub         clf.teststub         SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPlugin.cpp)
clf_add_test_plugin(clf_plugin_teststub_badinit clf.teststub.badinit SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginBadInit.cpp)
clf_add_test_plugin(clf_plugin_teststub_badabi  clf.teststub.badabi  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginBadAbi.cpp)
clf_add_test_plugin(clf_plugin_teststub_multisvc clf.teststub.multisvc SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginMultiSvc.cpp)
clf_add_test_plugin(clf_plugin_teststub_loopA   clf.teststub.loopA   SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginLoopA.cpp)
clf_add_test_plugin(clf_plugin_teststub_loopB   clf.teststub.loopB   SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginLoopB.cpp)
clf_add_test_plugin(clf_plugin_teststub_svc1    clf.teststub.svc1    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginSvc1.cpp)
clf_add_test_plugin(clf_plugin_teststub_svc2    clf.teststub.svc2    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginSvc2.cpp)
clf_add_test_plugin(clf_plugin_teststub_depA    clf.teststub.depA    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginDepA.cpp)
clf_add_test_plugin(clf_plugin_teststub_depB    clf.teststub.depB    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginDepB.cpp)
clf_add_test_plugin(clf_plugin_teststub_nosym   clf.teststub.nosym   SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLFTestPluginNoSym.cpp)
