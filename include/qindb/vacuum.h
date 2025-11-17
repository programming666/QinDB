#ifndef QINDB_VACUUM_H  // 防止头文件重复包含
#define QINDB_VACUUM_H

#include "common.h"      // 包含公共定义和类型
#include "transaction.h" // 包含事务相关定义
#include "buffer_pool_manager.h" // 包含缓冲池管理器定义
#include "catalog.h"     // 包含目录表相关定义
#include "table_page.h"  // 包含表页相关定义
#include <QThread>       // Qt线程支持
#include <QMutex>
#include <QWaitCondition> // Qt等待条件支持

namespace qindb {  // 定义qindb命名空间

/**
 * @brief VACUUM 垃圾回收器
 *
 * 负责清理被逻辑删除的记录（deleteTxnId != INVALID_TXN_ID 且已提交）
 * 支持手动触发和后台自动清理
 */
class VacuumWorker : public QObject {  // 继承自QObject以支持Qt信号槽机制
public:
    /**
     * @brief 构造函数
     * @param txnMgr 事务管理器指针
     * @param bufferPool 缓冲池管理器指针
     */
    explicit VacuumWorker(TransactionManager* txnMgr,
                         BufferPoolManager* bufferPool);

    ~VacuumWorker();  // 析构函数

    /**
     * @brief 清理指定表的旧版本
     * @param tableDef 表定义
     * @return 清理的记录数
     */
    int cleanupTable(const TableDef* tableDef);

    /**
     * @brief 启动后台线程定期清理
     * @param intervalSeconds 清理间隔（秒）
     */
    void startBackgroundWorker(int intervalSeconds = 60);

    /**
     * @brief 停止后台线程
     */
    void stopBackgroundWorker();

    /**
     * @brief 检查后台线程是否正在运行
     */
    bool isRunning() const { return running_; }  // 内联函数实现

private:
    /**
     * @brief 判断元组是否可以安全删除
     * @param header 记录头部
     * @return 是否可以删除
     *
     * 删除条件：
     * 1. 记录已被标记为删除（deleteTxnId != INVALID_TXN_ID）
     * 2. 删除事务已提交
     * 3. 没有活跃事务可能看到该记录
     */
    bool canDelete(const RecordHeader& header);

    /**
     * @brief 后台清理工作线程函数
     */
    void backgroundWork();

    TransactionManager* txnMgr_;      // 事务管理器指针成员
    BufferPoolManager* bufferPool_;

    // 后台线程控制相关成员
    QThread* workerThread_;
    bool running_;
    int intervalSeconds_;      // 清理间隔时间（秒）
    QMutex mutex_;            // 互斥锁，用于线程同步
    QWaitCondition condition_; // 等待条件，用于线程间通信
};

} // namespace qindb

#endif // QINDB_VACUUM_H  // 结束头文件包含保护
