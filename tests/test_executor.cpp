#include "test_framework.h"
#include "qindb/executor.h"
#include "qindb/parser.h"
#include "qindb/database_manager.h"
#include "qindb/catalog.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include <QCoreApplication>
#include <iostream>
#include <QFile>
#include <QDir>

using namespace qindb;
using namespace qindb::test;

/**
 * @brief Executor 综合测试套件
 */
class ExecutorTests : public TestCase {
public:
    ExecutorTests() : TestCase("ExecutorTests") {}

    void run() override {
        testExecuteCreateTable();
        testExecuteInsert();
        testExecuteSelect();
        testExecuteUpdate();
        testExecuteDelete();
        testExecuteDropTable();
        testExecuteCreateIndex();
        testExecuteShowTables();
        testInsertAndSelectIntegration();
        testUpdateAndSelectIntegration();
        testWhereClauseFiltering();
        testMultipleInserts();
    }

private:
    struct TestContext {
        std::unique_ptr<DatabaseManager> dbManager;
        std::unique_ptr<Executor> executor;
    };

    TestContext createTestContext() {
        TestContext ctx;

        static int dbCounter = 0;  // 为每个测试创建独立的数据库
        QString dbDir = QString("test_executor_db_%1").arg(dbCounter++);

        // 彻底清理测试数据库目录
        QDir dir(dbDir);
        if (dir.exists()) {
            dir.removeRecursively();
        }
        dir.mkpath(".");

        ctx.dbManager = std::make_unique<DatabaseManager>(dbDir);

        // Create and select a test database
        ctx.dbManager->createDatabase("testdb", true);
        ctx.dbManager->useDatabase("testdb");

        ctx.executor = std::make_unique<Executor>(ctx.dbManager.get());

        return ctx;
    }

    void testExecuteCreateTable() {
        startTimer();
        try {
            auto ctx = createTestContext();

            QString sql = "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(50));";
            Parser parser(sql);
            auto ast = parser.parse();

            QueryResult result = ctx.executor->execute(ast);
            assertTrue(result.success, "CREATE TABLE should succeed");
            // 检查消息包含表名和created关键字
            assertTrue(result.message.contains("users") && result.message.contains("created"),
                      "Should contain table name and success indicator");

            addResult("testExecuteCreateTable", true, "Execute CREATE TABLE works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExecuteCreateTable", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExecuteInsert() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Create table first
            QString createSql = "CREATE TABLE products (id INT PRIMARY KEY, name VARCHAR(100), price DOUBLE);";
            Parser createParser(createSql);
            auto createAst = createParser.parse();
            ctx.executor->execute(createAst);

            // Insert data
            QString insertSql = "INSERT INTO products (id, name, price) VALUES (1, 'Laptop', 999.99);";
            Parser insertParser(insertSql);
            auto insertAst = insertParser.parse();

            QueryResult result = ctx.executor->execute(insertAst);
            assertTrue(result.success, "INSERT should succeed");

            addResult("testExecuteInsert", true, "Execute INSERT works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExecuteInsert", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExecuteSelect() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Setup: Create table and insert data
            ctx.executor->execute(Parser("CREATE TABLE employees (id INT, name VARCHAR(50), salary DOUBLE);").parse());
            ctx.executor->execute(Parser("INSERT INTO employees VALUES (1, 'Alice', 50000);").parse());
            ctx.executor->execute(Parser("INSERT INTO employees VALUES (2, 'Bob', 60000);").parse());

            // Execute SELECT
            QString selectSql = "SELECT * FROM employees;";
            Parser selectParser(selectSql);
            auto selectAst = selectParser.parse();

            QueryResult result = ctx.executor->execute(selectAst);
            assertTrue(result.success, "SELECT should succeed");
            assertEqual(static_cast<int>(2), static_cast<int>(result.rows.size()), "Should return 2 rows");
            assertEqual(static_cast<int>(3), static_cast<int>(result.columnNames.size()), "Should have 3 columns");

            addResult("testExecuteSelect", true, "Execute SELECT works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExecuteSelect", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExecuteUpdate() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Setup
            ctx.executor->execute(Parser("CREATE TABLE items (id INT, name VARCHAR(50), quantity INT);").parse());
            ctx.executor->execute(Parser("INSERT INTO items VALUES (1, 'Widget', 10);").parse());

            // Execute UPDATE
            QString updateSql = "UPDATE items SET quantity = 20 WHERE id = 1;";
            Parser updateParser(updateSql);
            auto updateAst = updateParser.parse();

            QueryResult result = ctx.executor->execute(updateAst);
            assertTrue(result.success, "UPDATE should succeed");

            // Verify update
            QueryResult selectResult = ctx.executor->execute(Parser("SELECT * FROM items WHERE id = 1;").parse());
            assertTrue(selectResult.rows.size() > 0, "Should find updated row");

            addResult("testExecuteUpdate", true, "Execute UPDATE works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExecuteUpdate", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExecuteDelete() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Setup
            ctx.executor->execute(Parser("CREATE TABLE temp (id INT, value VARCHAR(20));").parse());
            ctx.executor->execute(Parser("INSERT INTO temp VALUES (1, 'test1');").parse());
            ctx.executor->execute(Parser("INSERT INTO temp VALUES (2, 'test2');").parse());

            // Execute DELETE
            QString deleteSql = "DELETE FROM temp WHERE id = 1;";
            Parser deleteParser(deleteSql);
            auto deleteAst = deleteParser.parse();

            QueryResult result = ctx.executor->execute(deleteAst);
            assertTrue(result.success, "DELETE should succeed");

            // Verify deletion
            QueryResult selectResult = ctx.executor->execute(Parser("SELECT * FROM temp;").parse());
            assertEqual(qsizetype(1), selectResult.rows.size(), "Should have 1 row remaining");

            addResult("testExecuteDelete", true, "Execute DELETE works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExecuteDelete", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExecuteDropTable() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Create table
            ctx.executor->execute(Parser("CREATE TABLE to_drop (id INT);").parse());

            // Drop table
            QString dropSql = "DROP TABLE to_drop;";
            Parser dropParser(dropSql);
            auto dropAst = dropParser.parse();

            QueryResult result = ctx.executor->execute(dropAst);
            assertTrue(result.success, "DROP TABLE should succeed");

            addResult("testExecuteDropTable", true, "Execute DROP TABLE works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExecuteDropTable", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExecuteCreateIndex() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Create table first
            ctx.executor->execute(Parser("CREATE TABLE indexed_table (id INT, name VARCHAR(50));").parse());

            // Create index
            QString indexSql = "CREATE INDEX idx_name ON indexed_table(name);";
            Parser indexParser(indexSql);
            auto indexAst = indexParser.parse();

            QueryResult result = ctx.executor->execute(indexAst);
            assertTrue(result.success, "CREATE INDEX should succeed");

            addResult("testExecuteCreateIndex", true, "Execute CREATE INDEX works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExecuteCreateIndex", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExecuteShowTables() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Create a few tables
            ctx.executor->execute(Parser("CREATE TABLE table1 (id INT);").parse());
            ctx.executor->execute(Parser("CREATE TABLE table2 (id INT);").parse());
            ctx.executor->execute(Parser("CREATE TABLE table3 (id INT);").parse());

            // Execute SHOW TABLES
            QueryResult result = ctx.executor->executeShowTables();
            assertTrue(result.success, "SHOW TABLES should succeed");
            assertEqual(qsizetype(3), result.rows.size(), "Should show 3 tables");

            addResult("testExecuteShowTables", true, "Execute SHOW TABLES works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExecuteShowTables", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testInsertAndSelectIntegration() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Create table
            ctx.executor->execute(Parser("CREATE TABLE customers (id INT, name VARCHAR(100), email VARCHAR(100));").parse());

            // Insert multiple rows
            ctx.executor->execute(Parser("INSERT INTO customers VALUES (1, 'Alice', 'alice@example.com');").parse());
            ctx.executor->execute(Parser("INSERT INTO customers VALUES (2, 'Bob', 'bob@example.com');").parse());
            ctx.executor->execute(Parser("INSERT INTO customers VALUES (3, 'Charlie', 'charlie@example.com');").parse());

            // Select all
            QueryResult result = ctx.executor->execute(Parser("SELECT * FROM customers;").parse());
            assertTrue(result.success, "SELECT should succeed");
            assertEqual(qsizetype(3), result.rows.size(), "Should have 3 rows");

            // Verify data
            assertTrue(result.rows[0][1].toString().contains("Alice"), "First row should be Alice");
            assertTrue(result.rows[1][1].toString().contains("Bob"), "Second row should be Bob");

            addResult("testInsertAndSelectIntegration", true, "Insert and Select integration works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testInsertAndSelectIntegration", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testUpdateAndSelectIntegration() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Setup
            ctx.executor->execute(Parser("CREATE TABLE accounts (id INT, balance DOUBLE);").parse());
            ctx.executor->execute(Parser("INSERT INTO accounts VALUES (1, 1000.0);").parse());

            // Update
            ctx.executor->execute(Parser("UPDATE accounts SET balance = 1500.0 WHERE id = 1;").parse());

            // Select to verify
            QueryResult result = ctx.executor->execute(Parser("SELECT * FROM accounts WHERE id = 1;").parse());
            assertTrue(result.success, "SELECT should succeed");
            assertEqual(qsizetype(1), result.rows.size(), "Should have 1 row");

            // Verify updated value
            double balance = result.rows[0][1].toDouble();
            assertTrue(balance == 1500.0, "Balance should be updated to 1500.0");

            addResult("testUpdateAndSelectIntegration", true, "Update and Select integration works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testUpdateAndSelectIntegration", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testWhereClauseFiltering() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Setup
            ctx.executor->execute(Parser("CREATE TABLE orders (id INT, amount DOUBLE, status VARCHAR(20));").parse());
            ctx.executor->execute(Parser("INSERT INTO orders VALUES (1, 100.0, 'pending');").parse());
            ctx.executor->execute(Parser("INSERT INTO orders VALUES (2, 200.0, 'completed');").parse());
            ctx.executor->execute(Parser("INSERT INTO orders VALUES (3, 150.0, 'pending');").parse());
            ctx.executor->execute(Parser("INSERT INTO orders VALUES (4, 300.0, 'completed');").parse());

            // Select with WHERE clause
            QueryResult result = ctx.executor->execute(
                Parser("SELECT * FROM orders WHERE status = 'pending';").parse()
            );

            assertTrue(result.success, "SELECT with WHERE should succeed");
            assertEqual(qsizetype(2), result.rows.size(), "Should filter to 2 pending orders");

            addResult("testWhereClauseFiltering", true, "WHERE clause filtering works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testWhereClauseFiltering", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testMultipleInserts() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Create table
            ctx.executor->execute(Parser("CREATE TABLE logs (id INT, message VARCHAR(200));").parse());

            // Insert many rows
            for (int i = 0; i < 10; ++i) {
                QString sql = QString("INSERT INTO logs VALUES (%1, 'Log message %2');").arg(i).arg(i);
                QueryResult result = ctx.executor->execute(Parser(sql).parse());
                assertTrue(result.success, QString("Insert %1 should succeed").arg(i));
            }

            // Verify count
            QueryResult result = ctx.executor->execute(Parser("SELECT * FROM logs;").parse());
            assertEqual(qsizetype(10), result.rows.size(), "Should have 10 rows");

            addResult("testMultipleInserts", true, "Multiple inserts work", stopTimer());
        } catch (const std::exception& e) {
            addResult("testMultipleInserts", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }
};

#ifndef QINDB_TEST_MAIN_INCLUDED
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    TestSuite suite("Executor Tests");
    suite.addTest(new ExecutorTests());

    TestRunner::instance().registerSuite(&suite);
    int result = TestRunner::instance().runAll();

    return result;
}
#endif
