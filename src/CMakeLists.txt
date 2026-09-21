# ============================================================================
# clf_plugin_api — 插件 ABI 接口头（阶段 2 §3.5：唯一跨 DLL 共享头，INTERFACE
# target 统一提供；禁依赖 CLFPluginApi/ 之外任何项目头）
# ============================================================================
add_library(clf_plugin_api INTERFACE)
# include 根与既有 target 惯例一致（clf_types/clf_core 等均为 ${CMAKE_CURRENT_SOURCE_DIR} = src）
target_include_directories(clf_plugin_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(clf_plugin_api INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/CLFPluginApi/CLFPluginApi.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CLFPluginApi/CLFToolApi.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CLFPluginApi/CLFFileService.hpp
)

# ============================================================================
# CLFTypes — 基础类型 (header-only + EventQueue, 无项目依赖)
# ============================================================================
add_library(clf_types STATIC
    CLFTypes/CLFEncoding.cpp
    CLFTypes/CLFPeriodicTimer.cpp
    CLFTypes/CLFTextUtil.cpp
)
target_include_directories(clf_types PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# CLFCapabilities — 能力层雏形（C1：FileOps/Diff 归属修正，依赖 clf_types 单向）
# 阶段 2 试点时 FileOps 域迁 DLL，此目标即迁移单元
# ============================================================================
add_library(clf_capabilities STATIC
    CLFCapabilities/FileOps/CLFFileOps.cpp
    CLFCapabilities/FileOps/CLFDiff.cpp
    CLFCapabilities/FileOps/CLFFileServiceImpl.cpp
)
target_link_libraries(clf_capabilities PUBLIC clf_types)
target_include_directories(clf_capabilities PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# CLFNetwork — HTTP 传输 (依赖 CLFTypes)
# ============================================================================
add_library(clf_network STATIC
    CLFNetwork/CLFHttpClient.cpp
    CLFNetwork/CLFThinkingIndicator.cpp
)
target_link_libraries(clf_network PUBLIC clf_types)
target_include_directories(clf_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(clf_network PRIVATE -Wno-error=deprecated-declarations -Wno-error=conversion-null)
endif()

# ============================================================================
# CLFCore — Agent 核心逻辑 (依赖 CLFTypes + CLFNetwork)
# ============================================================================
add_library(clf_core STATIC
    CLFCore/CLFAgentLoop.cpp
    CLFCore/CLFArgParser.cpp
    CLFCore/CLFConfigLoader.cpp
    CLFCore/CLFContext.cpp
    CLFCore/CLFContextWindow.cpp
    CLFCore/CLFDangerousCommandDetector.cpp
    CLFCore/CLFLogger.cpp
    CLFCore/CLFMessageCodec.cpp
    CLFCore/CLFHostApiImpl.cpp
    CLFCore/CLFPasserby.cpp
    CLFCore/CLFPluginManager.cpp
    CLFCore/CLFProjectRulesLoader.cpp
    CLFCore/CLFProtocolAdapter.cpp
    CLFCore/CLFRetryPolicy.cpp
    CLFCore/CLFSecurityPolicy.cpp
    CLFCore/CLFSessionFileCtx.cpp
    CLFCore/CLFSessionManager.cpp
    CLFCore/CLFSessionSummarizer.cpp
    CLFCore/CLFSkillLoader.cpp
    CLFCore/CLFSubprocessRunner.cpp
    CLFCore/CLFSummaryCache.cpp
    CLFCore/CLFSystemInfoProvider.cpp
    CLFCore/CLFSystemPromptBuilder.cpp
    CLFCore/CLFTodoStore.cpp
    CLFCore/CLFToolExecutor.cpp
)
target_link_libraries(clf_core PUBLIC clf_types clf_network clf_capabilities clf_plugin_api)
target_include_directories(clf_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# CLFUI — 终端 UI (依赖 CLFCore + CLFNetwork)
# ============================================================================
add_library(clf_ui STATIC
    CLFUI/CLFAnsi.cpp
    CLFUI/CLFAnsiParser.cpp
    CLFUI/CLFAsyncSubmit.cpp
    CLFUI/CLFClipboard.cpp
    CLFUI/CLFCommandDispatcher.cpp
    CLFUI/CLFCommands.cpp
    CLFUI/CLFConfirmBar.cpp
    CLFUI/CLFInputHandler.cpp
    CLFUI/CLFPasteCoalescer.cpp
    CLFUI/CLFRepl.cpp
    CLFUI/CLFReplView.cpp
    CLFUI/CLFSelectionModel.cpp
    CLFUI/CLFScrollView.cpp
    CLFUI/CLFTerminal.cpp
    CLFUI/CLFTipsBar.cpp
)
target_link_libraries(clf_ui PUBLIC clf_core ftxui::screen ftxui::dom ftxui::component)
target_include_directories(clf_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# CLFTools — 工具实现 (依赖 CLFCore)
# ============================================================================
add_library(clf_tools STATIC
    CLFTools/CLFBuiltinTools.cpp
    CLFTools/CLFCommandExec.cpp
    CLFTools/CLFSearchContent.cpp
    CLFTools/CLFWebFetch.cpp
)
target_link_libraries(clf_tools PUBLIC clf_core clf_capabilities)
target_include_directories(clf_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# 主程序
# ============================================================================
add_executable(CLFCode main.cpp)
target_link_libraries(CLFCode PRIVATE clf_core clf_network clf_tools clf_ui ${CLF_OPENSSL_LIBS})
if(WIN32)
    target_link_libraries(CLFCode PRIVATE ws2_32 crypt32)
endif()

# ============================================================================
# 插件 target 模板（阶段 2 §3.5；2.1 建立，2.2a 首个生产使用）
# 约定：一个 DLL = 一个能力域；只导出工厂 C 符号；与主程序同一编译链与运行时
# ============================================================================
function(clf_add_plugin TARGET_NAME OUTPUT_BASENAME)
    # 用法：clf_add_plugin(clf_plugin_fileops tools.fileops SOURCES ... LIBS ...)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_library(${TARGET_NAME} SHARED ${ARG_SOURCES})
    target_link_libraries(${TARGET_NAME} PRIVATE clf_plugin_api ${ARG_LIBS})
    set_target_properties(${TARGET_NAME} PROPERTIES
        PREFIX ""                                              # 无 lib 前缀
        OUTPUT_NAME "${OUTPUT_BASENAME}"                       # tools.fileops.dll
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"                # /MD 硬约束（§3.5）
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/plugins"   # .dll
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib/$<CONFIG>/plugins"   # 导入库
    )
endfunction()

# ============================================================================
# 测试（迁至 test/CMakeLists.txt 统一管理；Boost.UT，L1 单元 + L2 Mock 集成）
# 注意：必须位于全部库 target 定义之后（target_link_libraries 引用未定义
# target 会被当作 -l<name> 字符串传递给链接器）
# ============================================================================
if(CLF_BUILD_TESTS)
    add_subdirectory(test)
endif()

