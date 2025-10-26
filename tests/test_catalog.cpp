#include "test_framework.h"
#include "qindb/catalog.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include <QCoreApplication>
#include <iostream>
#include <QFile>

using namespace qindb;
using namespace qindb::test;

/**
 * @brief Catalog 测试套件
 */
class CatalogTests : public TestCase {
public:
    CatalogTests() : TestCase("CatalogTests") {}

    void run() override {
        testCreateAndGetTable();
        testDropTable();
        testCreateIndex();
        testDropIndex();
        testTableExists();
        testGetAllTables();
        testColumnOperations();
    }

private:
    std::unique_ptr<Catalog> createCatalog() {
        QString dbFile = "test_catalog.db";
        QString catalogFile = "test_catalog.json";
        QFile::remove(dbFile);
        QFile::remove(catalogFile);

        auto diskManager = std::make_unique<DiskManager>(dbFile);
        auto bufferPool = std::make_unique<BufferPoolManager>(50, diskManager.get());

        auto catalog = std::make_unique<Catalog>();
        catalog->setDatabaseBackend(bufferPool.get(), diskManager.get());
        return catalog;
    }

    void testCreateAndGetTable() {
        startTimer();
        try {
            auto catalog = createCatalog();

            // 创建表定义
            TableDef tableDef;
            tableDef.name = "users";
            tableDef.firstPageId = 1;

            ColumnDef col1;
            col1.name = "id";
            col1.type = DataType::INT;
            col1.primaryKey = true;
            tableDef.columns.append(col1);

            ColumnDef col2;
            col2.name = "name";
            col2.type = DataType::VARCHAR;
            col2.length = 50;
            tableDef.columns.append(col2);

            // 创建表
            bool created = catalog->createTable(tableDef);
            assertTrue(created, "Should create table successfully");

            // 获取表
            const TableDef* table = catalog->getTable("users");
            assertNotNull(table, "Should get table successfully");
            assertEqual(QString("users"), table->name, "Table name should match");
            assertEqual(2, static_cast<int>(table->columns.size()), "Should have 2 columns");
            assertEqual(QString("id"), table->columns[0].name, "First column should be 'id'");

            addResult("testCreateAndGetTable", true, "Create and get table works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testCreateAndGetTable", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testDropTable() {
        startTimer();
        try {
            auto catalog = createCatalog();

            // 创建表
            TableDef tableDef;
            tableDef.name = "temp_table";
            tableDef.firstPageId = 1;
            catalog->createTable(tableDef);

            // 删除表
            bool dropped = catalog->dropTable("temp_table");
            assertTrue(dropped, "Should drop table successfully");

            // 验证已删除
            const TableDef* table = catalog->getTable("temp_table");
            assertNull(table, "Table should not exist after drop");

            addResult("testDropTable", true, "Drop table works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testDropTable", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testCreateIndex() {
        startTimer();
        try {
            auto catalog = createCatalog();

            // 先创建表
            TableDef tableDef;
            tableDef.name = "users";
            tableDef.firstPageId = 1;

            ColumnDef col;
            col.name = "id";
            col.type = DataType::INT;
            tableDef.columns.append(col);

            catalog->createTable(tableDef);

            // 创建索引
            IndexDef indexDef;
            indexDef.name = "idx_id";
            indexDef.tableName = "users";
            indexDef.columns.append("id");
            indexDef.indexType = qindb::IndexType::BTREE;
            indexDef.rootPageId = 10;

            bool created = catalog->createIndex(indexDef);
            assertTrue(created, "Should create index successfully");

            // 获取索引
            const IndexDef* index = catalog->getIndex("idx_id");
            assertNotNull(index, "Should get index successfully");
            assertEqual(QString("idx_id"), index->name, "Index name should match");

            addResult("testCreateIndex", true, "Create index works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testCreateIndex", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testDropIndex() {
        startTimer();
        try {
            auto catalog = createCatalog();

            // 创建表和索引
            TableDef tableDef;
            tableDef.name = "users";
            tableDef.firstPageId = 1;

            ColumnDef col;
            col.name = "id";
            col.type = DataType::INT;
            tableDef.columns.append(col);

            catalog->createTable(tableDef);

            IndexDef indexDef;
            indexDef.name = "idx_id";
            indexDef.tableName = "users";
            indexDef.columns.append("id");
            indexDef.rootPageId = 10;

            catalog->createIndex(indexDef);

            // 删除索引
            bool dropped = catalog->dropIndex("idx_id");
            assertTrue(dropped, "Should drop index successfully");

            // 验证已删除
            const IndexDef* index = catalog->getIndex("idx_id");
            assertNull(index, "Index should not exist after drop");

            addResult("testDropIndex", true, "Drop index works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testDropIndex", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testTableExists() {
        startTimer();
        try {
            auto catalog = createCatalog();

            // 表不存在
            assertFalse(catalog->tableExists("nonexistent"), "Non-existent table should return false");

            // 创建表
            TableDef tableDef;
            tableDef.name = "test_table";
            tableDef.firstPageId = 1;
            catalog->createTable(tableDef);

            // 表存在
            assertTrue(catalog->tableExists("test_table"), "Existing table should return true");

            addResult("testTableExists", true, "Table exists check works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testTableExists", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testGetAllTables() {
        startTimer();
        try {
            auto catalog = createCatalog();

            // 创建多个表
            for (int i = 0; i < 3; ++i) {
                TableDef tableDef;
                tableDef.name = QString("table%1").arg(i);
                tableDef.firstPageId = i + 1;
                catalog->createTable(tableDef);
            }

            // 获取所有表
            QVector<QString> tables = catalog->getAllTableNames();
            assertEqual(3, static_cast<int>(tables.size()), "Should have 3 tables");

            addResult("testGetAllTables", true, "Get all tables works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testGetAllTables", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testColumnOperations() {
        startTimer();
        try {
            auto catalog = createCatalog();

            // 创建表
            TableDef tableDef;
            tableDef.name = "users";
            tableDef.firstPageId = 1;

            ColumnDef col1;
            col1.name = "id";
            col1.type = DataType::INT;
            col1.primaryKey = true;
            tableDef.columns.append(col1);

            ColumnDef col2;
            col2.name = "name";
            col2.type = DataType::VARCHAR;
            col2.length = 50;
            col2.notNull = true;
            tableDef.columns.append(col2);

            catalog->createTable(tableDef);

            // 验证列属性
            const TableDef* table = catalog->getTable("users");
            assertNotNull(table, "Table should exist");
            assertEqual(2, static_cast<int>(table->columns.size()), "Should have 2 columns");

            assertTrue(table->columns[0].primaryKey, "First column should be primary key");
            assertTrue(table->columns[1].notNull, "Second column should be NOT NULL");

            addResult("testColumnOperations", true, "Column operations work", stopTimer());
        } catch (const std::exception& e) {
            addResult("testColumnOperations", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }
};

#ifndef QINDB_TEST_MAIN_INCLUDED
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    TestSuite suite("Catalog Tests");
    suite.addTest(new CatalogTests());

    TestRunner::instance().registerSuite(&suite);
    int result = TestRunner::instance().runAll();

    return result;
}
#endif