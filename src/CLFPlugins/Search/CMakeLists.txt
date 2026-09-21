# tools.search（2.3）：SearchContent 能力 + search_content 随域打包（零 core 依赖）。
clf_add_plugin(clf_plugin_search tools.search
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/CLFTools/CLFSearchContent.cpp
        ${CMAKE_SOURCE_DIR}/src/CLFTools/CLFSearchToolHandler.cpp
        ${CMAKE_SOURCE_DIR}/src/CLFCapabilities/CLFHandlerScaffold.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/CLFSearchPlugin.cpp
    LIBS clf_types)
