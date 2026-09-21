# tools.command（2.3）：CommandExec 能力 + execute_command 随域打包。
# cwd 边界校验经 host->config 取根（2.2b 校验归属模式）。
clf_add_plugin(clf_plugin_command tools.command
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/CLFTools/CLFCommandExec.cpp
        ${CMAKE_SOURCE_DIR}/src/CLFTools/CLFCommandToolHandler.cpp
        ${CMAKE_SOURCE_DIR}/src/CLFCapabilities/CLFHandlerScaffold.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/CLFCommandPlugin.cpp
    LIBS clf_types)
