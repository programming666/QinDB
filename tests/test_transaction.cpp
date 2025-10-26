#include "test_framework.h"
#include "qindb/transaction.h"
#include "qindb/wal.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include <QCoreApplication>
#include <iostream>
#include <QFile>
#include <QThread>

using namespace qindb;
using namespace qindb::test;

/**
 * @brief Transaction 测试套件
 */
class TransactionTests : public TestCase {
public:
    TransactionTests() : TestCase("TransactionTests") {}

    void run() override {
        testBeginTransaction();
        testCommitTransaction();
        testAbortTransaction();
        testTransactionState();
        testPageLocking();
        testSharedLockCompatibility();
        testExclusiveLockBlocking();
        testLockTimeout();
        testUndoLogTracking();
        testMultipleTransactions();
        testReleaseLocksOnCommit();
    }

private:
    void testBeginTransaction() {
        startTimer();
        try {
            QString dbFile = "test_txn.db";
            QString walFile = "test_txn.wal";
            QFile::remove(dbFile);
            QFile::remove(walFile);

            auto diskManager = std::make_unique<DiskManager>(dbFile);
            auto bufferPool = std::make_unique<BufferPoolManager>(50, diskManager.get());
            auto walManager = std::make_unique<WALManager>(walFile);
            walManager->setDatabaseBackend(bufferPool.get(), diskManager.get());
            walManager->initialize();
            auto txnManager = std::make_unique<TransactionManager>(walManager.get());

            TransactionId txnId = txnManager->beginTransaction();
            assertTrue(txnId != INVALID_TXN_ID, "Transaction ID should be valid");

            TransactionState state = txnManager->getTransactionState(txnId);
            assertTrue(state == TransactionState::ACTIVE, "New transaction should be ACTIVE");

            addResult("testBeginTransaction", true, "Begin transaction works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testBeginTransaction", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testCommitTransaction() {
        startTimer();
        try {
            QString dbFile = "test_txn.db";
            QString walFile = "test_txn.wal";
            QFile::remove(dbFile);
            QFile::remove(walFile);

            auto diskManager = std::make_unique<DiskManager>(dbFile);
            auto bufferPool = std::make_unique<BufferPoolManager>(50, diskManager.get());
            auto walManager = std::make_unique<WALManager>(walFile);
            walManager->setDatabaseBackend(bufferPool.get(), diskManager.get());
            walManager->initialize();
            auto txnManager = std::make_unique<TransactionManager>(walManager.get());

            TransactionId txnId = txnManager->beginTransaction();
            bool committed = txnManager->commitTransaction(txnId);
            assertTrue(committed, "Commit should succeed");

            TransactionState state = txnManager->getTransactionState(txnId);
            assertTrue(state == TransactionState::COMMITTED, "Transaction should be COMMITTED");

            addResult("testCommitTransaction", true, "Commit transaction works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testCommitTransaction", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testAbortTransaction() {
        startTimer();
        try {
            QString dbFile = "test_txn.db";
            QString walFile = "test_txn.wal";
            QFile::remove(dbFile);
            QFile::remove(walFile);

            auto diskManager = std::make_unique<DiskManager>(dbFile);
            auto bufferPool = std::make_unique<BufferPoolManager>(50, diskManager.get());
            auto walManager = std::make_unique<WALManager>(walFile);
            walManager->setDatabaseBackend(bufferPool.get(), diskManager.get());
            walManager->initialize();
            auto txnManager = std::make_unique<TransactionManager>(walManager.get());

            TransactionId txnId = txnManager->beginTransaction();
            bool aborted = txnManager->abortTransaction(txnId);
            assertTrue(aborted, "Abort should succeed");

            TransactionState state = txnManager->getTransactionState(txnId);
            assertTrue(state == TransactionState::ABORTED, "Transaction should be ABORTED");

            addResult("testAbortTransaction", true, "Abort transaction works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testAbortTransaction", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testTransactionState() {
        startTimer();
        try {
            QString dbFile = "test_txn.db";
            QString walFile = "test_txn.wal";
            QFile::remove(dbFile);
            QFile::remove(walFile);

            auto diskManager = std::make_unique<DiskManager>(dbFile);
            auto bufferPool = std::make_unique<BufferPoolManager>(50, diskManager.get());
            auto walManager = std::make_unique<WALManager>(walFile);
            walManager->setDatabaseBackend(bufferPool.get(), diskManager.get());
            walManager->initialize();
            auto txnManager = std::make_unique<TransactionManager>(walManager.get());

            // Invalid transaction
            TransactionState invalidState = txnManager->getTransactionState(INVALID_TXN_ID);
            assertTrue(invalidState == TransactionState::INVALID, "Invalid ID should return INVALID state");

            // Active transaction
            TransactionId txnId = txnManager->beginTransaction();
            Transaction* txn = txnManager->getTransaction(txnId);
            assertNotNull(txn, "Should get valid transaction pointer");
            assertTrue(txn->state == TransactionState::ACTIVE, "Transaction should be active");

            addResult("testTransactionState", true, "Transaction state tracking works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testTransactionState", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testPageLocking() {
        startTimer();
        try {
            QString dbFile = "test_txn.db";
            QString walFile = "test_txn.wal";
            QFile::remove(dbFile);
            QFile::remove(walFile);

            auto diskManager = std::make_unique<DiskManager>(dbFile);
            auto bufferPool = std::make_unique<BufferPoolManager>(50, diskManager.get());
            auto walManager = std::make_unique<WALManager>(walFile);
            walManager->setDatabaseBackend(bufferPool.get(), diskManager.get());
            walManager->initialize();
            auto txnManager = std::make_unique<TransactionManager>(walManager.get());

            TransactionId txnId = txnManager->beginTransaction();
            PageId pageId = 100;

            // Acquire shared lock
            bool locked = txnManager->lockPage(txnId, pageId, LockType::SHARED);
            assertTrue(locked, "Should acquire shared lock");

            // Unlock page
            bool unlocked = txnManager->unlockPage(txnId, pageId);
            assertTrue(unlocked, "Should release lock");

            addResult("testPageLocking", true, "Page locking works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testPageLocking", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testSharedLockCompatibility() {
        startTimer();
        try {
            QString dbFile = "test_txn.db";
            QString walFile = "test_txn.wal";
            QFile::remove(dbFile);
            QFile::remove(walFile);

            auto diskManager = std::make_unique<DiskManager>(dbFile);
            auto bufferPool = std::make_unique<BufferPoolManager>(50, diskManager.get());
            auto walManager = std::make_unique<WALManager>(walFile);
            walManager->setDatabaseBackend(bufferPool.get(), diskManager.get());
            walManager->initialize();
            auto txnManager = std::make_unique<TransactionManager>(walManager.get());

            TransactionId txn1 = txnManager->beginTransaction();
            TransactionId txn2 = txnManager->beginTransaction();
            PageId pageId = 200;

            // Both transactions acquire shared locks (should succeed)
            bool lock1 = txnManager->lockPage(txn1, pageId, LockType::SHARED);
            assertTrue(lock1, "First shared lock should succeed");

            bool lock2 = txnManager->lockPage(txn2, pageId, LockType::SHARED);
            assertTrue(lock2, "Second shared lock should succeed (compatible)");

            addResult("testSharedLockCompatibility", true, "Shared lock compatibility works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testSharedLockCompatibility", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExclusiveLockBlocking() {
        startTimer();
        try {
            QString dbFile = "test_txn.db";
            QString walFile = "test_txn.wal";
            QFile::remove(dbFile);
            QFile::remove(walFile);

            auto diskManager = std::make_unique<DiskManager>(dbFile);
            auto bufferPool = std::make_unique<BufferPoolManager>(50, diskManager.get());
            auto walManager = std::make_unique<WALManager>(walFile);
            walManager->setDatabaseBackend(bufferPool.get(), diskManager.get());
            walManager->initialize();
            auto txnManager = std::make_unique<TransactionManager>(walManager.get());

            TransactionId txn1 = txnManager->beginTransaction();
            TransactionId txn2 = txnManager->beginTransaction();
            PageId pageId = 300;

            // First transaction gets exclusive lock
            bool lock1 = txnManager->lockPage(txn1, pageId, LockType::EXCLUSIVE);
            assertTrue(lock1, "First exclusive lock should succeed");

            // Second transaction tries to get lock (should timeout)
            bool lock2 = txnManager->lockPage(txn2, pageId, LockType::SHARED, 100);
            assertFalse(lock2, "Second lock should fail due to exclusive lock");

            addResult("testExclusiveLockBlocking", true, "Exclusive lock blocking works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExclusiveLockBlocking", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testLockTimeout() {
        startTimer();
        try {
            QString dbFile = "test_txn.db";
            QString walFile = "test_txn.wal";
            QFile::remove(dbFile);
            QFile::remove(walFile);

            auto diskManager = std::make_unique<DiskManager>(dbFile);
            auto bufferPool = std::make_unique<BufferPoolManager>(50, diskManager.get());
            auto walManager = std::make_unique<WALManager>(walFile);
            walManager->setDatabaseBackend(bufferPool.get(), diskManager.get());
            walManager->initialize();
            auto txnManager = std::make_unique<TransactionManager>(walManager.get());

            TransactionId txn1 = txnManager->beginTransaction();
            TransactionId txn2 = txnManager->beginTransaction();
            PageId pageId = 400;

            // Transaction 1 holds exclusive lock
            txnManager->lockPage(txn1, pageId, LockType::EXCLUSIVE);

            // Transaction 2 times out trying to acquire lock
            auto startTime = QDateTime::currentMSecsSinceEpoch();
            bool acquired = txnManager->lockPage(txn2, pageId, LockType::EXCLUSIVE, 500);
            auto elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;

            assertFalse(acquired, "Lock acquisition should fail");
            assertTrue(elapsed >= 500, "Should wait at least timeout duration");

            addResult("testLockTimeout", true, "Lock timeout works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testLockTimeout", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testUndoLogTracking() {
        startTimer();
        try {
            QString dbFile = "test_txn.db";
            QString walFile = "test_txn.wal";
            QFile::remove(dbFile);
            QFile::remove(walFile);

            auto diskManager = std::make_unique<DiskManager>(dbFile);
            auto bufferPool = std::make_unique<BufferPoolManager>(50, diskManager.get());
            auto walManager = std::make_unique<WALManager>(walFile);
            walManager->setDatabaseBackend(bufferPool.get(), diskManager.get());
            walManager->initialize();
            auto txnManager = std::make_unique<TransactionManager>(walManager.get());

            TransactionId txnId = txnManager->beginTransaction();
            Transaction* txn = txnManager->getTransaction(txnId);

            // Add undo records
            UndoRecord record1 = UndoRecord::createInsertUndo("test_table", 100, 5, 0);
            UndoRecord record2 = UndoRecord::createUpdateUndo("test_table", 101, 10, QVector<QVariant>(), 0);

            txnManager->addUndoRecord(txnId, record1);
            txnManager->addUndoRecord(txnId, record2);

            assertEqual(static_cast<int>(2), static_cast<int>(txn->undoLog.size()), "Should have 2 undo records");
            assertEqual((PageId)100, txn->undoLog[0].pageId, "First record pageId should match");
            assertEqual((PageId)101, txn->undoLog[1].pageId, "Second record pageId should match");

            addResult("testUndoLogTracking", true, "Undo log tracking works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testUndoLogTracking", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testMultipleTransactions() {
        startTimer();
        try {
            QString dbFile = "test_txn.db";
            QString walFile = "test_txn.wal";
            QFile::remove(dbFile);
            QFile::remove(walFile);

            auto diskManager = std::make_unique<DiskManager>(dbFile);
            auto bufferPool = std::make_unique<BufferPoolManager>(50, diskManager.get());
            auto walManager = std::make_unique<WALManager>(walFile);
            walManager->setDatabaseBackend(bufferPool.get(), diskManager.get());
            walManager->initialize();
            auto txnManager = std::make_unique<TransactionManager>(walManager.get());

            // Start multiple transactions
            TransactionId txn1 = txnManager->beginTransaction();
            TransactionId txn2 = txnManager->beginTransaction();
            TransactionId txn3 = txnManager->beginTransaction();

            assertEqual(3, txnManager->getActiveTransactionCount(), "Should have 3 active transactions");

            // Commit one
            txnManager->commitTransaction(txn1);
            assertEqual(2, txnManager->getActiveTransactionCount(), "Should have 2 active after commit");

            // Abort one
            txnManager->abortTransaction(txn2);
            assertEqual(1, txnManager->getActiveTransactionCount(), "Should have 1 active after abort");

            addResult("testMultipleTransactions", true, "Multiple transactions work", stopTimer());
        } catch (const std::exception& e) {
            addResult("testMultipleTransactions", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testReleaseLocksOnCommit() {
        startTimer();
        try {
            QString dbFile = "test_txn.db";
            QString walFile = "test_txn.wal";
            QFile::remove(dbFile);
            QFile::remove(walFile);

            auto diskManager = std::make_unique<DiskManager>(dbFile);
            auto bufferPool = std::make_unique<BufferPoolManager>(50, diskManager.get());
            auto walManager = std::make_unique<WALManager>(walFile);
            walManager->setDatabaseBackend(bufferPool.get(), diskManager.get());
            walManager->initialize();
            auto txnManager = std::make_unique<TransactionManager>(walManager.get());

            TransactionId txn1 = txnManager->beginTransaction();
            TransactionId txn2 = txnManager->beginTransaction();
            PageId pageId = 500;

            // Transaction 1 acquires lock
            txnManager->lockPage(txn1, pageId, LockType::EXCLUSIVE);

            // Transaction 2 cannot acquire lock
            assertFalse(txnManager->lockPage(txn2, pageId, LockType::SHARED, 100),
                       "Should not acquire lock while txn1 holds it");

            // Commit transaction 1 (releases locks)
            txnManager->commitTransaction(txn1);

            // Now transaction 2 should be able to acquire lock
            bool acquired = txnManager->lockPage(txn2, pageId, LockType::SHARED, 100);
            assertTrue(acquired, "Should acquire lock after txn1 committed");

            addResult("testReleaseLocksOnCommit", true, "Locks released on commit", stopTimer());
        } catch (const std::exception& e) {
            addResult("testReleaseLocksOnCommit", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }
};

#ifndef QINDB_TEST_MAIN_INCLUDED
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    TestSuite suite("Transaction Tests");
    suite.addTest(new TransactionTests());

    TestRunner::instance().registerSuite(&suite);
    int result = TestRunner::instance().runAll();

    return result;
}
#endif
