add_library(clf_core STATIC
    CLFCore/CLFAgentLoop.cpp
    CLFCore/CLFConfigLoader.cpp
    CLFCore/CLFContext.cpp
    CLFCore/CLFProtocolAdapter.cpp
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

# 主程序
add_executable(CLFCode main.cpp)
target_link_libraries(CLFCode PRIVATE clf_core clf_network clf_tools ${CLF_OPENSSL_LIBS})

# 头文件搜索路径
target_include_directories(clf_core    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(clf_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(clf_tools   PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
