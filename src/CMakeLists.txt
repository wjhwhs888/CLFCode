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
# 测试（Boost.UT，L1 单元 + L2 Mock 集成）
# ============================================================================
set(CLF_TEST_TARGETS qa_CLFContext qa_CLFProtocolAdapter qa_CLFSecurityPolicy
                     qa_CLFSessionManager qa_CLFAgentLoop qa_CLFStreamAccumulator
                     qa_CLFHeadTail qa_CLFSearchContent qa_CLFToolExecutor
                     qa_CLFRetryPolicy qa_CLFFileOps qa_CLFBuiltinTools qa_CLFWebFetch
                     qa_CLFMessageCodec qa_CLFPeriodicTimer
                     qa_CLFPasteCoalescer qa_CLFSelectionModel
                     qa_CLFInputRender qa_CLFPasserby qa_CLFTodoPanel
                     qa_CLFTipsBar qa_CLFCapabilities
                     qa_CLFTodoStore qa_CLFSessionFileCtx qa_CLFSummaryCache
                     qa_CLFContextWindow qa_CLFSystemComponents
                     qa_CLFConfigLoader qa_CLFSessionUsage qa_CLFTextUtil
                     qa_CLFAnsiParser)

add_executable(qa_CLFContext test/qa_CLFContext.cpp)
target_link_libraries(qa_CLFContext PRIVATE clf_core clf_ui)
add_test(NAME qa_CLFContext COMMAND qa_CLFContext)

add_executable(qa_CLFProtocolAdapter test/qa_CLFProtocolAdapter.cpp)
target_link_libraries(qa_CLFProtocolAdapter PRIVATE clf_core clf_ui)
add_test(NAME qa_CLFProtocolAdapter COMMAND qa_CLFProtocolAdapter)

add_executable(qa_CLFSecurityPolicy test/qa_CLFSecurityPolicy.cpp)
target_link_libraries(qa_CLFSecurityPolicy PRIVATE clf_core clf_ui)
add_test(NAME qa_CLFSecurityPolicy COMMAND qa_CLFSecurityPolicy)

add_executable(qa_CLFSessionManager test/qa_CLFSessionManager.cpp)
target_link_libraries(qa_CLFSessionManager PRIVATE clf_core clf_ui)
add_test(NAME qa_CLFSessionManager COMMAND qa_CLFSessionManager)

add_executable(qa_CLFAgentLoop test/qa_CLFAgentLoop.cpp)
target_link_libraries(qa_CLFAgentLoop PRIVATE clf_core clf_network clf_ui ${CLF_OPENSSL_LIBS})
add_test(NAME qa_CLFAgentLoop COMMAND qa_CLFAgentLoop)

add_executable(qa_CLFStreamAccumulator test/qa_CLFStreamAccumulator.cpp)
target_link_libraries(qa_CLFStreamAccumulator PRIVATE clf_core clf_ui)
add_test(NAME qa_CLFStreamAccumulator COMMAND qa_CLFStreamAccumulator)

add_executable(qa_CLFHeadTail test/qa_CLFHeadTail.cpp)
target_link_libraries(qa_CLFHeadTail PRIVATE clf_core clf_ui)
add_test(NAME qa_CLFHeadTail COMMAND qa_CLFHeadTail)

add_executable(qa_CLFSearchContent test/qa_CLFSearchContent.cpp)
target_link_libraries(qa_CLFSearchContent PRIVATE clf_tools)
add_test(NAME qa_CLFSearchContent COMMAND qa_CLFSearchContent)

add_executable(qa_CLFToolExecutor test/qa_CLFToolExecutor.cpp)
target_link_libraries(qa_CLFToolExecutor PRIVATE clf_core)
add_test(NAME qa_CLFToolExecutor COMMAND qa_CLFToolExecutor)

add_executable(qa_CLFRetryPolicy test/qa_CLFRetryPolicy.cpp)
target_link_libraries(qa_CLFRetryPolicy PRIVATE clf_core)
add_test(NAME qa_CLFRetryPolicy COMMAND qa_CLFRetryPolicy)

add_executable(qa_CLFFileOps test/qa_CLFFileOps.cpp)
target_link_libraries(qa_CLFFileOps PRIVATE clf_capabilities)
add_test(NAME qa_CLFFileOps COMMAND qa_CLFFileOps)

# clf_tools 经 clf_core 间接拉入 clf_network（httplib+SSL），静态库按需拉取
# 会带出 CLFHttpClient.obj 的 OpenSSL 符号，故须显式链接 OpenSSL
add_executable(qa_CLFBuiltinTools test/qa_CLFBuiltinTools.cpp)
# B4 用例构造 CLFAgentLoop（默认 http 客户端）需 clf_core/clf_network
target_link_libraries(qa_CLFBuiltinTools PRIVATE clf_tools clf_core clf_network ${CLF_OPENSSL_LIBS})
if(WIN32)
    target_link_libraries(qa_CLFBuiltinTools PRIVATE ws2_32 crypt32)
endif()
add_test(NAME qa_CLFBuiltinTools COMMAND qa_CLFBuiltinTools)

add_executable(qa_CLFWebFetch test/qa_CLFWebFetch.cpp)
target_link_libraries(qa_CLFWebFetch PRIVATE clf_tools ${CLF_OPENSSL_LIBS})
if(WIN32)
    target_link_libraries(qa_CLFWebFetch PRIVATE ws2_32 crypt32)
endif()
add_test(NAME qa_CLFWebFetch COMMAND qa_CLFWebFetch)

add_executable(qa_CLFMessageCodec test/qa_CLFMessageCodec.cpp)
target_link_libraries(qa_CLFMessageCodec PRIVATE clf_core)
add_test(NAME qa_CLFMessageCodec COMMAND qa_CLFMessageCodec)

add_executable(qa_CLFPeriodicTimer test/qa_CLFPeriodicTimer.cpp)
target_link_libraries(qa_CLFPeriodicTimer PRIVATE clf_types)
add_test(NAME qa_CLFPeriodicTimer COMMAND qa_CLFPeriodicTimer)

add_executable(qa_CLFPasteCoalescer test/qa_CLFPasteCoalescer.cpp)
target_link_libraries(qa_CLFPasteCoalescer PRIVATE clf_core clf_ui)
add_test(NAME qa_CLFPasteCoalescer COMMAND qa_CLFPasteCoalescer)

add_executable(qa_CLFSelectionModel test/qa_CLFSelectionModel.cpp)
target_link_libraries(qa_CLFSelectionModel PRIVATE clf_core clf_ui)
add_test(NAME qa_CLFSelectionModel COMMAND qa_CLFSelectionModel)

add_executable(qa_CLFInputRender test/qa_CLFInputRender.cpp)
target_link_libraries(qa_CLFInputRender PRIVATE clf_core clf_ui)
add_test(NAME qa_CLFInputRender COMMAND qa_CLFInputRender)

add_executable(qa_CLFPasserby test/qa_CLFPasserby.cpp)
target_link_libraries(qa_CLFPasserby PRIVATE clf_core)
add_test(NAME qa_CLFPasserby COMMAND qa_CLFPasserby)

# 任务面板行构建（设计-任务清单UI显示 §4.2，2026-09-02）
add_executable(qa_CLFTodoPanel test/qa_CLFTodoPanel.cpp)
target_link_libraries(qa_CLFTodoPanel PRIVATE clf_core clf_network clf_ui ${CLF_OPENSSL_LIBS})
add_test(NAME qa_CLFTodoPanel COMMAND qa_CLFTodoPanel)

# Tips 行（设计-工具调用循环上限机制改造 §3.4，2026-09-02）
add_executable(qa_CLFTipsBar test/qa_CLFTipsBar.cpp)
target_link_libraries(qa_CLFTipsBar PRIVATE clf_core clf_ui)
add_test(NAME qa_CLFTipsBar COMMAND qa_CLFTipsBar)

# 能力层接口契约 + executor 经接口全链路（C1，2026-09-07）
add_executable(qa_CLFCapabilities test/qa_CLFCapabilities.cpp)
target_link_libraries(qa_CLFCapabilities PRIVATE clf_core clf_capabilities)
add_test(NAME qa_CLFCapabilities COMMAND qa_CLFCapabilities)

# C2 拆角色三组件（2026-09-07）
add_executable(qa_CLFTodoStore test/qa_CLFTodoStore.cpp)
target_link_libraries(qa_CLFTodoStore PRIVATE clf_core)
add_test(NAME qa_CLFTodoStore COMMAND qa_CLFTodoStore)

add_executable(qa_CLFSessionFileCtx test/qa_CLFSessionFileCtx.cpp)
target_link_libraries(qa_CLFSessionFileCtx PRIVATE clf_core)
add_test(NAME qa_CLFSessionFileCtx COMMAND qa_CLFSessionFileCtx)

add_executable(qa_CLFSummaryCache test/qa_CLFSummaryCache.cpp)
target_link_libraries(qa_CLFSummaryCache PRIVATE clf_core)
add_test(NAME qa_CLFSummaryCache COMMAND qa_CLFSummaryCache)

# 消息窗口截断策略（C2b，2026-09-07）
add_executable(qa_CLFContextWindow test/qa_CLFContextWindow.cpp)
target_link_libraries(qa_CLFContextWindow PRIVATE clf_core)
add_test(NAME qa_CLFContextWindow COMMAND qa_CLFContextWindow)

# C5 拆分组件（RulesLoader/InfoProvider/SubprocessRunner，2026-09-07）
add_executable(qa_CLFSystemComponents test/qa_CLFSystemComponents.cpp)
target_link_libraries(qa_CLFSystemComponents PRIVATE clf_core)
add_test(NAME qa_CLFSystemComponents COMMAND qa_CLFSystemComponents)

# 配置加载器表驱动回归（C6，2026-09-07）
add_executable(qa_CLFConfigLoader test/qa_CLFConfigLoader.cpp)
target_link_libraries(qa_CLFConfigLoader PRIVATE clf_core)
add_test(NAME qa_CLFConfigLoader COMMAND qa_CLFConfigLoader)

# 会话 usage 统计（缓存命中率显示-底部常亮参数行，2026-09-08）
add_executable(qa_CLFSessionUsage test/qa_CLFSessionUsage.cpp)
target_link_libraries(qa_CLFSessionUsage PRIVATE clf_types)
add_test(NAME qa_CLFSessionUsage COMMAND qa_CLFSessionUsage)

# 公共文本工具（ANSI 转义宽度感知，2026-09-08）
add_executable(qa_CLFTextUtil test/qa_CLFTextUtil.cpp)
target_link_libraries(qa_CLFTextUtil PRIVATE clf_types)
add_test(NAME qa_CLFTextUtil COMMAND qa_CLFTextUtil)

# SGR 转义渲染层解析（ANSI 颜色修复 v2，2026-09-08）
add_executable(qa_CLFAnsiParser test/qa_CLFAnsiParser.cpp)
target_link_libraries(qa_CLFAnsiParser PRIVATE clf_ui)
add_test(NAME qa_CLFAnsiParser COMMAND qa_CLFAnsiParser)

foreach(target IN LISTS CLF_TEST_TARGETS)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endforeach()
