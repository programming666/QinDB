cmake_minimum_required(VERSION 3.19)
project(qindb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置默认构建类型为Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 设置Release模式优化选项
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ob2 /Oi /Ot")
        # 暂时禁用 LTCG 以避免链接问题
        # set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
    endif()
endif()

# 静态链接Qt和运行时库
set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib)
set(BUILD_SHARED_LIBS OFF)

if(MSVC)
    # 静态链接MSVC运行时
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(Qt6 6 REQUIRED COMPONENTS Core Network)

qt_standard_project_setup()

# 包含头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 收集所有源文件
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
)

# 收集所有头文件
file(GLOB_RECURSE HEADERS
    "include/qindb/*.h"
)

qt_add_executable(qindb
    main.cpp
    ${SOURCES}
    ${HEADERS}
)

target_link_libraries(qindb
    PRIVATE
        Qt::Core
        Qt::Network
)

# 静态链接选项
if(WIN32)
    target_link_options(qindb PRIVATE -static -static-libgcc -static-libstdc++ -Wl,-subsystem,console)
endif()

# 启用警告
if(MSVC)
    target_compile_options(qindb PRIVATE /W4)
else()
    target_compile_options(qindb PRIVATE -Wall -Wextra -pedantic)
endif()

include(GNUInstallDirs)

install(TARGETS qindb
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# 添加测试子目录
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
