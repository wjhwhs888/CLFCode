# tools.web（2.3）：WebFetch 能力 + web_fetch 随域打包。
# 经 httplib 需 OpenSSL（CLF_OPENSSL_LIBS 继承自顶层作用域）；
# 能力内部不携带任何凭据（S2-5 定案）。
clf_add_plugin(clf_plugin_web tools.web
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/CLFTools/CLFWebFetch.cpp
        ${CMAKE_SOURCE_DIR}/src/CLFTools/CLFWebToolHandler.cpp
        ${CMAKE_SOURCE_DIR}/src/CLFCapabilities/CLFHandlerScaffold.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/CLFWebPlugin.cpp
    LIBS clf_types ${CLF_OPENSSL_LIBS})
