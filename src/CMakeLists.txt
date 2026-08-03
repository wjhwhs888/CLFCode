add_library(clf_core STATIC
    CLFCore/CLFAgentLoop.cpp
    CLFCore/CLFConfigLoader.cpp
    CLFCore/CLFConsole.cpp
    CLFCore/CLFContext.cpp
    CLFCore/CLFLogger.cpp
    CLFCore/CLFProtocolAdapter.cpp
    CLFCore/CLFRepl.cpp
    CLFCore/CLFRetryPolicy.cpp
    CLFCore/CLFSecurityPolicy.cpp
    CLFCore/CLFSessionManager.cpp
    CLFCore/CLFSkillLoader.cpp
    CLFCore/CLFStreamProcessor.cpp
    CLFCore/CLFTerminal.cpp
    CLFCore/CLFThinkingIndicator.cpp
    CLFCore/CLFToolExecutor.cpp
)

add_library(clf_network STATIC
    CLFNetwork/CLFHttpClient.cpp
)

add_library(clf_tools STATIC
    CLFTools/CLFBuiltinTools.cpp
    CLFTools/CLFFileOps.cpp
    CLFTools/CLFCommandExec.cpp
)
target_link_libraries(clf_tools PUBLIC clf_core)

# clf_core 依赖 clf_network（CLFAgentLoop.cpp 构造 CLFHttpClient）
target_link_libraries(clf_core PUBLIC clf_network)

# 主程序
add_executable(CLFCode main.cpp)
target_link_libraries(CLFCode PRIVATE clf_core clf_network clf_tools ${CLF_OPENSSL_LIBS})

# 头文件搜索路径
target_include_directories(clf_core    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(clf_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(clf_tools   PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# 测试（Boost.UT，L1 单元 + L2 Mock 集成）
# ============================================================================
# 各测试目标：只链接所需模块
# 注：Boost.UT 需要 C++20，测试目标单独设置（主程序保持 C++17）
set(CLF_TEST_TARGETS qa_CLFContext qa_CLFProtocolAdapter qa_CLFSecurityPolicy
                     qa_CLFSessionManager qa_CLFAgentLoop)

add_executable(qa_CLFContext test/qa_CLFContext.cpp)
target_link_libraries(qa_CLFContext PRIVATE clf_core)
add_test(NAME qa_CLFContext COMMAND qa_CLFContext)

add_executable(qa_CLFProtocolAdapter test/qa_CLFProtocolAdapter.cpp)
target_link_libraries(qa_CLFProtocolAdapter PRIVATE clf_core)
add_test(NAME qa_CLFProtocolAdapter COMMAND qa_CLFProtocolAdapter)

add_executable(qa_CLFSecurityPolicy test/qa_CLFSecurityPolicy.cpp)
target_link_libraries(qa_CLFSecurityPolicy PRIVATE clf_core)
add_test(NAME qa_CLFSecurityPolicy COMMAND qa_CLFSecurityPolicy)

add_executable(qa_CLFSessionManager test/qa_CLFSessionManager.cpp)
target_link_libraries(qa_CLFSessionManager PRIVATE clf_core)
add_test(NAME qa_CLFSessionManager COMMAND qa_CLFSessionManager)

add_executable(qa_CLFAgentLoop test/qa_CLFAgentLoop.cpp)
# 需要 OpenSSL：clf_network 启用 HTTPS（CPPHTTPLIB_OPENSSL_SUPPORT）
target_link_libraries(qa_CLFAgentLoop PRIVATE clf_core clf_network ${CLF_OPENSSL_LIBS})
add_test(NAME qa_CLFAgentLoop COMMAND qa_CLFAgentLoop)

foreach(target IN LISTS CLF_TEST_TARGETS)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endforeach()
