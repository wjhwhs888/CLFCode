cmake_minimum_required(VERSION 3.20)
project(CLFCode VERSION 0.1.0 LANGUAGES CXX)

# 测试支持（顶层启用 + 开关；src/CMakeLists.txt 末尾 add_subdirectory(test) 注册）
# 默认 ON 保持 CLion/日常体验不变；发布/纯程序构建可 -DCLF_BUILD_TESTS=OFF
option(CLF_BUILD_TESTS "构建并注册单元测试（qa_*，Boost.UT，C++20）" ON)
if(CLF_BUILD_TESTS)
    enable_testing()
endif()

# FTXUI — C++ 声明式终端 UI 库 (MIT, 零外部依赖, v7.0.0)
# 源码位于 3rdparty/ftxui/, 随项目版本控制
add_subdirectory(${CMAKE_SOURCE_DIR}/3rdparty/ftxui ${CMAKE_BINARY_DIR}/ftxui)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Werror)
    # GCC 15（MinGW）兼容豁免（2026-10-13 收编入版本库——此前经
    # CMAKE_CXX_FLAGS 配置时手动传入，新 build 目录不再需要手动传参）：
    #   deprecated-declarations：fs::u8path(string) 是 C++17 下唯一正确写法
    #     （char8_t 重载 C++20 才有），GCC 15 弃用标记属标准演进误伤
    #   unused-variable / missing-field-initializers / parentheses /
    #   unused-function：全仓既有豁免类别
    add_compile_options(-Wno-error=unused-variable -Wno-error=missing-field-initializers
                        -Wno-error=parentheses -Wno-error=unused-function
                        -Wno-error=deprecated-declarations)
    # Debug 构建带符号（默认 CMAKE_CXX_FLAGS_DEBUG 为空——手动配置时无 -g，
    # 段错误取证需要行号；2026-10-13 插件加载崩溃取证实抓）
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
endif()

# 第三方头文件路径（SYSTEM 语义：第三方库的弃用/风格警告不受本工程
# -Werror 管辖——2026-09-02 系统 OpenSSL 升 4.0.1 后旧 httplib 弃用 API
# 警告曾阻断构建；自身代码的 -Werror 保持不动）
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/3rdparty/httplib)
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/3rdparty)
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/3rdparty/boost-ut)

# OpenSSL 检测（HTTPS 支持）
# 优先使用 CMake find_package，失败时尝试常见安装路径 + 预生成 MinGW 导入库
find_package(OpenSSL QUIET)
if(OPENSSL_FOUND)
    add_definitions(-DCPPHTTPLIB_OPENSSL_SUPPORT)
    include_directories(SYSTEM ${OPENSSL_INCLUDE_DIR})
    set(CLF_OPENSSL_LIBS OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "OpenSSL found (find_package) — HTTPS enabled")
elseif(WIN32)
    # 尝试 Windows 常见安装路径（用户手动安装 + 项目 3rdparty/openssl 导入库）
    set(OSSL_MANUAL_ROOT "C:/Program Files/OpenSSL-Win64")
    set(OSSL_MANUAL_LIB  "${CMAKE_SOURCE_DIR}/3rdparty/openssl/lib")
    if(EXISTS "${OSSL_MANUAL_ROOT}/include/openssl/ssl.h" AND
       EXISTS "${OSSL_MANUAL_LIB}/libssl.a")
        add_definitions(-DCPPHTTPLIB_OPENSSL_SUPPORT)
        include_directories(SYSTEM "${OSSL_MANUAL_ROOT}/include")
        set(CLF_OPENSSL_LIBS "${OSSL_MANUAL_LIB}/libssl.a"
                             "${OSSL_MANUAL_LIB}/libcrypto.a"
                             ws2_32)
        message(STATUS "OpenSSL found (manual ${OSSL_MANUAL_ROOT}) — HTTPS enabled")
    else()
        set(CLF_OPENSSL_LIBS "")
        message(WARNING "OpenSSL not found — HTTPS disabled. "
                        "Install OpenSSL to call HTTPS APIs (e.g. DeepSeek).\n"
                        "  Windows: vcpkg install openssl\n"
                        "  or download from https://slproweb.com/products/Win32OpenSSL.html")
    endif()
else()
    set(CLF_OPENSSL_LIBS "")
    message(WARNING "OpenSSL not found — HTTPS disabled.")
endif()

# 输出目录（2026-10-13 修根：按构建目录隔离——此前多构建目录共享
# 源码 bin/$<CONFIG>，MSVC 与 MinGW 产物互相覆盖混装：MSVC exe 加载 GCC
# 插件 → 跨工具链虚调用 ABI 不兼容 → 启动崩溃（CLFPluginManager::loadDll
# plugin->name() strlen 崩，实机实抓）。隔离后各构建配置产物自洽——正是
# "防止不兼容"的根治。插件扫描路径 = exe 目录/plugins 相对路径自动跟随）
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/$<CONFIG>)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)

# MinGW 运行时 DLL 部署（2026-10-13 GCC 15 实抓修根）：
# Windows DLL 搜索顺序 = exe 目录 → system32 → PATH——本机 system32 有旧版
# libcrypto-4-x64.dll（第三方软件安装），抢先加载致 ENTRYPOINT_NOT_FOUND
# （0xc0000139）。DLL 随产物同目录部署是唯一可靠解（exe/插件/测试插件
# 三个输出目录各一份；copy_if_different 幂等）。
if(MINGW)
    get_filename_component(_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_RUNTIME_DLLS
        "${_MINGW_BIN}/libgcc_s_seh-1.dll"
        "${_MINGW_BIN}/libstdc++-6.dll"
        "${_MINGW_BIN}/libwinpthread-1.dll"
        "C:/Program Files/OpenSSL-Win64/bin/libcrypto-4-x64.dll"
        "C:/Program Files/OpenSSL-Win64/bin/libssl-4-x64.dll")
    add_custom_target(clf_deploy_runtime ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_RUNTIME_DLLS}
                "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_RUNTIME_DLLS}
                "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_RUNTIME_DLLS}
                "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/plugins_test"
        COMMENT "Deploy MinGW/OpenSSL runtime DLLs to output dirs"
        VERBATIM)
endif()

add_subdirectory(src)
