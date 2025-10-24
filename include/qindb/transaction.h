#ifndef QINDB_TRANSACTION_H
#define QINDB_TRANSACTION_H

#include "common.h"
#include "wal.h"
#include "undo_log.h"
#include <QMutex>
#include <QHash>
#include <QSet>
#include <QDateTime>
#include <memory>

namespace qindb {

/**
 * @brief 事务状态
 */
enum class TransactionState {
    INVALID = 0,
    ACTIVE,      // 活跃中
    COMMITTED,   // 已提交
    ABORTED      // 已回滚
};

/**
 * @brief 锁类型
 */
enum class LockType {
    SHARED,      // 共享锁（读锁）
    EXCLUSIVE    // 排他锁（写锁）
};

/**
 * @brief 事务上下文
 */
struct Transaction {
    TransactionId txnId;                // 事务ID
    TransactionState state;             // 事务状态
    uint64_t startTime;                 // 开始时间（毫秒）
    QSet<PageId> lockedPages;          // 持有的页锁
    QVector<UndoRecord> undoLog;       // Undo 日志记录列表

    Transaction()
        : txnId(INVALID_TXN_ID)
        , state(TransactionState::INVALID)
        , startTime(0)
    {}

    explicit Transaction(TransactionId id)
        : txnId(id)
        , state(TransactionState::ACTIVE)
        , startTime(QDateTime::currentMSecsSinceEpoch())
    {}
};

/**
 * @brief 页级锁信息
 */
struct PageLock {
    PageId pageId;
    LockType lockType;
    QSet<TransactionId> holders;  // 持有该锁的事务ID集合

    PageLock() : pageId(INVALID_PAGE_ID), lockType(LockType::SHARED) {}
    explicit PageLock(PageId pid) : pageId(pid), lockType(LockType::SHARED) {}
};

/**
 * @brief 事务管理器
 *
 * 职责：
 * 1. 管理事务生命周期（开始、提交、回滚）
 * 2. 提供锁管理（页级锁）
 * 3. 与 WAL 集成保证持久性
 * 4. 检测死锁（简单的超时机制）
 */
class TransactionManager {
public:
    /**
     * @brief 构造函数
     * @param walManager WAL 管理器
     */
    explicit TransactionManager(WALManager* walManager);

    ~TransactionManager();

    /**
     * @brief 开始新事务
     * @return 事务ID
     */
    TransactionId beginTransaction();

    /**
     * @brief 提交事务
     * @param txnId 事务ID
     * @return 是否成功
     */
    bool commitTransaction(TransactionId txnId);

    /**
     * @brief 回滚事务
     * @param txnId 事务ID
     * @return 是否成功
     */
    bool abortTransaction(TransactionId txnId);

    /**
     * @brief 获取事务状态
     * @param txnId 事务ID
     * @return 事务状态
     */
    TransactionState getTransactionState(TransactionId txnId) const;

    /**
     * @brief 获取事务对象
     * @param txnId 事务ID
     * @return 事务对象指针，如果不存在返回 nullptr
     */
    Transaction* getTransaction(TransactionId txnId);

    /**
     * @brief 请求页锁
     * @param txnId 事务ID
     * @param pageId 页ID
     * @param lockType 锁类型
     * @param timeoutMs 超时时间（毫秒），0 表示无限等待
     * @return 是否成功获取锁
     */
    bool lockPage(TransactionId txnId, PageId pageId, LockType lockType, int timeoutMs = 5000);

    /**
     * @brief 释放页锁
     * @param txnId 事务ID
     * @param pageId 页ID
     * @return 是否成功
     */
    bool unlockPage(TransactionId txnId, PageId pageId);

    /**
     * @brief 检查锁是否兼容
     * @param existingLock 已存在的锁
     * @param requestedLock 请求的锁类型
     * @return 是否兼容
     */
    static bool isLockCompatible(const PageLock& existingLock, LockType requestedLock);

    /**
     * @brief 获取活跃事务数量
     */
    int getActiveTransactionCount() const;

    /**
     * @brief 添加 Undo 日志记录
     * @param txnId 事务ID
     * @param undoRecord Undo 记录
     */
    void addUndoRecord(TransactionId txnId, const UndoRecord& undoRecord);

private:
    /**
     * @brief 释放事务持有的所有锁
     * @param txnId 事务ID
     */
    void releaseAllLocks(TransactionId txnId);

    /**
     * @brief 生成新的事务ID
     */
    TransactionId generateTransactionId();

    WALManager* walManager_;                                    // WAL 管理器
    QHash<TransactionId, std::shared_ptr<Transaction>> transactions_; // 事务表
    QHash<PageId, PageLock> pageLocks_;                        // 页锁表
    TransactionId nextTxnId_;                                  // 下一个事务ID
    mutable QMutex mutex_;                                     // 互斥锁
};

} // namespace qindb

#endif // QINDB_TRANSACTION_H
