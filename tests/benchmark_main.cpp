#include "benchmark_framework.h"
#include "benchmark_bplustree.cpp"
#include "benchmark_buffer_pool.cpp"
#include <QCoreApplication>

using namespace qindb::benchmark;

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    std::cout << "qinDB Performance Benchmark Suite" << std::endl;
    std::wcout << L"==================================\n" << std::endl;

    // 注册性能测试
    BPlusTreeBenchmark bptreeBench;
    BufferPoolBenchmark bufferPoolBench;

    BenchmarkRunner::instance().registerBenchmark(&bptreeBench);
    BenchmarkRunner::instance().registerBenchmark(&bufferPoolBench);

    // 运行所有性能测试
    BenchmarkRunner::instance().runAll();

    return 0;
}
