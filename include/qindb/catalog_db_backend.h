#ifndef QINDB_CATALOG_DB_BACKEND_H
#define QINDB_CATALOG_DB_BACKEND_H

#include "common.h"
#include "catalog.h"
#include <QString>
#include <memory>

namespace qindb {

// 前向声明
class BufferPoolManager;  // 缓冲池管理器的前向声明
class DiskManager;       // 磁盘管理器的前向声明

/**
 * @brief Catalog数据库存储后端类
 *
 * 负责将Catalog元数据存储到数据库的系统表中，
 * 而不是使用外部的catalog.json文件。
 *
 * 使用三个系统表：
 * - sys_tables: 存储表定义
 * - sys_columns: 存储列定义
 * - sys_indexes: 存储索引定义
 */
class CatalogDbBackend {
public:
    /**
     * @brief 构造函数
     * @param bufferPool 缓冲池管理器
     * @param diskManager 磁盘管理器
     */
    CatalogDbBackend(BufferPoolManager* bufferPool, DiskManager* diskManager);

    ~CatalogDbBackend();

    /**
     * @brief 初始化系统表（如果不存在则创建）
     * @return 是否成功
     */
    bool initialize();

    /**
     * @brief 保存catalog到数据库
     * @param tables 表定义映射
     * @param indexes 索引定义映射
     * @return 是否成功
     */
    bool saveCatalog(
        const QHash<QString, std::shared_ptr<TableDef>>& tables,
        const QHash<QString, IndexDef>& indexes
    );

    /**
     * @brief 从数据库加载catalog
     * @param tables 输出：表定义映射
     * @param indexes 输出：索引定义映射
     * @return 是否成功
     */
    bool loadCatalog(
        QHash<QString, std::shared_ptr<TableDef>>& tables,
        QHash<QString, IndexDef>& indexes
    );

    /**
     * @brief 检查系统表是否存在
     * @return 是否存在
     */
    bool systemTablesExist();

private:
    BufferPoolManager* bufferPool_;
    DiskManager* diskManager_;

    // 系统表的首页ID
    PageId sysTablesFirstPage_;
    PageId sysColumnsFirstPage_;
    PageId sysIndexesFirstPage_;

    /**
     * @brief 创建系统表
     */
    bool createSystemTables();

    /**
     * @brief 保存表定义
     */
    bool saveTableDef(const TableDef& table);

    /**
     * @brief 保存列定义
     */
    bool saveColumnDef(const QString& tableName, const ColumnDef& column, int order);

    /**
     * @brief 保存索引定义
     */
    bool saveIndexDef(const IndexDef& index);

    /**
     * @brief 加载所有表定义
     */
    bool loadTableDefs(QHash<QString, std::shared_ptr<TableDef>>& tables);

    /**
     * @brief 加载所有列定义
     */
    bool loadColumnDefs(QHash<QString, std::shared_ptr<TableDef>>& tables);

    /**
     * @brief 加载所有索引定义
     */
    bool loadIndexDefs(
        QHash<QString, std::shared_ptr<TableDef>>& tables,
        QHash<QString, IndexDef>& indexes
    );

    /**
     * @brief 清空系统表
     */
    bool clearSystemTables();
};

} // namespace qindb

#endif // QINDB_CATALOG_DB_BACKEND_H
