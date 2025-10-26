#include "test_framework.h"
#include "qindb/parser.h"
#include "qindb/ast.h"
#include <QCoreApplication>
#include <iostream>

using namespace qindb;
using namespace qindb::test;
using namespace qindb::ast;

/**
 * @brief Parser 测试套件
 */
class ParserTests : public TestCase {
public:
    ParserTests() : TestCase("ParserTests") {}

    void run() override {
        testSelectBasic();
        testSelectWithWhere();
        testSelectWithJoin();
        testSelectIntoOutfile();
        testSelectIntoOutfileWithFormat();
        testInsertBasic();
        testUpdateBasic();
        testDeleteBasic();
        testCreateTable();
        testDropTable();
        testCreateIndex();
        testCreateUser();
        testGrant();
        testRevoke();
    }

private:
    void testSelectBasic() {
        startTimer();
        try {
            QString sql = "SELECT id, name FROM users;";
            Parser parser(sql);
            auto stmt = parser.parse();

            assertNotNull(stmt.get(), "Parse result should not be null");

            auto selectStmt = dynamic_cast<SelectStatement*>(stmt.get());
            assertNotNull(selectStmt, "Statement should be SelectStatement");
            assertEqual(2, (int)selectStmt->selectList.size(), "Should have 2 columns");
            assertTrue(selectStmt->from != nullptr, "Should have FROM clause");
            assertEqual(QString("users"), selectStmt->from->tableName, "Table name should be 'users'");

            addResult("testSelectBasic", true, "SELECT basic parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testSelectBasic", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testSelectWithWhere() {
        startTimer();
        try {
            QString sql = "SELECT * FROM employees WHERE salary > 50000;";
            Parser parser(sql);
            auto stmt = parser.parse();

            auto selectStmt = dynamic_cast<SelectStatement*>(stmt.get());
            assertNotNull(selectStmt, "Statement should be SelectStatement");
            assertNotNull(selectStmt->where.get(), "Should have WHERE clause");

            addResult("testSelectWithWhere", true, "SELECT with WHERE parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testSelectWithWhere", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testSelectWithJoin() {
        startTimer();
        try {
            QString sql = "SELECT * FROM users INNER JOIN orders ON users.id = orders.user_id;";
            Parser parser(sql);
            auto stmt = parser.parse();

            auto selectStmt = dynamic_cast<SelectStatement*>(stmt.get());
            assertNotNull(selectStmt, "Statement should be SelectStatement");
            assertTrue(selectStmt->joins.size() > 0, "Should have JOIN clause");

            addResult("testSelectWithJoin", true, "SELECT with JOIN parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testSelectWithJoin", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testSelectIntoOutfile() {
        startTimer();
        try {
            QString sql = "SELECT * FROM employees INTO OUTFILE 'output.csv';";
            Parser parser(sql);
            auto stmt = parser.parse();

            auto selectStmt = dynamic_cast<SelectStatement*>(stmt.get());
            assertNotNull(selectStmt, "Statement should be SelectStatement");
            assertEqual(QString("output.csv"), selectStmt->exportFilePath, "Export path should be 'output.csv'");
            assertEqual(QString("CSV"), selectStmt->exportFormat, "Default format should be CSV");

            addResult("testSelectIntoOutfile", true, "SELECT INTO OUTFILE parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testSelectIntoOutfile", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testSelectIntoOutfileWithFormat() {
        startTimer();
        try {
            QString sql = "SELECT * FROM employees INTO OUTFILE 'output.json' FORMAT JSON;";
            Parser parser(sql);
            auto stmt = parser.parse();

            auto selectStmt = dynamic_cast<SelectStatement*>(stmt.get());
            assertNotNull(selectStmt, "Statement should be SelectStatement");
            assertEqual(QString("output.json"), selectStmt->exportFilePath, "Export path should be 'output.json'");
            assertEqual(QString("JSON"), selectStmt->exportFormat, "Format should be JSON");

            addResult("testSelectIntoOutfileWithFormat", true, "SELECT INTO OUTFILE with FORMAT parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testSelectIntoOutfileWithFormat", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testInsertBasic() {
        startTimer();
        try {
            QString sql = "INSERT INTO users (id, name) VALUES (1, 'Alice');";
            Parser parser(sql);
            auto stmt = parser.parse();

            auto insertStmt = dynamic_cast<InsertStatement*>(stmt.get());
            assertNotNull(insertStmt, "Statement should be InsertStatement");
            assertEqual(QString("users"), insertStmt->tableName, "Table name should be 'users'");
            assertEqual(2, (int)insertStmt->columns.size(), "Should have 2 columns");

            addResult("testInsertBasic", true, "INSERT parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testInsertBasic", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testUpdateBasic() {
        startTimer();
        try {
            QString sql = "UPDATE users SET name = 'Bob' WHERE id = 1;";
            Parser parser(sql);
            auto stmt = parser.parse();

            auto updateStmt = dynamic_cast<UpdateStatement*>(stmt.get());
            assertNotNull(updateStmt, "Statement should be UpdateStatement");
            assertEqual(QString("users"), updateStmt->tableName, "Table name should be 'users'");
            assertNotNull(updateStmt->where.get(), "Should have WHERE clause");

            addResult("testUpdateBasic", true, "UPDATE parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testUpdateBasic", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testDeleteBasic() {
        startTimer();
        try {
            QString sql = "DELETE FROM users WHERE id = 1;";
            Parser parser(sql);
            auto stmt = parser.parse();

            auto deleteStmt = dynamic_cast<DeleteStatement*>(stmt.get());
            assertNotNull(deleteStmt, "Statement should be DeleteStatement");
            assertEqual(QString("users"), deleteStmt->tableName, "Table name should be 'users'");
            assertNotNull(deleteStmt->where.get(), "Should have WHERE clause");

            addResult("testDeleteBasic", true, "DELETE parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testDeleteBasic", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testCreateTable() {
        startTimer();
        try {
            QString sql = "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(50));";
            Parser parser(sql);
            auto stmt = parser.parse();

            auto createStmt = dynamic_cast<CreateTableStatement*>(stmt.get());
            assertNotNull(createStmt, "Statement should be CreateTableStatement");
            assertEqual(QString("users"), createStmt->tableName, "Table name should be 'users'");
            assertEqual(2, (int)createStmt->columns.size(), "Should have 2 columns");

            addResult("testCreateTable", true, "CREATE TABLE parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testCreateTable", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testDropTable() {
        startTimer();
        try {
            QString sql = "DROP TABLE users;";
            Parser parser(sql);
            auto stmt = parser.parse();

            auto dropStmt = dynamic_cast<DropTableStatement*>(stmt.get());
            assertNotNull(dropStmt, "Statement should be DropTableStatement");
            assertEqual(QString("users"), dropStmt->tableName, "Table name should be 'users'");

            addResult("testDropTable", true, "DROP TABLE parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testDropTable", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testCreateIndex() {
        startTimer();
        try {
            QString sql = "CREATE INDEX idx_name ON users(name);";
            Parser parser(sql);
            auto stmt = parser.parse();

            auto createIndexStmt = dynamic_cast<CreateIndexStatement*>(stmt.get());
            assertNotNull(createIndexStmt, "Statement should be CreateIndexStatement");
            assertEqual(QString("idx_name"), createIndexStmt->indexName, "Index name should be 'idx_name'");
            assertEqual(QString("users"), createIndexStmt->tableName, "Table name should be 'users'");

            addResult("testCreateIndex", true, "CREATE INDEX parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testCreateIndex", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testCreateUser() {
        startTimer();
        try {
            QString sql = "CREATE USER alice IDENTIFIED BY 'password123';";
            Parser parser(sql);
            auto stmt = parser.parse();

            auto createUserStmt = dynamic_cast<CreateUserStatement*>(stmt.get());
            assertNotNull(createUserStmt, "Statement should be CreateUserStatement");
            assertEqual(QString("alice"), createUserStmt->username, "Username should be 'alice'");
            assertEqual(QString("password123"), createUserStmt->password, "Password should be 'password123'");

            addResult("testCreateUser", true, "CREATE USER parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testCreateUser", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testGrant() {
        startTimer();
        try {
            QString sql = "GRANT SELECT ON testdb.users TO alice;";
            Parser parser(sql);
            auto stmt = parser.parse();

            auto grantStmt = dynamic_cast<GrantStatement*>(stmt.get());
            assertNotNull(grantStmt, "Statement should be GrantStatement");
            assertEqual(QString("alice"), grantStmt->username, "Username should be 'alice'");

            addResult("testGrant", true, "GRANT parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testGrant", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testRevoke() {
        startTimer();
        try {
            QString sql = "REVOKE SELECT ON testdb.users FROM alice;";
            Parser parser(sql);
            auto stmt = parser.parse();

            auto revokeStmt = dynamic_cast<RevokeStatement*>(stmt.get());
            assertNotNull(revokeStmt, "Statement should be RevokeStatement");
            assertEqual(QString("alice"), revokeStmt->username, "Username should be 'alice'");

            addResult("testRevoke", true, "REVOKE parsing works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testRevoke", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }
};

#ifndef QINDB_TEST_MAIN_INCLUDED
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    TestSuite suite("Parser Tests");
    suite.addTest(new ParserTests());

    TestRunner::instance().registerSuite(&suite);
    int result = TestRunner::instance().runAll();

    return result;
}
#endif
