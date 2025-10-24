# qinDB 测试套件

cmake_minimum_required(VERSION 3.16)

# 测试可执行文件
add_executable(qindb_tests
    test_framework.cpp
    test_main.cpp
    test_bplustree.cpp
    test_buffer_pool.cpp
    test_lexer.cpp
)

# 包含目录
target_include_directories(qindb_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

# 链接库
target_link_libraries(qindb_tests PRIVATE
    Qt6::Core
)

# 链接主项目的对象文件
target_sources(qindb_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/storage/buffer_pool_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/disk_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/page.cpp
    ${CMAKE_SOURCE_DIR}/src/index/generic_bplustree.cpp
    ${CMAKE_SOURCE_DIR}/src/index/key_comparator.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/type_serializer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
    ${CMAKE_SOURCE_DIR}/src/parser/lexer.cpp
)

# 添加测试
enable_testing()
add_test(NAME qindb_unit_tests COMMAND qindb_tests)

# 调试测试可执行文件（用于调试B+树问题）
add_executable(test_debug_bptree
    test_debug_bptree.cpp
)

target_include_directories(test_debug_bptree PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_debug_bptree PRIVATE
    Qt6::Core
)

target_sources(test_debug_bptree PRIVATE
    ${CMAKE_SOURCE_DIR}/src/storage/buffer_pool_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/disk_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/page.cpp
    ${CMAKE_SOURCE_DIR}/src/index/generic_bplustree.cpp
    ${CMAKE_SOURCE_DIR}/src/index/key_comparator.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/type_serializer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)

# 性能测试可执行文件
add_executable(qindb_benchmarks
    benchmark_main.cpp
    benchmark_bplustree.cpp
    benchmark_buffer_pool.cpp
)

target_include_directories(qindb_benchmarks PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(qindb_benchmarks PRIVATE
    Qt6::Core
)

target_sources(qindb_benchmarks PRIVATE
    ${CMAKE_SOURCE_DIR}/src/storage/buffer_pool_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/disk_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/page.cpp
    ${CMAKE_SOURCE_DIR}/src/index/generic_bplustree.cpp
    ${CMAKE_SOURCE_DIR}/src/index/key_comparator.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/type_serializer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)
