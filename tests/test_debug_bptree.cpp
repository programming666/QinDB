#include "test_framework.h"
#include "qindb/generic_bplustree.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include "qindb/config.h"
#include <QTemporaryFile>
#include <QCoreApplication>
#include <iostream>

using namespace qindb;
using namespace qindb::test;

/**
 * @brief 调试大数据集B+树问题的独立测试
 */
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    std::cout << "========== B+ Tree Large Dataset Debug Test ==========" << std::endl;

    // 创建临时数据库文件
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(false);  // 保留文件以便调试
    if (!tempFile.open()) {
        std::cout << "Failed to create temp file" << std::endl;
        return 1;
    }
    QString dbPath = tempFile.fileName();
    std::cout << "Using temp file: " << dbPath.toStdString() << std::endl;
    tempFile.close();

    DiskManager diskMgr(dbPath);
    Config& config = Config::instance();
    BufferPoolManager bufferPool(config.getBufferPoolSize(), &diskMgr);

    GenericBPlusTree tree(&bufferPool, DataType::INT);

    // 插入10000个键
    const int COUNT = 10000;
    std::cout << "\nInserting " << COUNT << " keys..." << std::endl;

    int failedInserts = 0;
    for (int i = 1; i <= COUNT; ++i) {
        bool inserted = tree.insert(QVariant(i), i);
        if (!inserted) {
            std::cout << "Failed to insert key " << i << std::endl;
            failedInserts++;
        }

        if (i % 1000 == 0) {
            std::cout << "  Inserted " << i << " keys..." << std::endl;
        }
    }

    std::cout << "Insertion complete. Failed inserts: " << failedInserts << std::endl;

    // 验证每隔100个键
    std::cout << "\nVerifying keys (every 100th key)..." << std::endl;
    int failedSearches = 0;

    for (int i = 1; i <= COUNT; i += 100) {
        RowId foundRowId = INVALID_ROW_ID;
        bool found = tree.search(QVariant(i), foundRowId);

        if (!found) {
            std::cout << "  ✗ Key " << i << " NOT FOUND" << std::endl;
            failedSearches++;
        } else if (foundRowId != static_cast<RowId>(i)) {
            std::cout << "  ✗ Key " << i << " found but RowId mismatch: expected "
                      << i << ", got " << foundRowId << std::endl;
            failedSearches++;
        } else {
            std::cout << "  ✓ Key " << i << " found correctly" << std::endl;
        }
    }

    std::cout << "\n========== Test Summary ==========" << std::endl;
    std::cout << "Total keys inserted: " << (COUNT - failedInserts) << "/" << COUNT << std::endl;
    std::cout << "Failed searches: " << failedSearches << "/100" << std::endl;

    if (failedSearches == 0) {
        std::cout << "\n✓ ALL TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "\n✗ SOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
