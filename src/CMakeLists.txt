# ============================================================================
# CLFTypes — 基础类型 (header-only + EventQueue, 无项目依赖)
# ============================================================================
add_library(clf_types STATIC
    CLFTypes/CLFEncoding.cpp
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

# ============================================================================
# CLFCore — Agent 核心逻辑 (依赖 CLFTypes + CLFNetwork)
# ============================================================================
add_library(clf_core STATIC
    CLFCore/CLFAgentLoop.cpp
    CLFCore/CLFConfigLoader.cpp
    CLFCore/CLFContext.cpp
    CLFCore/CLFLogger.cpp
    CLFCore/CLFMessageCodec.cpp
    CLFCore/CLFProtocolAdapter.cpp
    CLFCore/CLFRetryPolicy.cpp
    CLFCore/CLFSecurityPolicy.cpp
    CLFCore/CLFSessionManager.cpp
    CLFCore/CLFSkillLoader.cpp
    CLFCore/CLFToolExecutor.cpp
)
target_link_libraries(clf_core PUBLIC clf_types clf_network)
target_include_directories(clf_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# CLFUI — 终端 UI (依赖 CLFCore + CLFNetwork)
# ============================================================================
add_library(clf_ui STATIC
    CLFUI/CLFAnsi.cpp
    CLFUI/CLFCommandDispatcher.cpp
    CLFUI/CLFCommands.cpp
    CLFUI/CLFConfirmBar.cpp
    CLFUI/CLFRepl.cpp
    CLFUI/CLFTerminal.cpp
)
target_link_libraries(clf_ui PUBLIC clf_core ftxui::screen ftxui::dom ftxui::component)
target_include_directories(clf_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# CLFTools — 工具实现 (依赖 CLFCore)
# ============================================================================
add_library(clf_tools STATIC
    CLFTools/CLFBuiltinTools.cpp
    CLFTools/CLFFileOps.cpp
    CLFTools/CLFCommandExec.cpp
)
target_link_libraries(clf_tools PUBLIC clf_core)
target_include_directories(clf_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# 主程序
# ============================================================================
add_executable(CLFCode main.cpp)
target_link_libraries(CLFCode PRIVATE clf_core clf_network clf_tools clf_ui ${CLF_OPENSSL_LIBS})

# ============================================================================
# 测试（Boost.UT，L1 单元 + L2 Mock 集成）
# ============================================================================
set(CLF_TEST_TARGETS qa_CLFContext qa_CLFProtocolAdapter qa_CLFSecurityPolicy
                     qa_CLFSessionManager qa_CLFAgentLoop qa_CLFStreamAccumulator)

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

foreach(target IN LISTS CLF_TEST_TARGETS)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endforeach()
