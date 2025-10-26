#include "test_framework.h"
#include "qindb/query_cache.h"
#include "qindb/query_result.h"
#include <QCoreApplication>
#include <QThread>
#include <iostream>

using namespace qindb;
using namespace qindb::test;

/**
 * @brief QueryCache 测试套件
 */
class QueryCacheTests : public TestCase {
public:
    QueryCacheTests() : TestCase("QueryCacheTests") {}

    void run() override {
        testPutAndGet();
        testCacheMiss();
        testCacheInvalidation();
        testCacheEviction();
        testCacheStats();
        testNormalizeQuery();
        testDisableCache();
        testMultipleTables();
    }

private:
    QueryResult createTestResult(int rowCount) {
        QueryResult result;
        result.success = true;
        result.columnNames = {"id", "name"};

        for (int i = 0; i < rowCount; ++i) {
            QVector<QVariant> row = {i, QString("User%1").arg(i)};
            result.rows.append(row);
        }

        return result;
    }

    void testPutAndGet() {
        startTimer();
        try {
            QueryCache cache;
            cache.setEnabled(true);

            QString query = "SELECT * FROM users WHERE id = 1";
            QueryResult originalResult = createTestResult(1);
            QSet<QString> tables = {"users"};

            // 放入缓存
            bool putSuccess = cache.put(query, originalResult, tables);
            assertTrue(putSuccess, "Should successfully put result in cache");

            // 从缓存获取
            QueryResult cachedResult;
            bool getSuccess = cache.get(query, cachedResult);
            assertTrue(getSuccess, "Should successfully get result from cache");
            assertEqual(originalResult.rows.size(), cachedResult.rows.size(), "Cached result should have same row count");

            addResult("testPutAndGet", true, "Cache put/get works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testPutAndGet", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testCacheMiss() {
        startTimer();
        try {
            QueryCache cache;
            cache.setEnabled(true);

            QString query = "SELECT * FROM users WHERE id = 999";
            QueryResult result;

            bool getSuccess = cache.get(query, result);
            assertFalse(getSuccess, "Should miss cache for non-existent query");

            addResult("testCacheMiss", true, "Cache miss works correctly", stopTimer());
        } catch (const std::exception& e) {
            addResult("testCacheMiss", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testCacheInvalidation() {
        startTimer();
        try {
            QueryCache cache;
            cache.setEnabled(true);

            QString query = "SELECT * FROM users";
            QueryResult result = createTestResult(3);
            QSet<QString> tables = {"users"};

            // 放入缓存
            cache.put(query, result, tables);

            // 验证缓存存在
            QueryResult cachedResult;
            assertTrue(cache.get(query, cachedResult), "Cache should exist before invalidation");

            // 失效缓存
            cache.invalidateTable("users");

            // 验证缓存已失效
            QueryResult afterInvalidation;
            assertFalse(cache.get(query, afterInvalidation), "Cache should be invalidated");

            addResult("testCacheInvalidation", true, "Cache invalidation works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testCacheInvalidation", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testCacheEviction() {
        startTimer();
        try {
            QueryCache cache(5);  // 最大5个条目
            cache.setEnabled(true);

            QSet<QString> tables = {"test"};

            // 添加6个查询（超过最大容量）
            for (int i = 0; i < 6; ++i) {
                QString query = QString("SELECT * FROM test WHERE id = %1").arg(i);
                QueryResult result = createTestResult(1);
                cache.put(query, result, tables);
            }

            // 获取统计信息
            auto stats = cache.getStatistics();
            assertTrue(stats.totalEntries <= 5, "Cache should evict entries to stay under limit");

            addResult("testCacheEviction", true, "Cache eviction works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testCacheEviction", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testCacheStats() {
        startTimer();
        try {
            QueryCache cache;
            cache.setEnabled(true);

            QString query1 = "SELECT * FROM users WHERE id = 1";
            QString query2 = "SELECT * FROM users WHERE id = 2";
            QueryResult result = createTestResult(1);
            QSet<QString> tables = {"users"};

            // 添加两个查询
            cache.put(query1, result, tables);
            cache.put(query2, result, tables);

            // 缓存命中
            QueryResult cachedResult;
            cache.get(query1, cachedResult);

            // 缓存未命中
            cache.get("SELECT * FROM nonexistent", cachedResult);

            auto stats = cache.getStatistics();
            assertEqual(2, (int)stats.totalEntries, "Should have 2 entries");
            assertEqual(1, (int)stats.totalHits, "Should have 1 hit");
            assertEqual(1, (int)stats.totalMisses, "Should have 1 miss");
            assertTrue(stats.totalMemoryBytes > 0, "Memory usage should be > 0");

            addResult("testCacheStats", true, "Cache statistics work", stopTimer());
        } catch (const std::exception& e) {
            addResult("testCacheStats", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testNormalizeQuery() {
        startTimer();
        try {
            QString query1 = "  SELECT  *  FROM  users  WHERE  id  =  1  ";
            QString query2 = "SELECT * FROM users WHERE id = 1";

            QString normalized1 = QueryCache::normalizeQuery(query1);
            QString normalized2 = QueryCache::normalizeQuery(query2);

            assertEqual(normalized1, normalized2, "Normalized queries should be identical");

            addResult("testNormalizeQuery", true, "Query normalization works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testNormalizeQuery", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testDisableCache() {
        startTimer();
        try {
            QueryCache cache;
            cache.setEnabled(false);  // 禁用缓存

            QString query = "SELECT * FROM users";
            QueryResult result = createTestResult(3);
            QSet<QString> tables = {"users"};

            // 尝试放入缓存
            bool putSuccess = cache.put(query, result, tables);
            assertFalse(putSuccess, "Put should fail when cache is disabled");

            // 尝试从缓存获取
            QueryResult cachedResult;
            bool getSuccess = cache.get(query, cachedResult);
            assertFalse(getSuccess, "Get should fail when cache is disabled");

            addResult("testDisableCache", true, "Disabled cache works correctly", stopTimer());
        } catch (const std::exception& e) {
            addResult("testDisableCache", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testMultipleTables() {
        startTimer();
        try {
            QueryCache cache;
            cache.setEnabled(true);

            QString query = "SELECT * FROM users JOIN orders ON users.id = orders.user_id";
            QueryResult result = createTestResult(5);
            QSet<QString> tables = {"users", "orders"};

            cache.put(query, result, tables);

            // 失效其中一个表
            cache.invalidateTable("users");

            // 验证缓存已失效
            QueryResult cachedResult;
            assertFalse(cache.get(query, cachedResult), "Cache should be invalidated when any table is modified");

            addResult("testMultipleTables", true, "Multiple table invalidation works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testMultipleTables", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }
};

#ifndef QINDB_TEST_MAIN_INCLUDED
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    TestSuite suite("Query Cache Tests");
    suite.addTest(new QueryCacheTests());

    TestRunner::instance().registerSuite(&suite);
    int result = TestRunner::instance().runAll();

    return result;
}
#endif
