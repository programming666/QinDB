#ifndef QINDB_DATABASE_MANAGER_H
#define QINDB_DATABASE_MANAGER_H

#include "common.h"
#include "catalog.h"
#include "buffer_pool_manager.h"
#include "disk_manager.h"
#include "wal.h"
#include "transaction.h"
#include "permission_manager.h"
#include <QString>
#include <QMutex>
#include <QDir>
#include <memory>
#include <map>

namespace qindb {

/**
 * @brief 数据库定义
 */
struct DatabaseDef {
    QString name;                                   // 数据库名
    QString path;                                   // 数据库目录路径
    std::unique_ptr<DiskManager> diskManager;       // 磁盘管理器 (必须在 bufferPool 之前声明，这样会在 bufferPool 之后析构)
    std::unique_ptr<Catalog> catalog;               // 元数据管理器
    std::unique_ptr<BufferPoolManager> bufferPool;  // 缓冲池
    std::unique_ptr<WALManager> walManager;         // WAL管理器
    std::unique_ptr<TransactionManager> transactionManager;  // 事务管理器
    std::unique_ptr<PermissionManager> permissionManager;    // 权限管理器

    DatabaseDef(const QString& dbName, const QString& dbPath)
        : name(dbName), path(dbPath) {}
};

/**
 * @brief 数据库管理器 - 管理多个数据库
 *
 * 职责：
 * - 创建/删除数据库
 * - 切换当前数据库
 * - 管理所有数据库的生命周期
 * - 持久化数据库元信息
 */
class DatabaseManager {
public:
    explicit DatabaseManager(const QString& dataDir = "./data");
    ~DatabaseManager();

    /**
     * @brief 创建数据库
     * @param dbName 数据库名
     * @param ifNotExists 是否检查已存在
     * @return 是否成功
     */
    bool createDatabase(const QString& dbName, bool ifNotExists = false);

    /**
     * @brief 删除数据库
     * @param dbName 数据库名
     * @param ifExists 是否检查存在性
     * @return 是否成功
     */
    bool dropDatabase(const QString& dbName, bool ifExists = false);

    /**
     * @brief 切换当前数据库
     * @param dbName 数据库名
     * @return 是否成功
     */
    bool useDatabase(const QString& dbName);

    /**
     * @brief 获取所有数据库名称
     */
    QVector<QString> getAllDatabaseNames() const;

    /**
     * @brief 数据库是否存在
     */
    bool databaseExists(const QString& dbName) const;

    /**
     * @brief 获取当前数据库名
     */
    QString currentDatabaseName() const;

    /**
     * @brief 获取当前数据库的Catalog
     */
    Catalog* getCurrentCatalog() const;

    /**
     * @brief 获取当前数据库的缓冲池
     */
    BufferPoolManager* getCurrentBufferPool() const;

    /**
     * @brief 获取当前数据库的磁盘管理器
     */
    DiskManager* getCurrentDiskManager() const;

    /**
     * @brief 获取当前数据库的WAL管理器
     */
    WALManager* getCurrentWALManager() const;

    /**
     * @brief ?????????????
     */
    PermissionManager* getCurrentPermissionManager() const;

    /**
     * @brief 获取当前数据库的事务管理器
     */
    TransactionManager* getCurrentTransactionManager() const;

    /**
     * @brief 获取当前会话的事务ID
     * @return 当前事务ID，如果没有活跃事务返回 INVALID_TXN_ID
     */
    TransactionId getCurrentTransactionId() const;

    /**
     * @brief 设置当前会话的事务ID
     * @param txnId 事务ID
     */
    void setCurrentTransactionId(TransactionId txnId);

    /**
     * @brief 获取指定数据库的目录路径
     */
    QString getDatabasePath(const QString& dbName) const;

    /**
     * @brief 保存数据库管理器元信息到磁盘
     */
    bool saveToDisk();

    /**
     * @brief 从磁盘加载数据库管理器元信息
     */
    bool loadFromDisk();

    /**
     * @brief 获取最后的错误信息
     */
    const Error& lastError() const { return m_error; }

private:
    /**
     * @brief 初始化数据库（创建目录结构和组件）
     */
    bool initializeDatabase(const QString& dbName);

    /**
     * @brief 加载数据库（从磁盘加载已存在的数据库）
     */
    bool loadDatabase(const QString& dbName);

    /**
     * @brief 关闭数据库（释放资源）
     */
    void closeDatabase(const QString& dbName);

    QString m_dataDir;                                      // 数据根目录
    QString m_currentDatabase;                              // 当前数据库名
    TransactionId m_currentTransactionId;                   // 当前会话事务ID
    std::map<QString, std::unique_ptr<DatabaseDef>> m_databases;  // 数据库名 -> 数据库定义
    mutable QMutex m_mutex;                                 // 线程安全
    Error m_error;                                          // 错误信息
};

} // namespace qindb

#endif // QINDB_DATABASE_MANAGER_H
