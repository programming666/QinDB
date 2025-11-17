#include "benchmark_framework.h"    // 引入基准测试框架头文件，提供基准测试的基础设施
#include "qindb/generic_bplustree.h"  // 引入B+树通用实现头文件，提供B+树数据结构的实现
#include "qindb/buffer_pool_manager.h"  // 引入缓冲池管理器头文件，管理内存缓冲区
#include "qindb/disk_manager.h"    // 引入磁盘管理器头文件
#include "qindb/config.h"      // 引入配置管理头文件
#include <QTemporaryFile>     // 引入Qt临时文件类，用于创建临时测试文件
#include <random>         // 引入随机数生成库，用于生成随机测试数据

namespace qindb {
namespace benchmark {

/**
 * @brief B+树性能测试类
 * 继承自Benchmark基类，用于测试B+树的各种操作性能
 * 包括插入、查询、删除等基本操作的性能评估
 */
class BPlusTreeBenchmark : public Benchmark {
public:
    BPlusTreeBenchmark() : Benchmark("B+ Tree Performance") {}  // 构造函数，初始化基准测试名称

    /**
     * @brief 测试环境初始化函数
     * 创建临时文件、磁盘管理器、缓冲池管理器和B+树实例
     */
    void setup() override {
        tempFile_ = new QTemporaryFile();  // 创建临时文件对象
        tempFile_->setAutoRemove(true);   // 设置自动删除
        tempFile_->open();                // 打开临时文件
        dbPath_ = tempFile_->fileName();  // 获取文件路径
        tempFile_->close();               // 关闭文件

        diskMgr_ = new DiskManager(dbPath_);  // 创建磁盘管理器
        Config& config = Config::instance();   // 获取配置实例
        bufferPool_ = new BufferPoolManager(config.getBufferPoolSize(), diskMgr_);  // 创建缓冲池管理器
        tree_ = new GenericBPlusTree(bufferPool_, DataType::INT);  // 创建B+树实例
    }

    /**
     * @brief 测试环境清理函数
     * 释放所有分配的资源
     */
    void teardown() override {
        delete tree_;      // 删除B+树
        delete bufferPool_; // 删除缓冲池管理器
        delete diskMgr_;   // 删除磁盘管理器
        delete tempFile_;  // 删除临时文件
    }

    /**
     * @brief 运行所有性能测试函数
     * 依次执行各种操作的性能测试
     */
    void run() override {
        benchmarkSequentialInsert();   // 顺序插入性能测试
        benchmarkRandomInsert();     // 随机插入性能测试
        benchmarkSequentialSearch();
        benchmarkRandomSearch();     // 随机查询性能测试
        benchmarkRangeSearch();      // 范围查询性能测试
        benchmarkMixedOperations();  // 混合操作性能测试
    }

private:
    /**
     * @brief 顺序插入性能测试
     * 测试按顺序插入大量记录的性能
     */
    void benchmarkSequentialInsert() {
        const int COUNT = 50000;

        runBatchBenchmark("Sequential Insert (50K records)", COUNT, [&]() {
            for (int i = 1; i <= COUNT; ++i) {
                tree_->insert(QVariant(i), i);
            }
        });
    }

    /**
     * @brief 随机插入性能测试
     */
    void benchmarkRandomInsert() {
        // 清理之前的数据
        delete tree_;
        tree_ = new GenericBPlusTree(bufferPool_, DataType::INT);

        const int COUNT = 50000;
        std::vector<int> keys;
        keys.reserve(COUNT);
        for (int i = 1; i <= COUNT; ++i) {
            keys.push_back(i);
        }

        // 打乱顺序
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(keys.begin(), keys.end(), g);

        runBatchBenchmark("Random Insert (50K records)", COUNT, [&]() {
            for (int key : keys) {
                tree_->insert(QVariant(key), key);
            }
        });
    }

    /**
     * @brief 顺序查询性能测试
     */
    void benchmarkSequentialSearch() {
        const int COUNT = 10000;

        runBatchBenchmark("Sequential Search (10K queries)", COUNT, [&]() {
            for (int i = 1; i <= COUNT; ++i) {
                RowId value;
                tree_->search(QVariant(i), value);
            }
        });
    }

    /**
     * @brief 随机查询性能测试
     */
    void benchmarkRandomSearch() {
        const int COUNT = 10000;
        std::vector<int> keys;
        keys.reserve(COUNT);
        for (int i = 1; i <= COUNT; ++i) {
            keys.push_back(i);
        }

        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(keys.begin(), keys.end(), g);

        runBatchBenchmark("Random Search (10K queries)", COUNT, [&]() {
            for (int key : keys) {
                RowId value;
                tree_->search(QVariant(key), value);
            }
        });
    }

    /**
     * @brief 范围查询性能测试
     */
    void benchmarkRangeSearch() {
        const int QUERIES = 100;
        QVector<std::pair<QVariant, RowId>> results;

        runBatchBenchmark("Range Search (100 queries, 1000 records each)", QUERIES, [&]() {
            for (int i = 0; i < QUERIES; ++i) {
                int start = i * 500 + 1;
                int end = start + 1000;
                results.clear();
                tree_->rangeSearch(QVariant(start), QVariant(end), results);
            }
        });
    }

    /**
     * @brief 混合操作性能测试
     */
    void benchmarkMixedOperations() {
        const int COUNT = 10000;
        std::random_device rd;
        std::mt19937 g(rd());
        std::uniform_int_distribution<> dist(1, 50000);

        runBatchBenchmark("Mixed Operations (10K: 70% read, 20% insert, 10% delete)", COUNT, [&]() {
            for (int i = 0; i < COUNT; ++i) {
                int op = i % 10;
                int key = dist(g);

                if (op < 7) {  // 70% 读操作
                    RowId value;
                    tree_->search(QVariant(key), value);
                } else if (op < 9) {  // 20% 插入操作
                    tree_->insert(QVariant(key + 50000), key);
                } else {  // 10% 删除操作
                    tree_->remove(QVariant(key));
                }
            }
        });
    }

    QTemporaryFile* tempFile_;
    QString dbPath_;
    DiskManager* diskMgr_;
    BufferPoolManager* bufferPool_;
    GenericBPlusTree* tree_;
};

} // namespace benchmark
} // namespace qindb
