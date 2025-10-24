#include "test_framework.h"
#include "qindb/generic_bplustree.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include "qindb/config.h"
#include <QTemporaryFile>

namespace qindb {
namespace test {

/**
 * @brief B+树单元测试
 */
class BPlusTreeTest : public TestCase {
public:
    BPlusTreeTest() : TestCase("BPlusTreeTest") {}

    void run() override {
        try { testIntInsertAndSearch(); } catch (...) {}
        try { testStringInsertAndSearch(); } catch (...) {}
        try { testDoubleInsertAndSearch(); } catch (...) {}
        try { testRemove(); } catch (...) {}
        try { testRangeSearch(); } catch (...) {}
        try { testLargeDataset(); } catch (...) {}
    }

private:
    /**
     * @brief 测试INT类型插入和查找
     */
    void testIntInsertAndSearch() {
        startTimer();

        // 创建临时数据库文件
        QTemporaryFile tempFile;
        tempFile.setAutoRemove(true);
        assertTrue(tempFile.open(), "Failed to create temp file");
        QString dbPath = tempFile.fileName();
        tempFile.close();

        // 创建磁盘管理器和缓冲池
        DiskManager diskMgr(dbPath);
        Config& config = Config::instance();
        BufferPoolManager bufferPool(config.getBufferPoolSize(), &diskMgr);

        // 创建B+树（INT类型）
        GenericBPlusTree tree(&bufferPool, DataType::INT);

        // 插入测试数据
        for (int i = 1; i <= 100; ++i) {
            RowId rowId = i * 10;
            bool inserted = tree.insert(QVariant(i), rowId);
            assertTrue(inserted, QString("Failed to insert key %1").arg(i));
        }

        // 查找测试
        for (int i = 1; i <= 100; ++i) {
            RowId foundRowId = INVALID_ROW_ID;
            bool found = tree.search(QVariant(i), foundRowId);
            assertTrue(found, QString("Failed to find key %1").arg(i));
            assertEqual(static_cast<RowId>(i * 10), foundRowId,
                       QString("RowId mismatch for key %1").arg(i));
        }

        // 查找不存在的键
        RowId notFoundRowId = INVALID_ROW_ID;
        assertFalse(tree.search(QVariant(999), notFoundRowId), "Should not find key 999");

        double elapsed = stopTimer();
        addResult("testIntInsertAndSearch", true, "", elapsed);
    }

    /**
     * @brief 测试STRING类型插入和查找
     */
    void testStringInsertAndSearch() {
        startTimer();

        QTemporaryFile tempFile;
        tempFile.setAutoRemove(true);
        assertTrue(tempFile.open());
        QString dbPath = tempFile.fileName();
        tempFile.close();

        DiskManager diskMgr(dbPath);
        Config& config = Config::instance();
        BufferPoolManager bufferPool(config.getBufferPoolSize(), &diskMgr);

        GenericBPlusTree tree(&bufferPool, DataType::VARCHAR);

        // 插入字符串键
        QStringList names = {"Alice", "Bob", "Charlie", "David", "Eve"};
        for (int i = 0; i < names.size(); ++i) {
            bool inserted = tree.insert(QVariant(names[i]), i + 1);
            assertTrue(inserted, QString("Failed to insert key '%1'").arg(names[i]));
        }

        // 查找测试
        for (int i = 0; i < names.size(); ++i) {
            RowId foundRowId = INVALID_ROW_ID;
            bool found = tree.search(QVariant(names[i]), foundRowId);
            assertTrue(found, QString("Failed to find key '%1'").arg(names[i]));
            assertEqual(static_cast<RowId>(i + 1), foundRowId);
        }

        double elapsed = stopTimer();
        addResult("testStringInsertAndSearch", true, "", elapsed);
    }

    /**
     * @brief 测试DOUBLE类型插入和查找
     */
    void testDoubleInsertAndSearch() {
        startTimer();

        QTemporaryFile tempFile;
        tempFile.setAutoRemove(true);
        assertTrue(tempFile.open());
        QString dbPath = tempFile.fileName();
        tempFile.close();

        DiskManager diskMgr(dbPath);
        Config& config = Config::instance();
        BufferPoolManager bufferPool(config.getBufferPoolSize(), &diskMgr);

        GenericBPlusTree tree(&bufferPool, DataType::DOUBLE);

        // 插入浮点数键
        QVector<double> prices = {9.99, 19.99, 29.99, 39.99, 49.99};
        for (int i = 0; i < prices.size(); ++i) {
            bool inserted = tree.insert(QVariant(prices[i]), i + 1);
            assertTrue(inserted, QString("Failed to insert key %1").arg(prices[i]));
        }

        // 查找测试
        for (int i = 0; i < prices.size(); ++i) {
            RowId foundRowId = INVALID_ROW_ID;
            bool found = tree.search(QVariant(prices[i]), foundRowId);
            assertTrue(found, QString("Failed to find key %1").arg(prices[i]));
            assertEqual(static_cast<RowId>(i + 1), foundRowId);
        }

        double elapsed = stopTimer();
        addResult("testDoubleInsertAndSearch", true, "", elapsed);
    }

    /**
     * @brief 测试删除操作
     */
    void testRemove() {
        startTimer();

        QTemporaryFile tempFile;
        tempFile.setAutoRemove(true);
        assertTrue(tempFile.open());
        QString dbPath = tempFile.fileName();
        tempFile.close();

        DiskManager diskMgr(dbPath);
        Config& config = Config::instance();
        BufferPoolManager bufferPool(config.getBufferPoolSize(), &diskMgr);

        GenericBPlusTree tree(&bufferPool, DataType::INT);

        // 插入测试数据
        for (int i = 1; i <= 50; ++i) {
            tree.insert(QVariant(i), i);
        }

        // 删除部分键
        for (int i = 10; i <= 20; ++i) {
            bool removed = tree.remove(QVariant(i));
            assertTrue(removed, QString("Failed to remove key %1").arg(i));
        }

        // 验证删除结果
        for (int i = 10; i <= 20; ++i) {
            RowId foundRowId = INVALID_ROW_ID;
            assertFalse(tree.search(QVariant(i), foundRowId),
                       QString("Key %1 should have been removed").arg(i));
        }

        // 验证其他键仍然存在
        for (int i = 1; i <= 9; ++i) {
            RowId foundRowId = INVALID_ROW_ID;
            assertTrue(tree.search(QVariant(i), foundRowId),
                      QString("Key %1 should still exist").arg(i));
        }

        double elapsed = stopTimer();
        addResult("testRemove", true, "", elapsed);
    }

    /**
     * @brief 测试范围查询
     */
    void testRangeSearch() {
        startTimer();

        QTemporaryFile tempFile;
        tempFile.setAutoRemove(true);
        assertTrue(tempFile.open());
        QString dbPath = tempFile.fileName();
        tempFile.close();

        DiskManager diskMgr(dbPath);
        Config& config = Config::instance();
        BufferPoolManager bufferPool(config.getBufferPoolSize(), &diskMgr);

        GenericBPlusTree tree(&bufferPool, DataType::INT);

        // 插入测试数据
        for (int i = 1; i <= 100; ++i) {
            tree.insert(QVariant(i), i);
        }

        // 范围查询 [20, 30]
        QVector<std::pair<QVariant, RowId>> results;
        tree.rangeSearch(QVariant(20), QVariant(30), results);

        // 应该找到11个结果 (20, 21, ..., 30)
        assertEqual(static_cast<int>(11), static_cast<int>(results.size()), "Range search should return 11 results");

        double elapsed = stopTimer();
        addResult("testRangeSearch", true, "", elapsed);
    }

    /**
     * @brief 测试大数据集
     */
    void testLargeDataset() {
        startTimer();

        QTemporaryFile tempFile;
        tempFile.setAutoRemove(true);
        assertTrue(tempFile.open());
        QString dbPath = tempFile.fileName();
        tempFile.close();

        DiskManager diskMgr(dbPath);
        Config& config = Config::instance();
        BufferPoolManager bufferPool(config.getBufferPoolSize(), &diskMgr);

        GenericBPlusTree tree(&bufferPool, DataType::INT);

        // 插入10000个键
        const int COUNT = 10000;
        for (int i = 1; i <= COUNT; ++i) {
            tree.insert(QVariant(i), i);
        }

        // 随机查找验证
        for (int i = 1; i <= COUNT; i += 100) {
            RowId foundRowId = INVALID_ROW_ID;
            bool found = tree.search(QVariant(i), foundRowId);
            assertTrue(found, QString("Failed to find key %1 in large dataset").arg(i));
            assertEqual(static_cast<RowId>(i), foundRowId);
        }

        double elapsed = stopTimer();
        addResult("testLargeDataset", true,
                 QString("Inserted and searched %1 keys").arg(COUNT), elapsed);
    }
};

} // namespace test
} // namespace qindb
