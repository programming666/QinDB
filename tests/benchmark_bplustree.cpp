#include "benchmark_framework.h"
#include "qindb/generic_bplustree.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include "qindb/config.h"
#include <QTemporaryFile>
#include <random>

namespace qindb {
namespace benchmark {

/**
 * @brief B+树性能测试
 */
class BPlusTreeBenchmark : public Benchmark {
public:
    BPlusTreeBenchmark() : Benchmark("B+ Tree Performance") {}

    void setup() override {
        tempFile_ = new QTemporaryFile();
        tempFile_->setAutoRemove(true);
        tempFile_->open();
        dbPath_ = tempFile_->fileName();
        tempFile_->close();

        diskMgr_ = new DiskManager(dbPath_);
        Config& config = Config::instance();
        bufferPool_ = new BufferPoolManager(config.getBufferPoolSize(), diskMgr_);
        tree_ = new GenericBPlusTree(bufferPool_, DataType::INT);
    }

    void teardown() override {
        delete tree_;
        delete bufferPool_;
        delete diskMgr_;
        delete tempFile_;
    }

    void run() override {
        benchmarkSequentialInsert();
        benchmarkRandomInsert();
        benchmarkSequentialSearch();
        benchmarkRandomSearch();
        benchmarkRangeSearch();
        benchmarkMixedOperations();
    }

private:
    /**
     * @brief 顺序插入性能测试
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
