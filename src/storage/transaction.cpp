#include "qindb/transaction.h"
#include "qindb/logger.h"
#include <QDateTime>
#include <QThread>

namespace qindb {

TransactionManager::TransactionManager(WALManager* walManager)
    : walManager_(walManager)
    , nextTxnId_(1)
{
    LOG_INFO("Transaction manager initialized");
}

TransactionManager::~TransactionManager() {
    // 回滚所有活跃事务
    QMutexLocker locker(&mutex_);

    QVector<TransactionId> activeTxns;
    for (auto it = transactions_.begin(); it != transactions_.end(); ++it) {
        if (it.value()->state == TransactionState::ACTIVE) {
            activeTxns.append(it.key());
        }
    }

    locker.unlock();

    for (TransactionId txnId : activeTxns) {
        LOG_WARN(QString("Aborting active transaction on shutdown: TxnID=%1").arg(txnId));
        abortTransaction(txnId);
    }

    LOG_INFO("Transaction manager destroyed");
}

TransactionId TransactionManager::generateTransactionId() {
    return nextTxnId_++;
}

TransactionId TransactionManager::beginTransaction() {
    QMutexLocker locker(&mutex_);

    TransactionId txnId = generateTransactionId();
    auto txn = std::make_shared<Transaction>(txnId);
    transactions_[txnId] = txn;

    locker.unlock();

    // 记录到 WAL
    if (walManager_) {
        walManager_->beginTransaction(txnId);
    }

    LOG_INFO(QString("Transaction started: TxnID=%1").arg(txnId));
    return txnId;
}

bool TransactionManager::commitTransaction(TransactionId txnId) {
    QMutexLocker locker(&mutex_);

    auto it = transactions_.find(txnId);
    if (it == transactions_.end()) {
        LOG_ERROR(QString("Transaction not found: TxnID=%1").arg(txnId));
        return false;
    }

    auto txn = it.value();
    if (txn->state != TransactionState::ACTIVE) {
        LOG_ERROR(QString("Transaction not active: TxnID=%1, State=%2")
                    .arg(txnId)
                    .arg(static_cast<int>(txn->state)));
        return false;
    }

    // 更新状态
    txn->state = TransactionState::COMMITTED;

    // 释放所有锁
    releaseAllLocks(txnId);

    locker.unlock();

    // 记录到 WAL（提交必须持久化）
    if (walManager_) {
        if (!walManager_->commitTransaction(txnId)) {
            LOG_ERROR(QString("Failed to write commit to WAL: TxnID=%1").arg(txnId));
            return false;
        }
    }

    LOG_INFO(QString("Transaction committed: TxnID=%1").arg(txnId));
    return true;
}

bool TransactionManager::abortTransaction(TransactionId txnId) {
    QMutexLocker locker(&mutex_);

    auto it = transactions_.find(txnId);
    if (it == transactions_.end()) {
        LOG_ERROR(QString("Transaction not found: TxnID=%1").arg(txnId));
        return false;
    }

    auto txn = it.value();
    if (txn->state != TransactionState::ACTIVE) {
        LOG_WARN(QString("Transaction not active: TxnID=%1, State=%2")
                    .arg(txnId)
                    .arg(static_cast<int>(txn->state)));
        return false;
    }

    // 更新状态
    txn->state = TransactionState::ABORTED;

    // TODO: 回滚操作（使用 undo log）
    // 目前只是简单地释放锁

    // 释放所有锁
    releaseAllLocks(txnId);

    locker.unlock();

    // 记录到 WAL
    if (walManager_) {
        walManager_->abortTransaction(txnId);
    }

    LOG_INFO(QString("Transaction aborted: TxnID=%1").arg(txnId));
    return true;
}

TransactionState TransactionManager::getTransactionState(TransactionId txnId) const {
    QMutexLocker locker(&mutex_);

    auto it = transactions_.find(txnId);
    if (it == transactions_.end()) {
        return TransactionState::INVALID;
    }

    return it.value()->state;
}

Transaction* TransactionManager::getTransaction(TransactionId txnId) {
    QMutexLocker locker(&mutex_);

    auto it = transactions_.find(txnId);
    if (it == transactions_.end()) {
        return nullptr;
    }

    return it.value().get();
}

bool TransactionManager::isLockCompatible(const PageLock& existingLock, LockType requestedLock) {
    // 共享锁与共享锁兼容
    if (existingLock.lockType == LockType::SHARED && requestedLock == LockType::SHARED) {
        return true;
    }

    // 排他锁与任何锁都不兼容（除非是同一个事务）
    return false;
}

bool TransactionManager::lockPage(TransactionId txnId, PageId pageId, LockType lockType, int timeoutMs) {
    auto startTime = QDateTime::currentMSecsSinceEpoch();

    while (true) {
        QMutexLocker locker(&mutex_);

        // 检查事务是否有效
        auto txnIt = transactions_.find(txnId);
        if (txnIt == transactions_.end() || txnIt.value()->state != TransactionState::ACTIVE) {
            LOG_ERROR(QString("Invalid or inactive transaction: TxnID=%1").arg(txnId));
            return false;
        }

        auto txn = txnIt.value();

        // 检查是否已经持有锁
        auto lockIt = pageLocks_.find(pageId);

        if (lockIt == pageLocks_.end()) {
            // 没有锁存在，直接授予
            PageLock newLock(pageId);
            newLock.lockType = lockType;
            newLock.holders.insert(txnId);
            pageLocks_[pageId] = newLock;
            txn->lockedPages.insert(pageId);

            LOG_DEBUG(QString("Lock granted: TxnID=%1, PageID=%2, LockType=%3")
                        .arg(txnId)
                        .arg(pageId)
                        .arg(lockType == LockType::SHARED ? "SHARED" : "EXCLUSIVE"));
            return true;
        }

        PageLock& existingLock = lockIt.value();

        // 检查是否已经持有该锁
        if (existingLock.holders.contains(txnId)) {
            // 锁升级处理
            if (existingLock.lockType == LockType::SHARED && lockType == LockType::EXCLUSIVE) {
                // 尝试升级为排他锁
                if (existingLock.holders.size() == 1) {
                    existingLock.lockType = LockType::EXCLUSIVE;
                    LOG_DEBUG(QString("Lock upgraded: TxnID=%1, PageID=%2").arg(txnId).arg(pageId));
                    return true;
                }
                // 其他事务持有共享锁，无法升级，需要等待
            } else {
                // 已经持有兼容的锁
                return true;
            }
        }

        // 检查锁兼容性
        if (isLockCompatible(existingLock, lockType)) {
            // 兼容，添加到持有者列表
            existingLock.holders.insert(txnId);
            txn->lockedPages.insert(pageId);

            LOG_DEBUG(QString("Lock granted (shared): TxnID=%1, PageID=%2").arg(txnId).arg(pageId));
            return true;
        }

        // 锁不兼容，需要等待
        locker.unlock();

        // 检查超时
        auto currentTime = QDateTime::currentMSecsSinceEpoch();
        if (timeoutMs > 0 && (currentTime - startTime) >= timeoutMs) {
            LOG_WARN(QString("Lock timeout: TxnID=%1, PageID=%2, waited %3ms")
                        .arg(txnId)
                        .arg(pageId)
                        .arg(currentTime - startTime));
            return false;
        }

        // 短暂休眠后重试
        QThread::msleep(10);
    }
}

bool TransactionManager::unlockPage(TransactionId txnId, PageId pageId) {
    QMutexLocker locker(&mutex_);

    // 检查事务是否存在
    auto txnIt = transactions_.find(txnId);
    if (txnIt == transactions_.end()) {
        LOG_ERROR(QString("Transaction not found: TxnID=%1").arg(txnId));
        return false;
    }

    auto txn = txnIt.value();

    // 检查锁是否存在
    auto lockIt = pageLocks_.find(pageId);
    if (lockIt == pageLocks_.end()) {
        LOG_WARN(QString("Lock not found: PageID=%1").arg(pageId));
        return false;
    }

    PageLock& lock = lockIt.value();

    // 移除该事务的持有
    if (!lock.holders.remove(txnId)) {
        LOG_WARN(QString("Transaction does not hold lock: TxnID=%1, PageID=%2").arg(txnId).arg(pageId));
        return false;
    }

    txn->lockedPages.remove(pageId);

    // 如果没有持有者了，删除锁
    if (lock.holders.isEmpty()) {
        pageLocks_.erase(lockIt);
        LOG_DEBUG(QString("Lock released and removed: TxnID=%1, PageID=%2").arg(txnId).arg(pageId));
    } else {
        LOG_DEBUG(QString("Lock released: TxnID=%1, PageID=%2, remaining holders=%3")
                    .arg(txnId)
                    .arg(pageId)
                    .arg(lock.holders.size()));
    }

    return true;
}

void TransactionManager::releaseAllLocks(TransactionId txnId) {
    // 假设调用者已经持有 mutex_

    auto txnIt = transactions_.find(txnId);
    if (txnIt == transactions_.end()) {
        return;
    }

    auto txn = txnIt.value();
    QSet<PageId> lockedPages = txn->lockedPages; // 复制一份，因为 unlockPage 会修改它

    for (PageId pageId : lockedPages) {
        auto lockIt = pageLocks_.find(pageId);
        if (lockIt != pageLocks_.end()) {
            PageLock& lock = lockIt.value();
            lock.holders.remove(txnId);

            if (lock.holders.isEmpty()) {
                pageLocks_.erase(lockIt);
            }
        }
    }

    txn->lockedPages.clear();

    LOG_DEBUG(QString("Released all locks for transaction: TxnID=%1, count=%2")
                .arg(txnId)
                .arg(lockedPages.size()));
}

int TransactionManager::getActiveTransactionCount() const {
    QMutexLocker locker(&mutex_);

    int count = 0;
    for (auto it = transactions_.begin(); it != transactions_.end(); ++it) {
        if (it.value()->state == TransactionState::ACTIVE) {
            count++;
        }
    }

    return count;
}

void TransactionManager::addUndoRecord(TransactionId txnId, const UndoRecord& undoRecord) {
    QMutexLocker locker(&mutex_);

    auto it = transactions_.find(txnId);
    if (it == transactions_.end()) {
        LOG_ERROR(QString("Transaction not found: TxnID=%1").arg(txnId));
        return;
    }

    auto txn = it.value();
    if (txn->state != TransactionState::ACTIVE) {
        LOG_WARN(QString("Cannot add undo record to non-active transaction: TxnID=%1").arg(txnId));
        return;
    }

    // 添加 Undo 记录到事务的 Undo 日志
    txn->undoLog.append(undoRecord);

    LOG_DEBUG(QString("Added undo record to transaction %1 (type=%2, table=%3)")
                 .arg(txnId)
                 .arg(static_cast<int>(undoRecord.opType))
                 .arg(undoRecord.tableName));
}

} // namespace qindb
