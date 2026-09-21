# tools.fileops（2.2a 试点）：FileOps/Diff 能力 + file 服务 + 4 工具随域打包。
# 插件不链 clf_core（避免把宿主实现拖进 DLL）——只链 clf_types。
# 同一源文件双编（clf_capabilities 静态 + 本 DLL）合法且是试点最小路径；
# 2.2b 宿主装配切换后静态副本随注册删除而退化。
clf_add_plugin(clf_plugin_fileops tools.fileops
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/CLFCapabilities/FileOps/CLFFileOps.cpp
        ${CMAKE_SOURCE_DIR}/src/CLFCapabilities/FileOps/CLFDiff.cpp
        ${CMAKE_SOURCE_DIR}/src/CLFCapabilities/FileOps/CLFFileServiceImpl.cpp
        ${CMAKE_SOURCE_DIR}/src/CLFCapabilities/CLFHandlerScaffold.cpp
        ${CMAKE_SOURCE_DIR}/src/CLFCapabilities/FileOps/CLFFileOpsHandlers.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/CLFFileOpsPlugin.cpp
    LIBS clf_types)
