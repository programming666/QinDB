#ifndef QINDB_VISIBILITY_CHECKER_H
#define QINDB_VISIBILITY_CHECKER_H

#include "common.h"
#include "transaction.h"
#include "table_page.h"

namespace qindb {

/**
 * @brief MVCC可见性检查器
 *
 * 根据当前事务ID和元组头部信息，判断元组对当前事务是否可见
 *
 * 可见性规则（基于快照隔离）：
 * 1. 如果 xmin 未提交（且不是当前事务），不可见
 * 2. 如果 xmin 已中止，不可见
 * 3. 如果 xmax == 0（未删除），可见
 * 4. 如果 xmax 已提交（且不是当前事务），不可见
 * 5. 如果 xmax 未提交或已中止，可见
 * 6. 如果 xmax 是当前事务，不可见（自己删除的）
 */
class VisibilityChecker {
public:
    /**
     * @brief 构造函数
     * @param txnMgr 事务管理器指针
     */
    explicit VisibilityChecker(TransactionManager* txnMgr);

    /**
     * @brief 检查元组对当前事务是否可见
     *
     * @param header 元组头部（包含 createTxnId 和 deleteTxnId）
     * @param currentTxnId 当前事务ID
     * @return 是否可见
     */
    bool isVisible(const RecordHeader& header, TransactionId currentTxnId);

private:
    TransactionManager* txnMgr_;

    /**
     * @brief 检查事务是否已提交
     * @param txnId 事务ID
     * @return 是否已提交
     */
    bool isCommitted(TransactionId txnId);

    /**
     * @brief 检查事务是否已中止
     * @param txnId 事务ID
     * @return 是否已中止
     */
    bool isAborted(TransactionId txnId);

    /**
     * @brief 检查事务是否正在运行
     * @param txnId 事务ID
     * @return 是否正在运行
     */
    bool isRunning(TransactionId txnId);
};

} // namespace qindb

#endif // QINDB_VISIBILITY_CHECKER_H
