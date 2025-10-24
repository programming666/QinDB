#include "qindb/visibility_checker.h"
#include "qindb/logger.h"

namespace qindb {

VisibilityChecker::VisibilityChecker(TransactionManager* txnMgr)
    : txnMgr_(txnMgr)
{
}

bool VisibilityChecker::isVisible(const RecordHeader& header, TransactionId currentTxnId) {
    TransactionId xmin = header.createTxnId;
    TransactionId xmax = header.deleteTxnId;

    // 规则 1: 如果 xmin 未提交（且不是当前事务），不可见
    if (!isCommitted(xmin) && xmin != currentTxnId) {
        return false;
    }

    // 规则 2: 如果 xmin 已中止，不可见
    if (isAborted(xmin)) {
        return false;
    }

    // 规则 3: 如果 xmax == INVALID_TXN_ID（未删除），可见
    if (xmax == INVALID_TXN_ID) {
        return true;
    }

    // 规则 6: 如果 xmax 是当前事务，不可见（自己删除的）
    // 这条规则必须在 Rule 5 之前检查！
    if (xmax == currentTxnId) {
        return false;
    }

    // 规则 4: 如果 xmax 已提交（且不是当前事务），不可见
    if (isCommitted(xmax) && xmax != currentTxnId) {
        return false;
    }

    // 规则 5: 如果 xmax 未提交或已中止，可见
    if (!isCommitted(xmax) || isAborted(xmax)) {
        return true;
    }

    // 默认可见
    return true;
}

bool VisibilityChecker::isCommitted(TransactionId txnId) {
    if (txnId == INVALID_TXN_ID) {
        return false;
    }

    TransactionState state = txnMgr_->getTransactionState(txnId);

    // 如果事务不在事务表中（INVALID状态），说明已经完成并被清理
    // 根据MVCC语义，这种情况视为已提交
    if (state == TransactionState::INVALID) {
        return true;  // 已完成的事务默认视为已提交
    }

    return (state == TransactionState::COMMITTED);
}

bool VisibilityChecker::isAborted(TransactionId txnId) {
    if (txnId == INVALID_TXN_ID) {
        return false;
    }

    TransactionState state = txnMgr_->getTransactionState(txnId);
    return (state == TransactionState::ABORTED);
}

bool VisibilityChecker::isRunning(TransactionId txnId) {
    if (txnId == INVALID_TXN_ID) {
        return false;
    }

    TransactionState state = txnMgr_->getTransactionState(txnId);
    return (state == TransactionState::ACTIVE);
}

} // namespace qindb
