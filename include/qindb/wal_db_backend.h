#ifndef QINDB_WAL_DB_BACKEND_H
#define QINDB_WAL_DB_BACKEND_H

#include "common.h"
#include "wal.h"
#include <QString>
#include <QVector>
#include <memory>

namespace qindb {

// 前向声明
class BufferPoolManager;
class DiskManager;

/**
 * @brief WAL数据库存储后端
 *
 * 负责将WAL日志记录存储到数据库的系统表中，
 * 而不是使用外部的wal文件。
 *
 * 使用两个系统表：
 * - sys_wal_logs: 存储WAL日志记录
 * - sys_wal_meta: 存储WAL元数据（如当前LSN）
 */
class WalDbBackend {
public:
    /**
     * @brief 构造函数
     * @param bufferPool 缓冲池管理器
     * @param diskManager 磁盘管理器
     */
    WalDbBackend(BufferPoolManager* bufferPool, DiskManager* diskManager);

    ~WalDbBackend();

    /**
     * @brief 初始化系统表（如果不存在则创建）
     * @return 是否成功
     */
    bool initialize();

    /**
     * @brief 写入WAL记录
     * @param record WAL记录
     * @return 是否成功
     */
    bool writeRecord(const WALRecord& record);

    /**
     * @brief 读取所有WAL记录
     * @param records 输出：WAL记录列表
     * @return 是否成功
     */
    bool readAllRecords(QVector<WALRecord>& records);

    /**
     * @brief 获取当前LSN
     * @return 当前LSN
     */
    uint64_t getCurrentLSN();

    /**
     * @brief 设置当前LSN
     * @param lsn LSN值
     * @return 是否成功
     */
    bool setCurrentLSN(uint64_t lsn);

    /**
     * @brief 刷新（对于数据库后端，这会调用缓冲池的刷新）
     * @return 是否成功
     */
    bool flush();

    /**
     * @brief 清空WAL日志（用于检查点后）
     * @return 是否成功
     */
    bool truncate();

    /**
     * @brief 检查系统表是否存在
     * @return 是否存在
     */
    bool systemTablesExist();

private:
    BufferPoolManager* bufferPool_;
    DiskManager* diskManager_;

    // 系统表的首页ID
    PageId sysWalLogsFirstPage_;
    PageId sysWalMetaFirstPage_;

    /**
     * @brief 创建系统表
     */
    bool createSystemTables();

    /**
     * @brief 获取元数据值
     */
    bool getMetaValue(const QString& key, uint64_t& value);

    /**
     * @brief 设置元数据值
     */
    bool setMetaValue(const QString& key, uint64_t value);

    /**
     * @brief 清空WAL日志表
     */
    bool clearWalLogs();
};

} // namespace qindb

#endif // QINDB_WAL_DB_BACKEND_H
