cmake_minimum_required(VERSION 3.20)
project(CLFCode VERSION 1.0.0 LANGUAGES CXX)

# 测试支持（顶层启用，src/ 下 add_test 注册）
enable_testing()

# FTXUI — C++ 声明式终端 UI 库 (MIT, 零外部依赖, v6.1.9)
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
endif()

# 第三方头文件路径
include_directories(${CMAKE_SOURCE_DIR}/3rdparty/httplib)
include_directories(${CMAKE_SOURCE_DIR}/3rdparty)
include_directories(${CMAKE_SOURCE_DIR}/3rdparty/boost-ut)

# OpenSSL 检测（HTTPS 支持）
find_package(OpenSSL QUIET)
if(OPENSSL_FOUND)
    add_definitions(-DCPPHTTPLIB_OPENSSL_SUPPORT)
    include_directories(${OPENSSL_INCLUDE_DIR})
    set(CLF_OPENSSL_LIBS OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "OpenSSL found — HTTPS enabled")
else()
    set(CLF_OPENSSL_LIBS "")
    message(WARNING "OpenSSL not found — HTTPS disabled. "
                    "Install OpenSSL to call HTTPS APIs (e.g. DeepSeek).\n"
                    "  Windows: vcpkg install openssl\n"
                    "  or download from https://slproweb.com/products/Win32OpenSSL.html")
endif()

# 输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/$<CONFIG>)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/$<CONFIG>)

add_subdirectory(src)
