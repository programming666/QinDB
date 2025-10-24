#include "test_framework.h"
#include "test_bplustree.cpp"
#include "test_buffer_pool.cpp"
#include "test_lexer.cpp"
#include <QCoreApplication>

using namespace qindb::test;

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // 创建测试套件
    TestSuite bptreeSuite("B+ Tree Tests");
    TestSuite bufferPoolSuite("Buffer Pool Tests");
    TestSuite lexerSuite("Lexer Tests");

    // 添加B+树测试
    bptreeSuite.addTest(new BPlusTreeTest());

    // 添加缓冲池测试
    bufferPoolSuite.addTest(new BufferPoolTest());

    // 添加词法分析器测试
    lexerSuite.addTest(new LexerTest());

    // 注册测试套件
    TestRunner::instance().registerSuite(&bptreeSuite);
    TestRunner::instance().registerSuite(&bufferPoolSuite);
    TestRunner::instance().registerSuite(&lexerSuite);

    // 运行所有测试
    int result = TestRunner::instance().runAll();

    return result;
}
