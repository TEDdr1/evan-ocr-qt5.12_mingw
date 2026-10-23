cmake_minimum_required(VERSION 3.16)

project(evan VERSION 1.2.1 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# 语言标准：Qt 5.12 LTS 官方支持 C++11/14/17，使用 C++17 可获得结构化绑定等特性，
# 同时避免 C++20 与 Qt 5.12 头文件（QString 比较运算符、QChar 隐式转换等）的兼容问题。
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(EVAN_VERSION="${PROJECT_VERSION}")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Windows SDK 版本：SetProcessDpiAwarenessContext / WinRT 需要 Windows 10 目标版本
if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
        NTDDI_VERSION=0x0A000000
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# ---------------------------------------------------------------------------
# Qt 5 定位：允许通过 -DQt5_DIR 或环境变量 CMAKE_PREFIX_PATH 指定
# 默认回落到本机 C:/Qts/5.12/5.12.10/mingw73_64
# ---------------------------------------------------------------------------
if(WIN32 AND NOT DEFINED Qt5_DIR AND EXISTS "C:/Qts/5.12/5.12.10/mingw73_64/lib/cmake/Qt5")
    set(Qt5_DIR "C:/Qts/5.12/5.12.10/mingw73_64/lib/cmake/Qt5")
endif()

find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Widgets Network)

message(STATUS "Qt5 版本: ${Qt5_VERSION}  (${Qt5Core_DIR})")

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
)

# 基础模块源文件收集
file(GLOB_RECURSE SOURCES
    "src/main.cpp"
    "src/core/*.cpp"
    "src/core/*.h"
    "src/ui/*.cpp"
    "src/ui/*.h"
    "src/platform/*.cpp"
    "src/platform/*.h"
)

# 资源清单（图标 + qrc）：MinGW 下 windres 按 UTF-8 解析，避免版本信息中的中文乱码
set(APP_RESOURCES resources/resources.qrc)

# Windows 平台专用链接库与选项
if(WIN32)
    enable_language(RC)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --codepage=65001")

    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} resources/app.rc ${APP_RESOURCES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/resources)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Network
        user32
        gdi32
        dwmapi
        ole32
        oleaut32
        runtimeobject
        shcore
    )
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${APP_RESOURCES})
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Network
    )
endif()

# MinGW / GCC 编译参数
if(MINGW)
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wno-unused-parameter
        -Wno-deprecated-declarations
    )
    # 静态链接 libstdc++/libgcc，避免目标机缺少 GCC 运行库 DLL
    target_link_options(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++)
endif()

# MSVC 编译参数调优 (UTF-8 字符集与快速编译)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /permissive- /W4)
endif()
