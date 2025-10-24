#ifndef QINDB_CATALOG_H
#define QINDB_CATALOG_H

#include "common.h"
#include "row_id_index.h"
#include <QString>
#include <QVector>
#include <QHash>
#include <QMutex>
#include <memory>

namespace qindb {

// 前向声明
class CatalogDbBackend;
class BufferPoolManager;
class DiskManager;

/**
 * @brief 列定义
 */
struct ColumnDef {
    QString name;           // 列名
    DataType type;          // 数据类型
    int length;             // 长度（对于VARCHAR等）
    bool notNull;           // 是否NOT NULL
    bool primaryKey;        // 是否主键
    bool autoIncrement;     // 是否自增
    QVariant defaultValue;  // 默认值

    ColumnDef()
        : type(DataType::NULL_TYPE)
        , length(0)
        , notNull(false)
        , primaryKey(false)
        , autoIncrement(false)
    {}

    ColumnDef(const QString& n, DataType t, int len = 0)
        : name(n)
        , type(t)
        , length(len)
        , notNull(false)
        , primaryKey(false)
        , autoIncrement(false)
    {}
};

/**
 * @brief 索引定义
 */
struct IndexDef {
    QString name;               // 索引名
    QString tableName;          // 表名
    QVector<QString> columns;   // 索引列
    IndexType indexType;        // 索引类型（B+树/哈希/TRIE/倒排/R树）
    DataType keyType;           // 键的数据类型（用于通用B+树）
    bool unique;                // 是否唯一索引
    bool autoCreated;           // 是否自动创建（true=系统自动，false=用户手动）
    PageId rootPageId;          // 索引根页ID
    QHash<QString, QString> options; // 索引选项（如TRIE的字符集、全文索引的分词器）

    IndexDef()
        : indexType(IndexType::BTREE)
        , keyType(DataType::NULL_TYPE)
        , unique(false)
        , autoCreated(false)
        , rootPageId(INVALID_PAGE_ID)
    {}
};

/**
 * @brief 表定义
 */
struct TableDef {
    QString name;                           // 表名
    QVector<ColumnDef> columns;             // 列定义
    PageId firstPageId;                     // 第一个数据页ID
    RowId nextRowId;                        // 下一个行ID（自增）
    QVector<IndexDef> indexes;              // 索引列表
    std::shared_ptr<RowIdIndex> rowIdIndex; // rowId 到位置的映射（使用指针以支持拷贝）

    TableDef()
        : firstPageId(INVALID_PAGE_ID)
        , nextRowId(1)
        , rowIdIndex(std::make_shared<RowIdIndex>())
    {}

    TableDef(const QString& n)
        : name(n)
        , firstPageId(INVALID_PAGE_ID)
        , nextRowId(1)
        , rowIdIndex(std::make_shared<RowIdIndex>())
    {}

    /**
     * @brief 根据列名查找列
     */
    const ColumnDef* findColumn(const QString& columnName) const {
        for (const auto& col : columns) {
            if (col.name.toLower() == columnName.toLower()) {
                return &col;
            }
        }
        return nullptr;
    }

    /**
     * @brief 获取列索引
     */
    int getColumnIndex(const QString& columnName) const {
        for (int i = 0; i < columns.size(); ++i) {
            if (columns[i].name.toLower() == columnName.toLower()) {
                return i;
            }
        }
        return -1;
    }

    /**
     * @brief 获取主键列索引
     */
    int getPrimaryKeyIndex() const {
        for (int i = 0; i < columns.size(); ++i) {
            if (columns[i].primaryKey) {
                return i;
            }
        }
        return -1;
    }
};

/**
 * @brief Catalog - 数据库目录/元数据管理器
 *
 * 职责：
 * - 管理所有表的元数据
 * - 管理索引信息
 * - 持久化元数据到磁盘
 * - 提供表和索引的查询接口
 */
class Catalog {
public:
    Catalog();
    ~Catalog();

    /**
     * @brief 设置数据库后端（用于存储catalog到数据库内部）
     * @param bufferPool 缓冲池管理器
     * @param diskManager 磁盘管理器
     */
    void setDatabaseBackend(BufferPoolManager* bufferPool, DiskManager* diskManager);

    /**
     * @brief 创建表
     */
    bool createTable(const TableDef& tableDef);

    /**
     * @brief 删除表
     */
    bool dropTable(const QString& tableName);

    /**
     * @brief 获取表定义
     */
    const TableDef* getTable(const QString& tableName) const;

    /**
     * @brief 表是否存在
     */
    bool tableExists(const QString& tableName) const;

    /**
     * @brief 获取所有表名
     */
    QVector<QString> getAllTableNames() const;

    /**
     * @brief 创建索引
     */
    bool createIndex(const IndexDef& indexDef);

    /**
     * @brief 删除索引
     */
    bool dropIndex(const QString& indexName);

    /**
     * @brief 获取索引定义
     */
    const IndexDef* getIndex(const QString& indexName) const;

    /**
     * @brief 获取表的所有索引
     */
    QVector<IndexDef> getTableIndexes(const QString& tableName) const;

    /**
     * @brief 保存元数据（自动选择文件或数据库模式）
     * @param filePath 文件路径（仅在文件模式下使用）
     * @return 是否成功
     */
    bool save(const QString& filePath = "");

    /**
     * @brief 加载元数据（自动选择文件或数据库模式）
     * @param filePath 文件路径（仅在文件模式下使用）
     * @return 是否成功
     */
    bool load(const QString& filePath = "");

    /**
     * @brief 保存元数据到文件（强制使用文件模式）
     */
    bool saveToDisk(const QString& filePath);

    /**
     * @brief 从文件加载元数据（强制使用文件模式）
     */
    bool loadFromDisk(const QString& filePath);

    /**
     * @brief 保存元数据到数据库（强制使用数据库模式）
     */
    bool saveToDatabase();

    /**
     * @brief 从数据库加载元数据（强制使用数据库模式）
     */
    bool loadFromDatabase();

    /**
     * @brief 更新表定义
     */
    bool updateTable(const QString& tableName, const TableDef& newDef);

private:
    QHash<QString, std::shared_ptr<TableDef>> tables_;  // 表名 -> 表定义
    QHash<QString, IndexDef> indexes_;                  // 索引名 -> 索引定义
    mutable QMutex mutex_;                              // 线程安全

    std::unique_ptr<CatalogDbBackend> dbBackend_;       // 数据库存储后端
    bool useDatabase_;                                  // 是否使用数据库存储（false=文件存储）
};

} // namespace qindb

#endif // QINDB_CATALOG_H
