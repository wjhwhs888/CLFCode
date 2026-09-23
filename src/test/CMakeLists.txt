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
    # 共享测试设施头（CLFTestTempDir.hpp 等，2026-09-22 引入）
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src")
endfunction()

# —— 仅 clf_types ——
clf_add_test(qa_CLFPeriodicTimer    clf_types)
clf_add_test(qa_CLFPlatform         clf_types)
clf_add_test(qa_CLFProcessRunner    clf_types)
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
clf_add_test(qa_CLFCommandDispatcher clf_core clf_network clf_ui ${CLF_OPENSSL_LIBS})

# —— clf_tools + OpenSSL（Windows 另需 ws2_32/crypt32）——
if(WIN32)
    clf_add_test(qa_CLFWebFetch     clf_tools ${CLF_OPENSSL_LIBS} ws2_32 crypt32)
    clf_add_test(qa_CLFBuiltinTools clf_tools clf_core clf_network ${CLF_OPENSSL_LIBS} ws2_32 crypt32)
else()
    clf_add_test(qa_CLFWebFetch     clf_tools ${CLF_OPENSSL_LIBS})
    clf_add_test(qa_CLFBuiltinTools clf_tools clf_core clf_network ${CLF_OPENSSL_LIBS})
endif()

# ============================================================================
# 测试插件（2.1 §4.2：测试插件 = 插件开发样板，2.2a 照此抄骨架；
# 注册内聚于 plugins/CMakeLists.txt——2.3 收尾重构同生产插件模式）
# ============================================================================
add_subdirectory(plugins)

clf_add_test(qa_CLFPluginManager clf_core)
# 插件目录绝对路径注入（多配置生成器下 $<CONFIG> 展开为实际配置）
target_compile_definitions(qa_CLFPluginManager PRIVATE
    CLF_TEST_PLUGIN_DIR="${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/plugins_test")
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
clf_add_test(qa_CLFPluginFileOps clf_core clf_tools ${CLF_OPENSSL_LIBS})
target_compile_definitions(qa_CLFPluginFileOps PRIVATE
    # 生产插件目录（tools.fileops.dll）——与测试插件目录（plugins_test）隔离
    CLF_FILEOPS_PLUGIN_DIR="${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/plugins")
add_dependencies(qa_CLFPluginFileOps clf_plugin_fileops)

# —— 2.3 三域插件全链路（command/search/web）——
clf_add_test(qa_CLFPluginDomains clf_core ${CLF_OPENSSL_LIBS})
target_compile_definitions(qa_CLFPluginDomains PRIVATE
    CLF_PROD_PLUGIN_DIR="${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/plugins")
add_dependencies(qa_CLFPluginDomains
    clf_plugin_command clf_plugin_search clf_plugin_web)
