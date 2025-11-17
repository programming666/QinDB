#ifndef TABLE_CACHE_H  // 防止重复包含的头文件宏定义
#define TABLE_CACHE_H

#include <QHash>      // Qt的哈希表容器
#include <QVector>    // Qt的动态数组容器
#include <QVariant>   // Qt的泛型数据类型
#include <QString>    // Qt的字符串类
#include <QMutex>     // Qt的互斥锁
#include <QDateTime>  // Qt的日期时间类
#include "qindb/table_page.h"  // 自定义表页相关定义

namespace qindb {  // 定义qindb命名空间

class TableDef;        // 表定义类的前向声明
class BufferPoolManager; // 缓冲池管理器的前向声明

/**
 * @brief 表级内存缓存条目
 *
 * 存储小表的所有行数据在内存中，避免频繁的磁盘I/O
 */
struct TableCacheEntry {  // 表缓存条目结构体
    QVector<QVector<QVariant>> rows;        // 所有行数据
    QVector<RecordHeader> headers;          // 所有行的记录头（MVCC信息）
    QDateTime loadedAt;                     // 加载时间
    uint64_t memorySizeBytes;               // 估算的内存占用（字节）
    uint32_t rowCount;                      // 行数
    bool isValid;                           // 缓存是否有效

    TableCacheEntry()  // 构造函数初始化
        : memorySizeBytes(0), rowCount(0), isValid(true) {
        loadedAt = QDateTime::currentDateTime();  // 设置加载时间为当前时间
    }
};

/**
 * @brief 表级内存缓存管理器
 *
 * 用于缓存小表（<5MB）的所有数据到内存中
 * 提供快速的全表扫描能力，避免磁盘I/O
 */
class TableCache {  // 表缓存管理器类
public:
    /**
     * @brief 构造函数
     * @param maxTableSizeBytes 可缓存的最大表大小（默认5MB）
     * @param maxTotalMemoryBytes 缓存的最大总内存占用（默认100MB）
     */
    explicit TableCache(uint64_t maxTableSizeBytes = 5 * 1024 * 1024,  // 默认最大表大小5MB
                       uint64_t maxTotalMemoryBytes = 100 * 1024 * 1024);  // 默认最大总内存100MB

    ~TableCache();  // 析构函数

    /**
     * @brief 检查表是否已缓存在内存中
     * @param dbName 数据库名
     * @param tableName 表名
     * @return true 如果表已缓存且有效
     */
    bool isTableCached(const QString& dbName, const QString& tableName) const;

    /**
     * @brief 获取缓存的表数据
     * @param dbName 数据库名
     * @param tableName 表名
     * @param rows 输出：行数据
     * @param headers 输出：记录头（MVCC信息）
     * @return true 如果成功获取缓存数据
     */
    bool getTableData(const QString& dbName,
                      const QString& tableName,
                      QVector<QVector<QVariant>>& rows,  // 输出行数据
                      QVector<RecordHeader>& headers);  // 输出记录头

    /**
     * @brief 加载表到内存缓存
     * @param dbName 数据库名
     * @param table 表元数据
     * @param bufferPool 缓冲池管理器
     * @return true 如果成功加载
     */
    bool loadTable(const QString& dbName,
                   TableDef* table,  // 表定义指针
                   BufferPoolManager* bufferPool);  // 缓冲池管理器指针

    /**
     * @brief 使表缓存失效（在INSERT/UPDATE/DELETE时调用）
     * @param dbName 数据库名
     * @param tableName 表名
     */
    void invalidateTable(const QString& dbName, const QString& tableName);

    /**
     * @brief 使整个数据库的所有表缓存失效
     * @param dbName 数据库名
     */
    void invalidateDatabase(const QString& dbName);

    /**
     * @brief 清空所有缓存
     */
    void clear();

    /**
     * @brief 获取表在磁盘上的大小估算
     * @param table 表元数据
     * @param bufferPool 缓冲池管理器
     * @return 表的估算大小（字节）
     */
    static uint64_t estimateTableSize(TableDef* table, BufferPoolManager* bufferPool);

    /**
     * @brief 设置最大可缓存的单表大小
     * @param bytes 字节数
     */
    void setMaxTableSize(uint64_t bytes);

    /**
     * @brief 设置最大总内存占用
     * @param bytes 字节数
     */
    void setMaxTotalMemory(uint64_t bytes);

    /**
     * @brief 获取缓存统计信息
     */
    struct Statistics {  // 缓存统计信息结构体
        uint64_t totalCachedTables;     // 已缓存的表数量
        uint64_t totalMemoryBytes;      // 总内存占用
        uint64_t cacheHits;             // 缓存命中次数
        uint64_t cacheMisses;           // 缓存未命中次数
        uint64_t invalidations;         // 缓存失效次数
        double hitRate;                 // 命中率
    };

    Statistics getStatistics() const;  // 获取缓存统计信息

    /**
     * @brief 启用或禁用表缓存
     * @param enabled true 启用，false 禁用
     */
    void setEnabled(bool enabled);

    bool isEnabled() const { return enabled_; }  // 获取缓存是否启用

private:
    // 内部辅助函数
    QString makeKey(const QString& dbName, const QString& tableName) const;  // 生成缓存键
    uint64_t estimateMemorySize(const QVector<QVector<QVariant>>& rows) const;  // 估算内存大小
    void evictLRU();  // 驱逐最少最近使用的表

    // 缓存存储
    QHash<QString, TableCacheEntry> cache_;  // key = "dbName.tableName"

    // 配置
    uint64_t maxTableSizeBytes_;      // 可缓存的最大单表大小
    uint64_t maxTotalMemoryBytes_;    // 最大总内存占用
    bool enabled_;                    // 是否启用

    // 统计
    mutable uint64_t cacheHits_;      // 缓存命中次数
    mutable uint64_t cacheMisses_;    // 缓存未命中次数
    uint64_t invalidations_;         // 缓存失效次数
    uint64_t totalMemoryBytes_;      // 总内存占用

    // 线程安全
    mutable QMutex mutex_;  // 互斥锁
};

} // namespace qindb

#endif // TABLE_CACHE_H  // 结束头文件宏定义
