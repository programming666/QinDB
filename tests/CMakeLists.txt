# qinDB 测试套件

cmake_minimum_required(VERSION 3.16)

# 启用 Qt 自动 MOC
set(CMAKE_AUTOMOC ON)

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
    Qt6::Network
)


if(WIN32)
    target_link_options(qindb_tests PRIVATE -Wl,-subsystem,console)
endif()

# 链接主项目的对象文件
target_sources(qindb_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/storage/buffer_pool_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/disk_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/page.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/table_page.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/type_serializer.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/row_id_index.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/transaction.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/wal.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/wal_db_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/undo_log.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/vacuum.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/visibility_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/index/generic_bplustree.cpp
    ${CMAKE_SOURCE_DIR}/src/index/key_comparator.cpp
    ${CMAKE_SOURCE_DIR}/src/index/hash_index.cpp
    ${CMAKE_SOURCE_DIR}/src/index/hash_bucket_page.cpp
    ${CMAKE_SOURCE_DIR}/src/index/inverted_index.cpp
    ${CMAKE_SOURCE_DIR}/src/index/tokenizer.cpp
    ${CMAKE_SOURCE_DIR}/src/parser/lexer.cpp
    ${CMAKE_SOURCE_DIR}/src/parser/parser.cpp
    ${CMAKE_SOURCE_DIR}/src/parser/ast.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/catalog_db_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/database_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/auth_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/permission_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/password_hasher.cpp
    ${CMAKE_SOURCE_DIR}/src/executor/executor.cpp
    ${CMAKE_SOURCE_DIR}/src/executor/expression_evaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/optimizer/query_rewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/optimizer/cost_optimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/optimizer/cost_model.cpp
    ${CMAKE_SOURCE_DIR}/src/optimizer/statistics.cpp
    ${CMAKE_SOURCE_DIR}/src/cache/query_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/cache/table_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/result_exporter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
    # 添加连接字符串解析器（用于test_connection_string）
    ${CMAKE_SOURCE_DIR}/src/network/connection_string_parser.cpp
)

# TLS Handshake Manager 测试可执行文件
add_executable(test_tls_handshake
    test_framework.cpp
    test_tls_handshake.cpp
    ${CMAKE_SOURCE_DIR}/include/qindb/tls_handshake_manager.h
)

# 设置AUTOMOC属性
set_target_properties(test_tls_handshake PROPERTIES AUTOMOC ON)

target_include_directories(test_tls_handshake PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_tls_handshake PRIVATE
    Qt6::Core
    Qt6::Network
)

if(WIN32)
    target_link_options(test_tls_handshake PRIVATE -Wl,-subsystem,console)
endif()

target_sources(test_tls_handshake PRIVATE
    ${CMAKE_SOURCE_DIR}/src/network/tls_handshake_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tls_config.cpp
    ${CMAKE_SOURCE_DIR}/src/network/certificate_generator.cpp
    ${CMAKE_SOURCE_DIR}/src/network/sslError_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/network/fingerprint_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)

add_test(NAME test_tls_handshake COMMAND test_tls_handshake)

# TLS Handshake Manager 简化测试可执行文件
add_executable(test_tls_handshake_simple
    test_framework.cpp
    test_tls_handshake_simple.cpp
)

target_include_directories(test_tls_handshake_simple PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_tls_handshake_simple PRIVATE
    Qt6::Core
    Qt6::Network
)

if(WIN32)
    target_link_options(test_tls_handshake_simple PRIVATE -Wl,-subsystem,console)
endif()

target_sources(test_tls_handshake_simple PRIVATE
    ${CMAKE_SOURCE_DIR}/src/network/tls_config.cpp
    ${CMAKE_SOURCE_DIR}/src/network/certificate_generator.cpp
    ${CMAKE_SOURCE_DIR}/src/network/sslError_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)

add_test(NAME test_tls_handshake_simple COMMAND test_tls_handshake_simple)

# SSL Error Handler 测试可执行文件
add_executable(test_ssl_error_handler
    test_framework.cpp
    test_ssl_error_handler.cpp
)

target_include_directories(test_ssl_error_handler PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_ssl_error_handler PRIVATE
    Qt6::Core
    Qt6::Network
)

if(WIN32)
    target_link_options(test_ssl_error_handler PRIVATE -Wl,-subsystem,console)
endif()

target_sources(test_ssl_error_handler PRIVATE
    ${CMAKE_SOURCE_DIR}/src/network/sslError_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/network/certificate_generator.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)

add_test(NAME test_ssl_error_handler COMMAND test_ssl_error_handler)

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


if(WIN32)
    target_link_options(test_debug_bptree PRIVATE -Wl,-subsystem,console)
endif()

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


if(WIN32)
    target_link_options(qindb_benchmarks PRIVATE -Wl,-subsystem,console)
endif()

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

# Connection String Parser 测试可执行文件
add_executable(test_connection_string
    test_framework.cpp
    test_connection_string.cpp
)

target_include_directories(test_connection_string PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_connection_string PRIVATE
    Qt6::Core
    Qt6::Network
)


if(WIN32)
    target_link_options(test_connection_string PRIVATE -Wl,-subsystem,console)
endif()

target_sources(test_connection_string PRIVATE
    ${CMAKE_SOURCE_DIR}/src/network/connection_string_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)

add_test(NAME test_connection_string COMMAND test_connection_string)

# Parser 测试可执行文件
add_executable(test_parser
    test_framework.cpp
    test_parser.cpp
)

target_include_directories(test_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_parser PRIVATE
    Qt6::Core
)


if(WIN32)
    target_link_options(test_parser PRIVATE -Wl,-subsystem,console)
endif()

target_sources(test_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src/parser/lexer.cpp
    ${CMAKE_SOURCE_DIR}/src/parser/parser.cpp
    ${CMAKE_SOURCE_DIR}/src/parser/ast.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)

add_test(NAME test_parser COMMAND test_parser)

# Result Exporter 测试可执行文件
add_executable(test_result_exporter
    test_framework.cpp
    test_result_exporter.cpp
)

target_include_directories(test_result_exporter PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_result_exporter PRIVATE
    Qt6::Core
)


if(WIN32)
    target_link_options(test_result_exporter PRIVATE -Wl,-subsystem,console)
endif()

target_sources(test_result_exporter PRIVATE
    ${CMAKE_SOURCE_DIR}/src/utils/result_exporter.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)

add_test(NAME test_result_exporter COMMAND test_result_exporter)

# Query Cache 测试可执行文件
add_executable(test_query_cache
    test_framework.cpp
    test_query_cache.cpp
)

target_include_directories(test_query_cache PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_query_cache PRIVATE
    Qt6::Core
)


if(WIN32)
    target_link_options(test_query_cache PRIVATE -Wl,-subsystem,console)
endif()

target_sources(test_query_cache PRIVATE
    ${CMAKE_SOURCE_DIR}/src/cache/query_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)

add_test(NAME test_query_cache COMMAND test_query_cache)

# Hash Index 测试可执行文件
add_executable(test_hash_index
    test_framework.cpp
    test_hash_index.cpp
)

target_include_directories(test_hash_index PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_hash_index PRIVATE
    Qt6::Core
)

if(WIN32)
    target_link_options(test_hash_index PRIVATE -static -static-libgcc -static-libstdc++)
endif()

target_sources(test_hash_index PRIVATE
    ${CMAKE_SOURCE_DIR}/src/index/hash_index.cpp
    ${CMAKE_SOURCE_DIR}/src/index/hash_bucket_page.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/buffer_pool_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/disk_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/page.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/type_serializer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)

add_test(NAME test_hash_index COMMAND test_hash_index)

# Catalog 测试可执行文件
add_executable(test_catalog
    test_framework.cpp
    test_catalog.cpp
)

target_include_directories(test_catalog PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_catalog PRIVATE
    Qt6::Core
)


if(WIN32)
    target_link_options(test_catalog PRIVATE -Wl,-subsystem,console)
endif()

target_sources(test_catalog PRIVATE
    ${CMAKE_SOURCE_DIR}/src/catalog/catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/catalog_db_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/buffer_pool_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/disk_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/page.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/table_page.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/type_serializer.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/row_id_index.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)

add_test(NAME test_catalog COMMAND test_catalog)

# Transaction 测试可执行文件
add_executable(test_transaction
    test_framework.cpp
    test_transaction.cpp
)

target_include_directories(test_transaction PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_transaction PRIVATE
    Qt6::Core
)


if(WIN32)
    target_link_options(test_transaction PRIVATE -Wl,-subsystem,console)
endif()

target_sources(test_transaction PRIVATE
    ${CMAKE_SOURCE_DIR}/src/storage/transaction.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/wal.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/wal_db_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/undo_log.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/buffer_pool_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/disk_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/page.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/table_page.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/type_serializer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/catalog_db_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/row_id_index.cpp
)

add_test(NAME test_transaction COMMAND test_transaction)

# Auth & Permission 测试可执行文件
add_executable(test_auth_permission
    test_framework.cpp
    test_auth_permission.cpp
)

target_include_directories(test_auth_permission PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_auth_permission PRIVATE
    Qt6::Core
)


if(WIN32)
    target_link_options(test_auth_permission PRIVATE -Wl,-subsystem,console)
endif()

target_sources(test_auth_permission PRIVATE
    ${CMAKE_SOURCE_DIR}/src/auth/auth_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/permission_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/password_hasher.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/catalog_db_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/buffer_pool_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/disk_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/page.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/table_page.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/type_serializer.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/row_id_index.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)

add_test(NAME test_auth_permission COMMAND test_auth_permission)

# Executor 测试可执行文件
add_executable(test_executor
    test_framework.cpp
    test_executor.cpp
)

target_include_directories(test_executor PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_executor PRIVATE
    Qt6::Core
)


if(WIN32)
    target_link_options(test_executor PRIVATE -Wl,-subsystem,console)
endif()

target_sources(test_executor PRIVATE
    ${CMAKE_SOURCE_DIR}/src/executor/executor.cpp
    ${CMAKE_SOURCE_DIR}/src/executor/expression_evaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/parser/lexer.cpp
    ${CMAKE_SOURCE_DIR}/src/parser/parser.cpp
    ${CMAKE_SOURCE_DIR}/src/parser/ast.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/catalog_db_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/database_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/auth_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/permission_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/password_hasher.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/buffer_pool_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/disk_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/page.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/table_page.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/type_serializer.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/row_id_index.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/vacuum.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/visibility_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/index/generic_bplustree.cpp
    ${CMAKE_SOURCE_DIR}/src/index/key_comparator.cpp
    ${CMAKE_SOURCE_DIR}/src/index/hash_index.cpp
    ${CMAKE_SOURCE_DIR}/src/index/hash_bucket_page.cpp
    ${CMAKE_SOURCE_DIR}/src/index/inverted_index.cpp
    ${CMAKE_SOURCE_DIR}/src/index/tokenizer.cpp
    ${CMAKE_SOURCE_DIR}/src/optimizer/query_rewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/optimizer/cost_optimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/optimizer/cost_model.cpp
    ${CMAKE_SOURCE_DIR}/src/optimizer/statistics.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/result_exporter.cpp
    ${CMAKE_SOURCE_DIR}/src/cache/query_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/cache/table_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/wal.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/wal_db_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/transaction.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/undo_log.cpp
)

add_test(NAME test_executor COMMAND test_executor)

# IP Whitelist CIDR 测试可执行文件
add_executable(test_ip_whitelist
    test_framework.cpp
    test_ip_whitelist.cpp
)

target_include_directories(test_ip_whitelist PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(test_ip_whitelist PRIVATE
    Qt6::Core
    Qt6::Network
)


if(WIN32)
    target_link_options(test_ip_whitelist PRIVATE -Wl,-subsystem,console)
endif()

target_sources(test_ip_whitelist PRIVATE
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)

add_test(NAME test_ip_whitelist COMMAND test_ip_whitelist)

# 添加测试
enable_testing()
add_test(NAME qindb_unit_tests COMMAND qindb_tests)

