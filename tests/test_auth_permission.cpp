#include "test_framework.h"
#include "qindb/auth_manager.h"
#include "qindb/permission_manager.h"
#include "qindb/catalog.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include <QCoreApplication>
#include <iostream>
#include <QFile>

using namespace qindb;
using namespace qindb::test;

/**
 * @brief Auth & Permission 测试套件
 */
class AuthPermissionTests : public TestCase {
public:
    AuthPermissionTests() : TestCase("AuthPermissionTests") {}

    void run() override {
        testCreateUser();
        testAuthenticateUser();
        testDropUser();
        testAlterUserPassword();
        testUserExists();
        testUserAdminCheck();
        testGrantPermission();
        testRevokePermission();
        testHasPermission();
        testGrantOption();
        testRevokeAllPermissions();
        testPermissionInheritance();
    }

private:
    struct TestContext {
        std::unique_ptr<DiskManager> diskManager;
        std::unique_ptr<BufferPoolManager> bufferPool;
        std::unique_ptr<Catalog> catalog;
        std::unique_ptr<AuthManager> authManager;
        std::unique_ptr<PermissionManager> permissionManager;
    };

    TestContext createTestContext() {
        TestContext ctx;

        QString dbFile = "test_auth.db";
        QString catalogFile = "test_auth.json";
        QFile::remove(dbFile);
        QFile::remove(catalogFile);

        ctx.diskManager = std::make_unique<DiskManager>(dbFile);
        ctx.bufferPool = std::make_unique<BufferPoolManager>(50, ctx.diskManager.get());
        ctx.catalog = std::make_unique<Catalog>();
        ctx.catalog->setDatabaseBackend(ctx.bufferPool.get(), ctx.diskManager.get());

        ctx.authManager = std::make_unique<AuthManager>(
            ctx.catalog.get(),
            ctx.bufferPool.get(),
            ctx.diskManager.get()
        );

        ctx.permissionManager = std::make_unique<PermissionManager>(
            ctx.bufferPool.get(),
            ctx.catalog.get(),
            "testdb"
        );

        // Initialize systems
        ctx.authManager->initializeUserSystem();
        ctx.permissionManager->initializePermissionSystem();

        return ctx;
    }

    void testCreateUser() {
        startTimer();
        try {
            auto ctx = createTestContext();

            bool created = ctx.authManager->createUser("alice", "password123", false);
            assertTrue(created, "Should create user successfully");

            bool exists = ctx.authManager->userExists("alice");
            assertTrue(exists, "User should exist after creation");

            // Try creating duplicate user
            bool duplicate = ctx.authManager->createUser("alice", "pass", false);
            assertFalse(duplicate, "Should not create duplicate user");

            addResult("testCreateUser", true, "Create user works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testCreateUser", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testAuthenticateUser() {
        startTimer();
        try {
            auto ctx = createTestContext();

            ctx.authManager->createUser("bob", "secret123", false);

            // Correct password
            bool authenticated = ctx.authManager->authenticate("bob", "secret123");
            assertTrue(authenticated, "Should authenticate with correct password");

            // Wrong password
            bool wrongPass = ctx.authManager->authenticate("bob", "wrongpass");
            assertFalse(wrongPass, "Should not authenticate with wrong password");

            // Non-existent user
            bool noUser = ctx.authManager->authenticate("nobody", "pass");
            assertFalse(noUser, "Should not authenticate non-existent user");

            addResult("testAuthenticateUser", true, "User authentication works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testAuthenticateUser", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testDropUser() {
        startTimer();
        try {
            auto ctx = createTestContext();

            ctx.authManager->createUser("temp_user", "pass", false);
            assertTrue(ctx.authManager->userExists("temp_user"), "User should exist");

            bool dropped = ctx.authManager->dropUser("temp_user");
            assertTrue(dropped, "Should drop user successfully");

            assertFalse(ctx.authManager->userExists("temp_user"), "User should not exist after drop");

            addResult("testDropUser", true, "Drop user works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testDropUser", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testAlterUserPassword() {
        startTimer();
        try {
            auto ctx = createTestContext();

            ctx.authManager->createUser("charlie", "oldpass", false);

            // Change password
            bool altered = ctx.authManager->alterUserPassword("charlie", "newpass");
            assertTrue(altered, "Should alter password successfully");

            // Old password should not work
            assertFalse(ctx.authManager->authenticate("charlie", "oldpass"),
                       "Old password should not work");

            // New password should work
            assertTrue(ctx.authManager->authenticate("charlie", "newpass"),
                      "New password should work");

            addResult("testAlterUserPassword", true, "Alter user password works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testAlterUserPassword", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testUserExists() {
        startTimer();
        try {
            auto ctx = createTestContext();

            assertFalse(ctx.authManager->userExists("nonexistent"), "Non-existent user should return false");

            ctx.authManager->createUser("david", "pass", false);
            assertTrue(ctx.authManager->userExists("david"), "Existing user should return true");

            addResult("testUserExists", true, "User exists check works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testUserExists", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testUserAdminCheck() {
        startTimer();
        try {
            auto ctx = createTestContext();

            ctx.authManager->createUser("regular_user", "pass", false);
            ctx.authManager->createUser("admin_user", "pass", true);

            assertFalse(ctx.authManager->isUserAdmin("regular_user"), "Regular user should not be admin");
            assertTrue(ctx.authManager->isUserAdmin("admin_user"), "Admin user should be admin");

            addResult("testUserAdminCheck", true, "User admin check works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testUserAdminCheck", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testGrantPermission() {
        startTimer();
        try {
            auto ctx = createTestContext();

            bool granted = ctx.permissionManager->grantPermission(
                "alice", "testdb", "users", PermissionType::SELECT, false, "admin"
            );
            assertTrue(granted, "Should grant permission successfully");

            bool hasPermission = ctx.permissionManager->hasPermission(
                "alice", "testdb", "users", PermissionType::SELECT
            );
            assertTrue(hasPermission, "User should have granted permission");

            addResult("testGrantPermission", true, "Grant permission works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testGrantPermission", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testRevokePermission() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Grant then revoke
            ctx.permissionManager->grantPermission(
                "bob", "testdb", "orders", PermissionType::INSERT, false, "admin"
            );

            bool revoked = ctx.permissionManager->revokePermission(
                "bob", "testdb", "orders", PermissionType::INSERT
            );
            assertTrue(revoked, "Should revoke permission successfully");

            bool hasPermission = ctx.permissionManager->hasPermission(
                "bob", "testdb", "orders", PermissionType::INSERT
            );
            assertFalse(hasPermission, "User should not have revoked permission");

            addResult("testRevokePermission", true, "Revoke permission works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testRevokePermission", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testHasPermission() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // User without permission
            assertFalse(ctx.permissionManager->hasPermission(
                "charlie", "testdb", "products", PermissionType::DELETE
            ), "User without permission should return false");

            // Grant permission
            ctx.permissionManager->grantPermission(
                "charlie", "testdb", "products", PermissionType::DELETE, false, "admin"
            );

            // User with permission
            assertTrue(ctx.permissionManager->hasPermission(
                "charlie", "testdb", "products", PermissionType::DELETE
            ), "User with permission should return true");

            addResult("testHasPermission", true, "Has permission check works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testHasPermission", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testGrantOption() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Grant with grant option
            ctx.permissionManager->grantPermission(
                "david", "testdb", "employees", PermissionType::UPDATE, true, "admin"
            );

            bool hasGrantOption = ctx.permissionManager->hasGrantOption(
                "david", "testdb", "employees", PermissionType::UPDATE
            );
            assertTrue(hasGrantOption, "User should have grant option");

            // Grant without grant option
            ctx.permissionManager->grantPermission(
                "eve", "testdb", "employees", PermissionType::UPDATE, false, "admin"
            );

            bool noGrantOption = ctx.permissionManager->hasGrantOption(
                "eve", "testdb", "employees", PermissionType::UPDATE
            );
            assertFalse(noGrantOption, "User should not have grant option");

            addResult("testGrantOption", true, "Grant option works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testGrantOption", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testRevokeAllPermissions() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Grant multiple permissions
            ctx.permissionManager->grantPermission(
                "frank", "testdb", "table1", PermissionType::SELECT, false, "admin"
            );
            ctx.permissionManager->grantPermission(
                "frank", "testdb", "table2", PermissionType::INSERT, false, "admin"
            );
            ctx.permissionManager->grantPermission(
                "frank", "testdb", "table3", PermissionType::UPDATE, false, "admin"
            );

            // Revoke all
            bool revoked = ctx.permissionManager->revokeAllPermissions("frank");
            assertTrue(revoked, "Should revoke all permissions successfully");

            // Verify all permissions are gone
            assertFalse(ctx.permissionManager->hasPermission(
                "frank", "testdb", "table1", PermissionType::SELECT
            ), "Permission should be revoked");

            assertFalse(ctx.permissionManager->hasPermission(
                "frank", "testdb", "table2", PermissionType::INSERT
            ), "Permission should be revoked");

            addResult("testRevokeAllPermissions", true, "Revoke all permissions works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testRevokeAllPermissions", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testPermissionInheritance() {
        startTimer();
        try {
            auto ctx = createTestContext();

            // Grant different permissions to same user
            ctx.permissionManager->grantPermission(
                "george", "testdb", "users", PermissionType::SELECT, false, "admin"
            );
            ctx.permissionManager->grantPermission(
                "george", "testdb", "users", PermissionType::INSERT, false, "admin"
            );
            ctx.permissionManager->grantPermission(
                "george", "testdb", "users", PermissionType::UPDATE, false, "admin"
            );

            // Check each permission separately
            assertTrue(ctx.permissionManager->hasPermission(
                "george", "testdb", "users", PermissionType::SELECT
            ), "Should have SELECT permission");

            assertTrue(ctx.permissionManager->hasPermission(
                "george", "testdb", "users", PermissionType::INSERT
            ), "Should have INSERT permission");

            assertTrue(ctx.permissionManager->hasPermission(
                "george", "testdb", "users", PermissionType::UPDATE
            ), "Should have UPDATE permission");

            // Should not have DELETE (not granted)
            assertFalse(ctx.permissionManager->hasPermission(
                "george", "testdb", "users", PermissionType::DELETE
            ), "Should not have DELETE permission");

            addResult("testPermissionInheritance", true, "Multiple permissions work", stopTimer());
        } catch (const std::exception& e) {
            addResult("testPermissionInheritance", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }
};

#ifndef QINDB_TEST_MAIN_INCLUDED
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    TestSuite suite("Auth & Permission Tests");
    suite.addTest(new AuthPermissionTests());

    TestRunner::instance().registerSuite(&suite);
    int result = TestRunner::instance().runAll();

    return result;
}
#endif