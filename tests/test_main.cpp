#include "test_framework.h"

// Define this to prevent main() from being compiled in included test files
#define QINDB_TEST_MAIN_INCLUDED

#include "test_bplustree.cpp"
#include "test_buffer_pool.cpp"
#include "test_lexer.cpp"
#include "test_parser.cpp"
#include "test_hash_index.cpp"
#include "test_catalog.cpp"
#include "test_transaction.cpp"
#include "test_auth_permission.cpp"
#include "test_executor.cpp"
#include "test_query_cache.cpp"
#include "test_result_exporter.cpp"
#include <QCoreApplication>

using namespace qindb::test;

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // 创建测试套件
    TestSuite bptreeSuite("B+ Tree Tests");
    TestSuite bufferPoolSuite("Buffer Pool Tests");
    TestSuite lexerSuite("Lexer Tests");
    TestSuite parserSuite("Parser Tests");
    TestSuite hashIndexSuite("Hash Index Tests");
    TestSuite catalogSuite("Catalog Tests");
    TestSuite transactionSuite("Transaction Tests");
    TestSuite authPermissionSuite("Auth & Permission Tests");
    TestSuite executorSuite("Executor Tests");
    TestSuite queryCacheSuite("Query Cache Tests");
    TestSuite resultExporterSuite("Result Exporter Tests");

    // 添加B+树测试
    bptreeSuite.addTest(new BPlusTreeTest());

    // 添加缓冲池测试
    bufferPoolSuite.addTest(new BufferPoolTest());

    // 添加词法分析器测试
    lexerSuite.addTest(new LexerTest());

    // 添加语法分析器测试
    parserSuite.addTest(new ParserTests());

    // 添加哈希索引测试
    hashIndexSuite.addTest(new HashIndexTests());

    // 添加目录测试
    catalogSuite.addTest(new CatalogTests());

    // 添加事务测试
    transactionSuite.addTest(new TransactionTests());

    // 添加认证和权限测试
    authPermissionSuite.addTest(new AuthPermissionTests());

    // 添加执行器测试
    executorSuite.addTest(new ExecutorTests());

    // 添加查询缓存测试
    queryCacheSuite.addTest(new QueryCacheTests());

    // 添加结果导出测试
    resultExporterSuite.addTest(new ResultExporterTests());

    // 注册测试套件
    TestRunner::instance().registerSuite(&bptreeSuite);
    TestRunner::instance().registerSuite(&bufferPoolSuite);
    TestRunner::instance().registerSuite(&lexerSuite);
    TestRunner::instance().registerSuite(&parserSuite);
    TestRunner::instance().registerSuite(&hashIndexSuite);
    TestRunner::instance().registerSuite(&catalogSuite);
    TestRunner::instance().registerSuite(&transactionSuite);
    TestRunner::instance().registerSuite(&authPermissionSuite);
    TestRunner::instance().registerSuite(&executorSuite);
    TestRunner::instance().registerSuite(&queryCacheSuite);
    TestRunner::instance().registerSuite(&resultExporterSuite);

    // 运行所有测试
    int result = TestRunner::instance().runAll();

    return result;
}
