#ifndef QINDB_BENCHMARK_FRAMEWORK_H
#define QINDB_BENCHMARK_FRAMEWORK_H

#include <QString>
#include <QVector>
#include <QDateTime>
#include <iostream>
#include <iomanip>
#include <chrono>

namespace qindb {
namespace benchmark {

/**
 * @brief 性能测试结果
 */
struct BenchmarkResult {
    QString name;                   // 测试名称
    int iterations;                 // 迭代次数
    double totalTimeMs;             // 总时间(毫秒)
    double avgTimeMs;               // 平均时间(毫秒)
    double minTimeMs;               // 最小时间(毫秒)
    double maxTimeMs;               // 最大时间(毫秒)
    double opsPerSecond;            // 每秒操作数
    QString additionalInfo;         // 额外信息

    BenchmarkResult()
        : iterations(0), totalTimeMs(0), avgTimeMs(0),
          minTimeMs(0), maxTimeMs(0), opsPerSecond(0) {}
};

/**
 * @brief 性能测试基类
 */
class Benchmark {
public:
    explicit Benchmark(const QString& name) : name_(name) {}
    virtual ~Benchmark() = default;

    virtual void setup() {}
    virtual void teardown() {}
    virtual void run() = 0;

    QString getName() const { return name_; }
    const QVector<BenchmarkResult>& getResults() const { return results_; }

protected:
    /**
     * @brief 测量单次操作的时间
     */
    template<typename Func>
    double measureOnce(Func&& func) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    /**
     * @brief 运行基准测试
     */
    template<typename Func>
    void runBenchmark(const QString& name, int iterations, Func&& func) {
        BenchmarkResult result;
        result.name = name;
        result.iterations = iterations;
        result.minTimeMs = std::numeric_limits<double>::max();
        result.maxTimeMs = 0;

        QVector<double> times;
        times.reserve(iterations);

        // 预热
        if (iterations > 10) {
            for (int i = 0; i < 3; ++i) {
                func();
            }
        }

        // 正式测试
        for (int i = 0; i < iterations; ++i) {
            double time = measureOnce(func);
            times.push_back(time);
            result.totalTimeMs += time;
            result.minTimeMs = std::min(result.minTimeMs, time);
            result.maxTimeMs = std::max(result.maxTimeMs, time);
        }

        result.avgTimeMs = result.totalTimeMs / iterations;
        result.opsPerSecond = 1000.0 / result.avgTimeMs;

        results_.push_back(result);
    }

    /**
     * @brief 批量操作基准测试
     */
    template<typename Func>
    void runBatchBenchmark(const QString& name, int totalOps, Func&& func) {
        BenchmarkResult result;
        result.name = name;
        result.iterations = totalOps;

        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();

        result.totalTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
        result.avgTimeMs = result.totalTimeMs / totalOps;
        result.opsPerSecond = (totalOps * 1000.0) / result.totalTimeMs;

        results_.push_back(result);
    }

    void addInfo(const QString& info) {
        if (!results_.empty()) {
            results_.last().additionalInfo = info;
        }
    }

private:
    QString name_;
    QVector<BenchmarkResult> results_;
};

/**
 * @brief 性能测试运行器
 */
class BenchmarkRunner {
public:
    static BenchmarkRunner& instance() {
        static BenchmarkRunner runner;
        return runner;
    }

    void registerBenchmark(Benchmark* benchmark) {
        benchmarks_.push_back(benchmark);
    }

    void runAll() {
        std::cout << "\n╔════════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║         qinDB Performance Benchmark Runner         ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════════════════╝\n" << std::endl;

        for (Benchmark* benchmark : benchmarks_) {
            std::cout << "========================================" << std::endl;
            std::cout << "Running Benchmark: " << benchmark->getName().toStdString() << std::endl;
            std::cout << "========================================\n" << std::endl;

            benchmark->setup();
            benchmark->run();
            benchmark->teardown();

            printResults(benchmark);
        }

        printSummary();
    }

private:
    BenchmarkRunner() = default;

    void printResults(Benchmark* benchmark) {
        const auto& results = benchmark->getResults();

        std::cout << "\n----------------------------------------" << std::endl;
        std::cout << "Benchmark Results: " << benchmark->getName().toStdString() << std::endl;
        std::cout << "----------------------------------------" << std::endl;

        for (const auto& result : results) {
            std::cout << "\n[" << result.name.toStdString() << "]" << std::endl;
            std::cout << "  Iterations:    " << result.iterations << std::endl;

            if (result.avgTimeMs > 0) {
                std::cout << std::fixed << std::setprecision(4);
                std::cout << "  Total Time:    " << result.totalTimeMs << " ms" << std::endl;
                std::cout << "  Average Time:  " << result.avgTimeMs << " ms" << std::endl;

                if (result.minTimeMs > 0 && result.maxTimeMs > 0) {
                    std::cout << "  Min Time:      " << result.minTimeMs << " ms" << std::endl;
                    std::cout << "  Max Time:      " << result.maxTimeMs << " ms" << std::endl;
                }

                std::cout << std::fixed << std::setprecision(2);
                std::cout << "  Throughput:    " << result.opsPerSecond << " ops/sec" << std::endl;
            }

            if (!result.additionalInfo.isEmpty()) {
                std::cout << "  Info:          " << result.additionalInfo.toStdString() << std::endl;
            }
        }
        std::cout << std::endl;
    }

    void printSummary() {
        std::cout << "\n╔════════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║               Benchmark Summary                    ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════════════════╝" << std::endl;
        std::cout << "Total Benchmarks Run: " << benchmarks_.size() << std::endl;
        std::cout << "════════════════════════════════════════════════════════\n" << std::endl;
    }

    QVector<Benchmark*> benchmarks_;
};

} // namespace benchmark
} // namespace qindb

#endif // QINDB_BENCHMARK_FRAMEWORK_H
