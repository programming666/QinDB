#include "benchmark_framework.h"
#include "qindb/logger.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include <QTemporaryFile>
#include <random>

namespace qindb {
namespace benchmark {

/**
 * @brief 缓冲池性能测试
 */
class BufferPoolBenchmark : public Benchmark {
public:
    BufferPoolBenchmark() : Benchmark("Buffer Pool Performance") {}

    void setup() override {
        tempFile_ = new QTemporaryFile();
        tempFile_->setAutoRemove(true);
        if (!tempFile_->open()) {
             LOG_ERROR("Failed to open temporary file");
             return;
        }
        dbPath_ = tempFile_->fileName();
        tempFile_->close();

        diskMgr_ = new DiskManager(dbPath_);
    }

    void teardown() override {
        delete diskMgr_;
        delete tempFile_;
    }

    void run() override {
        benchmarkPageCreation();
        benchmarkPageFetch();
        benchmarkCacheHitRate();
        benchmarkLRUEfficiency();
    }

private:
    /**
     * @brief 页面创建性能测试
     */
    void benchmarkPageCreation() {
        BufferPoolManager bufferPool(1000, diskMgr_);
        const int COUNT = 5000;

        runBatchBenchmark("Page Creation (5K pages)", COUNT, [&]() {
            for (int i = 0; i < COUNT; ++i) {
                PageId pageId;
                Page* page = bufferPool.newPage(&pageId);
                if (page) {
                    bufferPool.unpinPage(pageId, false);
                }
            }
        });
    }

    /**
     * @brief 页面获取性能测试
     */
    void benchmarkPageFetch() {
        BufferPoolManager bufferPool(1000, diskMgr_);

        // 先创建一些页面
        std::vector<PageId> pageIds;
        for (int i = 0; i < 1000; ++i) {
            PageId pageId;
            Page* page = bufferPool.newPage(&pageId);
            if (page) {
                pageIds.push_back(pageId);
                bufferPool.unpinPage(pageId, true);
            }
        }

        const int COUNT = 10000;
        std::random_device rd;
        std::mt19937 g(rd());
        std::uniform_int_distribution<> dist(0, pageIds.size() - 1);

        runBatchBenchmark("Page Fetch (10K random fetches)", COUNT, [&]() {
            for (int i = 0; i < COUNT; ++i) {
                PageId pageId = pageIds[dist(g)];
                Page* page = bufferPool.fetchPage(pageId);
                if (page) {
                    bufferPool.unpinPage(pageId, false);
                }
            }
        });
    }

    /**
     * @brief 缓存命中率测试
     */
    void benchmarkCacheHitRate() {
        const int POOL_SIZE = 100;
        const int TOTAL_PAGES = 200;
        const int ACCESSES = 10000;

        BufferPoolManager bufferPool(POOL_SIZE, diskMgr_);

        // 创建页面
        std::vector<PageId> pageIds;
        for (int i = 0; i < TOTAL_PAGES; ++i) {
            PageId pageId;
            Page* page = bufferPool.newPage(&pageId);
            if (page) {
                pageIds.push_back(pageId);
                bufferPool.unpinPage(pageId, true);
            }
        }

        std::random_device rd;
        std::mt19937 g(rd());

        // 80/20法则: 80%的访问集中在20%的页面上
        std::uniform_real_distribution<> prob(0.0, 1.0);
        std::uniform_int_distribution<> hot_dist(0, TOTAL_PAGES / 5 - 1);  // 20%热点
        std::uniform_int_distribution<> cold_dist(TOTAL_PAGES / 5, TOTAL_PAGES - 1);  // 80%冷数据

        runBatchBenchmark("Cache Hit Rate Test (80/20 pattern, 10K accesses)", ACCESSES, [&]() {
            for (int i = 0; i < ACCESSES; ++i) {
                int idx;
                if (prob(g) < 0.8) {  // 80%访问热点数据
                    idx = hot_dist(g);
                } else {  // 20%访问冷数据
                    idx = cold_dist(g);
                }

                PageId pageId = pageIds[idx];
                Page* page = bufferPool.fetchPage(pageId);
                if (page) {
                    bufferPool.unpinPage(pageId, false);
                }
            }
        });

        addInfo("Pool size: 100, Total pages: 200, Access pattern: 80/20");
    }

    /**
     * @brief LRU替换效率测试
     */
    void benchmarkLRUEfficiency() {
        const int POOL_SIZE = 50;
        const int TOTAL_ACCESSES = 5000;

        BufferPoolManager bufferPool(POOL_SIZE, diskMgr_);

        // 创建比缓冲池更多的页面
        std::vector<PageId> pageIds;
        for (int i = 0; i < POOL_SIZE * 3; ++i) {
            PageId pageId;
            Page* page = bufferPool.newPage(&pageId);
            if (page) {
                pageIds.push_back(pageId);
                bufferPool.unpinPage(pageId, true);
            }
        }

        std::random_device rd;
        std::mt19937 g(rd());
        std::uniform_int_distribution<> dist(0, pageIds.size() - 1);

        runBatchBenchmark("LRU Replacement (5K accesses, pool exhaustion)", TOTAL_ACCESSES, [&]() {
            for (int i = 0; i < TOTAL_ACCESSES; ++i) {
                PageId pageId = pageIds[dist(g)];
                Page* page = bufferPool.fetchPage(pageId);
                if (page) {
                    bufferPool.unpinPage(pageId, false);
                }
            }
        });

        addInfo("Pool size: 50, Total pages: 150, LRU replacements triggered");
    }

    QTemporaryFile* tempFile_;
    QString dbPath_;
    DiskManager* diskMgr_;
};

} // namespace benchmark
} // namespace qindb
