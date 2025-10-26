#include "test_framework.h"
#include "qindb/hash_index.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include "qindb/config.h"
#include <QCoreApplication>
#include <iostream>
#include <QFile>

using namespace qindb;
using namespace qindb::test;

/**
 * @brief HashIndex 测试套件
 */
class HashIndexTests : public TestCase {
public:
    HashIndexTests() : TestCase("HashIndexTests") {}

    void run() override {
        testBasicInsertAndSearch();
        testMultipleInserts();
        testRemove();
        testResize();
        testDifferentTypes();
        testDuplicateKeys();
        testNotFound();
    }

private:
    void testBasicInsertAndSearch() {
        startTimer();
        try {
            QString dbFile = "test_hash_index.db";
            QFile::remove(dbFile);

            DiskManager diskManager(dbFile);
            BufferPoolManager bufferPool(50, &diskManager);
            HashIndex index("test_index", DataType::INT, &bufferPool, 256);

            // 插入键值对
            QVariant key(42);
            RowId rowId = 100;

            bool inserted = index.insert(key, rowId);
            assertTrue(inserted, "Should insert successfully");

            // 搜索
            std::vector<RowId> results;
            bool found = index.searchAll(key, results);
            assertTrue(found, "Should find the inserted key");
            assertEqual(static_cast<size_t>(1), results.size(), "Should have 1 result");
            assertEqual((RowId)100, results[0], "Should return correct RowId");

            addResult("testBasicInsertAndSearch", true, "Basic insert and search works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testBasicInsertAndSearch", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testMultipleInserts() {
        startTimer();
        try {
            QString dbFile = "test_hash_index.db";
            QFile::remove(dbFile);

            DiskManager diskManager(dbFile);
            BufferPoolManager bufferPool(50, &diskManager);
            HashIndex index("test_index", DataType::INT, &bufferPool, 256);

            // 插入多个键值对
            for (int i = 0; i < 100; ++i) {
                QVariant key(i);
                RowId rowId = i * 10 + 1;  // 避免使用 0（INVALID_ROW_ID）
                bool inserted = index.insert(key, rowId);
                assertTrue(inserted, QString("Should insert key %1").arg(i));
            }

            // 验证所有键都能找到
            for (int i = 0; i < 100; ++i) {
                QVariant key(i);
                std::vector<RowId> results;
                bool found = index.searchAll(key, results);
                assertTrue(found, QString("Should find key %1").arg(i));
                assertEqual((RowId)(i * 10 + 1), results[0], QString("Should return correct RowId for key %1").arg(i));
            }

            addResult("testMultipleInserts", true, "Multiple inserts work", stopTimer());
        } catch (const std::exception& e) {
            addResult("testMultipleInserts", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testRemove() {
        startTimer();
        try {
            QString dbFile = "test_hash_index.db";
            QFile::remove(dbFile);

            DiskManager diskManager(dbFile);
            BufferPoolManager bufferPool(50, &diskManager);
            HashIndex index("test_index", DataType::INT, &bufferPool, 256);

            // 插入然后删除
            QVariant key(42);
            RowId rowId = 100;
            index.insert(key, rowId);

            bool removed = index.remove(key, rowId);
            assertTrue(removed, "Should remove successfully");

            // 验证已删除
            std::vector<RowId> results;
            bool found = index.searchAll(key, results);
            assertFalse(found, "Should not find removed key");

            addResult("testRemove", true, "Remove works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testRemove", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testResize() {
        startTimer();
        try {
            QString dbFile = "test_hash_index.db";
            QFile::remove(dbFile);

            DiskManager diskManager(dbFile);
            BufferPoolManager bufferPool(50, &diskManager);
            HashIndex index("test_index", DataType::INT, &bufferPool, 256);

            // 插入大量数据触发扩容
            for (int i = 0; i < 200; ++i) {
                QVariant key(i);
                index.insert(key, i + 1);  // 避免使用 0（INVALID_ROW_ID）
            }

            // 验证扩容后数据仍然可访问
            for (int i = 0; i < 200; ++i) {
                QVariant key(i);
                std::vector<RowId> results;
                bool found = index.searchAll(key, results);
                assertTrue(found, QString("Should find key %1 after resize").arg(i));
            }

            addResult("testResize", true, "Resize works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testResize", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testDifferentTypes() {
        startTimer();
        try {
            QString dbFile = "test_hash_index.db";
            QFile::remove(dbFile);

            DiskManager diskManager(dbFile);
            BufferPoolManager bufferPool(50, &diskManager);

            // 测试 VARCHAR
            {
                HashIndex index("test_varchar", DataType::VARCHAR, &bufferPool, 256);
                QVariant key("hello");
                index.insert(key, 1);

                std::vector<RowId> results;
                assertTrue(index.searchAll(key, results), "Should find VARCHAR key");
            }

            // 测试 DOUBLE
            {
                HashIndex index("test_double", DataType::DOUBLE, &bufferPool, 256);
                QVariant key(3.14);
                index.insert(key, 2);

                std::vector<RowId> results;
                assertTrue(index.searchAll(key, results), "Should find DOUBLE key");
            }

            addResult("testDifferentTypes", true, "Different types work", stopTimer());
        } catch (const std::exception& e) {
            addResult("testDifferentTypes", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testDuplicateKeys() {
        startTimer();
        try {
            QString dbFile = "test_hash_index.db";
            QFile::remove(dbFile);

            DiskManager diskManager(dbFile);
            BufferPoolManager bufferPool(50, &diskManager);
            HashIndex index("test_index", DataType::INT, &bufferPool, 256);

            // 插入相同键的多个值
            QVariant key(42);
            index.insert(key, 100);
            index.insert(key, 200);
            index.insert(key, 300);

            // 搜索应返回所有值
            std::vector<RowId> results;
            bool found = index.searchAll(key, results);
            assertTrue(found, "Should find duplicate keys");
            assertEqual(static_cast<size_t>(3), results.size(), "Should have 3 results for duplicate keys");

            addResult("testDuplicateKeys", true, "Duplicate keys work", stopTimer());
        } catch (const std::exception& e) {
            addResult("testDuplicateKeys", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testNotFound() {
        startTimer();
        try {
            QString dbFile = "test_hash_index.db";
            QFile::remove(dbFile);

            DiskManager diskManager(dbFile);
            BufferPoolManager bufferPool(50, &diskManager);
            HashIndex index("test_index", DataType::INT, &bufferPool, 256);

            QVariant key(999);
            std::vector<RowId> results;
            bool found = index.searchAll(key, results);

            assertFalse(found, "Should not find non-existent key");
            assertTrue(results.empty(), "Results should be empty");

            addResult("testNotFound", true, "Not found case works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testNotFound", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }
};

#ifndef QINDB_TEST_MAIN_INCLUDED
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    TestSuite suite("Hash Index Tests");
    suite.addTest(new HashIndexTests());

    TestRunner::instance().registerSuite(&suite);
    int result = TestRunner::instance().runAll();

    return result;
}
#endif
