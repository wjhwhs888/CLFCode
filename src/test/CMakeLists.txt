# ============================================================================
# 单元/集成测试注册（Boost.UT；从 src/CMakeLists.txt 迁入，2026-09-11）
# 由顶层 option(CLF_BUILD_TESTS) 控制是否 add_subdirectory 本目录。
# 约定：
#   - 一个测试 = 一个 target = 同名 .cpp（qa_XXX → qa_XXX.cpp）
#   - 产物输出沿用顶层设定：${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/
#   - 测试专用 C++20（Boost.UT 要求）；子目录作用域，不影响 src/ 的 C++17
#   - WORKING_DIRECTORY 显式固定为 ${CMAKE_BINARY_DIR}/src——与迁移前
#     add_test 位于 src/CMakeLists.txt 时的默认值完全一致（消除行为差异）
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 一行注册一个测试：clf_add_test(<target 名> <链接库...>)
function(clf_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src")
endfunction()

# —— 仅 clf_types ——
clf_add_test(qa_CLFPeriodicTimer    clf_types)
clf_add_test(qa_CLFSessionUsage     clf_types)
clf_add_test(qa_CLFTextUtil         clf_types)

# —— clf_capabilities ——
clf_add_test(qa_CLFFileOps          clf_capabilities)

# —— clf_core ——
clf_add_test(qa_CLFToolExecutor     clf_core)
clf_add_test(qa_CLFRetryPolicy      clf_core)
clf_add_test(qa_CLFMessageCodec     clf_core)
clf_add_test(qa_CLFPasserby         clf_core)
clf_add_test(qa_CLFTodoStore        clf_core)
clf_add_test(qa_CLFSessionFileCtx   clf_core)
clf_add_test(qa_CLFSummaryCache     clf_core)
clf_add_test(qa_CLFContextWindow    clf_core)
clf_add_test(qa_CLFSystemComponents clf_core)
clf_add_test(qa_CLFConfigLoader     clf_core)

# —— clf_core + clf_ui ——
clf_add_test(qa_CLFContext          clf_core clf_ui)
clf_add_test(qa_CLFProtocolAdapter  clf_core clf_ui)
clf_add_test(qa_CLFSecurityPolicy   clf_core clf_ui)
clf_add_test(qa_CLFSessionManager   clf_core clf_ui)
clf_add_test(qa_CLFStreamAccumulator clf_core clf_ui)
clf_add_test(qa_CLFHeadTail         clf_core clf_ui)
clf_add_test(qa_CLFPasteCoalescer   clf_core clf_ui)
clf_add_test(qa_CLFSelectionModel   clf_core clf_ui)
clf_add_test(qa_CLFScrollView       clf_core clf_ui)
clf_add_test(qa_CLFInputRender      clf_core clf_ui)
clf_add_test(qa_CLFTipsBar          clf_core clf_ui)

# —— clf_core + clf_capabilities ——
clf_add_test(qa_CLFCapabilities     clf_core clf_capabilities)

# —— clf_ui ——
clf_add_test(qa_CLFAnsiParser       clf_ui)

# —— clf_tools ——
clf_add_test(qa_CLFSearchContent    clf_tools)

# —— clf_core + clf_network + clf_ui + OpenSSL ——
clf_add_test(qa_CLFAgentLoop  clf_core clf_network clf_ui ${CLF_OPENSSL_LIBS})
clf_add_test(qa_CLFTodoPanel  clf_core clf_network clf_ui ${CLF_OPENSSL_LIBS})

# —— clf_tools + OpenSSL（Windows 另需 ws2_32/crypt32）——
if(WIN32)
    clf_add_test(qa_CLFWebFetch     clf_tools ${CLF_OPENSSL_LIBS} ws2_32 crypt32)
    clf_add_test(qa_CLFBuiltinTools clf_tools clf_core clf_network ${CLF_OPENSSL_LIBS} ws2_32 crypt32)
else()
    clf_add_test(qa_CLFWebFetch     clf_tools ${CLF_OPENSSL_LIBS})
    clf_add_test(qa_CLFBuiltinTools clf_tools clf_core clf_network ${CLF_OPENSSL_LIBS})
endif()

# ============================================================================
# 插件管理器测试（2.1 §4.2：测试插件 = 插件开发样板，2.2a 照此抄骨架）
# 测试插件 DLL 输出 bin/$<CONFIG>/plugins/（clf_add_plugin 模板）；qa 经
# CLF_TEST_PLUGIN_DIR 编译定义注入绝对路径（ctest cwd 是构建目录，相对路径不可靠）
# ============================================================================
set(PLUGIN_TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/plugins)

# 主 stub（P2-P7/P12/P17/P19 等）+ 10 变体（P8-P11/P13-P16/P18）
clf_add_plugin(clf_plugin_teststub         clf.teststub         SOURCES ${PLUGIN_TEST_SRC}/CLFTestPlugin.cpp)
clf_add_plugin(clf_plugin_teststub_badinit clf.teststub.badinit SOURCES ${PLUGIN_TEST_SRC}/CLFTestPluginBadInit.cpp)
clf_add_plugin(clf_plugin_teststub_badabi  clf.teststub.badabi  SOURCES ${PLUGIN_TEST_SRC}/CLFTestPluginBadAbi.cpp)
clf_add_plugin(clf_plugin_teststub_multisvc clf.teststub.multisvc SOURCES ${PLUGIN_TEST_SRC}/CLFTestPluginMultiSvc.cpp)
clf_add_plugin(clf_plugin_teststub_loopA   clf.teststub.loopA   SOURCES ${PLUGIN_TEST_SRC}/CLFTestPluginLoopA.cpp)
clf_add_plugin(clf_plugin_teststub_loopB   clf.teststub.loopB   SOURCES ${PLUGIN_TEST_SRC}/CLFTestPluginLoopB.cpp)
clf_add_plugin(clf_plugin_teststub_svc1    clf.teststub.svc1    SOURCES ${PLUGIN_TEST_SRC}/CLFTestPluginSvc1.cpp)
clf_add_plugin(clf_plugin_teststub_svc2    clf.teststub.svc2    SOURCES ${PLUGIN_TEST_SRC}/CLFTestPluginSvc2.cpp)
clf_add_plugin(clf_plugin_teststub_depA    clf.teststub.depA    SOURCES ${PLUGIN_TEST_SRC}/CLFTestPluginDepA.cpp)
clf_add_plugin(clf_plugin_teststub_depB    clf.teststub.depB    SOURCES ${PLUGIN_TEST_SRC}/CLFTestPluginDepB.cpp)
clf_add_plugin(clf_plugin_teststub_nosym   clf.teststub.nosym   SOURCES ${PLUGIN_TEST_SRC}/CLFTestPluginNoSym.cpp)

clf_add_test(qa_CLFPluginManager clf_core)
# 插件目录绝对路径注入（多配置生成器下 $<CONFIG> 展开为实际配置）
target_compile_definitions(qa_CLFPluginManager PRIVATE
    CLF_TEST_PLUGIN_DIR="${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/plugins")
# 测试插件头（plugins/TestPluginCommon.hpp：qa 与插件共享的第二服务接口定义）
target_include_directories(qa_CLFPluginManager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# 运行期依赖：测试插件 DLL 须先构建
add_dependencies(qa_CLFPluginManager
    clf_plugin_teststub
    clf_plugin_teststub_badinit
    clf_plugin_teststub_badabi
    clf_plugin_teststub_multisvc
    clf_plugin_teststub_loopA
    clf_plugin_teststub_loopB
    clf_plugin_teststub_svc1
    clf_plugin_teststub_svc2
    clf_plugin_teststub_depA
    clf_plugin_teststub_depB
    clf_plugin_teststub_nosym
)

# —— FileOps 域插件全链路（2.2a）——
clf_add_test(qa_CLFPluginFileOps clf_core)
target_compile_definitions(qa_CLFPluginFileOps PRIVATE
    CLF_TEST_PLUGIN_DIR="${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/plugins")
add_dependencies(qa_CLFPluginFileOps clf_plugin_fileops)
