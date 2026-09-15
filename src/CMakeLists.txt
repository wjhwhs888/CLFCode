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
    CLFCore/CLFPasserby.cpp
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
target_link_libraries(clf_core PUBLIC clf_types clf_network clf_capabilities)
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
# 测试（迁至 test/CMakeLists.txt 统一管理；Boost.UT，L1 单元 + L2 Mock 集成）
# 注意：必须位于全部库 target 定义之后（target_link_libraries 引用未定义
# target 会被当作 -l<name> 字符串传递给链接器）
# ============================================================================
if(CLF_BUILD_TESTS)
    add_subdirectory(test)
endif()

