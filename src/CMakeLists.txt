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
    CLFCore/CLFLogger.cpp
    CLFCore/CLFMessageCodec.cpp
    CLFCore/CLFPasserby.cpp
    CLFCore/CLFProtocolAdapter.cpp
    CLFCore/CLFRetryPolicy.cpp
    CLFCore/CLFSecurityPolicy.cpp
    CLFCore/CLFSessionManager.cpp
    CLFCore/CLFSessionSummarizer.cpp
    CLFCore/CLFSkillLoader.cpp
    CLFCore/CLFSystemPromptBuilder.cpp
    CLFCore/CLFToolExecutor.cpp
    CLFTools/CLFDiff.cpp          # 编入 clf_core 避免循环依赖
    CLFTools/CLFFileOps.cpp       # 编入 clf_core（被 CLFToolExecutor 直接调用）
)
target_link_libraries(clf_core PUBLIC clf_types clf_network)
target_include_directories(clf_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# CLFUI — 终端 UI (依赖 CLFCore + CLFNetwork)
# ============================================================================
add_library(clf_ui STATIC
    CLFUI/CLFAnsi.cpp
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
target_link_libraries(clf_tools PUBLIC clf_core)
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
                     qa_CLFTipsBar)

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
target_link_libraries(qa_CLFFileOps PRIVATE clf_core)
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

foreach(target IN LISTS CLF_TEST_TARGETS)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endforeach()
