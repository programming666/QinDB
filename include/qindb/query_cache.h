#ifndef QINDB_QUERY_CACHE_H
#define QINDB_QUERY_CACHE_H

#include "qindb/common.h"
#include "qindb/query_result.h"
#include <QHash>
#include <QString>
#include <QMutex>
#include <QDateTime>
#include <QSet>
#include <QVariant>

namespace qindb {

/**
 * @brief 缓存条目结构
 *
 * 存储查询结果、创建时间、访问次数等元数据
 */
struct CacheEntry {
    QueryResult result;           // 查询结果
    QDateTime createdAt;          // 创建时间
    QDateTime lastAccessedAt;     // 最后访问时间
    uint64_t accessCount;         // 访问次数
    QSet<QString> affectedTables; // 查询涉及的表名列表（用于失效策略）
    uint64_t memorySizeBytes;     // 估算的内存占用（字节）

    CacheEntry()
        : createdAt(QDateTime::currentDateTime())
        , lastAccessedAt(QDateTime::currentDateTime())
        , accessCount(0)
        , memorySizeBytes(0) {}
};

/**
 * @brief 查询缓存管理器
 *
 * 功能：
 * 1. 缓存 SELECT 查询的结果
 * 2. 基于 SQL 文本生成缓存键
 * 3. 支持 LRU 驱逐策略
 * 4. 表修改时自动失效相关缓存
 * 5. 内存使用限制
 * 6. 线程安全
 *
 * 使用场景：
 * - 重复执行的只读查询（SELECT）
 * - 数据变化不频繁的表
 * - 提高查询响应速度
 *
 * 不适用场景：
 * - 实时数据查询（要求最新数据）
 * - 频繁更新的表
 * - 查询结果集非常大（超过缓存容量）
 */
class QueryCache {
public:
    /**
     * @brief 构造函数
     * @param maxEntries 最大缓存条目数（默认 1000）
     * @param maxMemoryMB 最大内存占用（MB，默认 100MB）
     * @param ttlSeconds 缓存条目生存时间（秒，默认 300 秒 = 5 分钟，0 表示永不过期）
     */
    explicit QueryCache(uint64_t maxEntries = 1000,
                       uint64_t maxMemoryMB = 100,
                       uint64_t ttlSeconds = 300);

    ~QueryCache();

    /**
     * @brief 查找缓存的查询结果
     * @param querySql SQL 查询文本（标准化后的）
     * @param result 输出参数，找到时返回缓存的结果
     * @return true 如果找到有效的缓存，false 否则
     */
    bool get(const QString& querySql, QueryResult& result);

    /**
     * @brief 存储查询结果到缓存
     * @param querySql SQL 查询文本（标准化后的）
     * @param result 查询结果
     * @param affectedTables 查询涉及的表名列表
     * @return true 如果成功缓存，false 如果被拒绝（例如结果太大）
     */
    bool put(const QString& querySql,
             const QueryResult& result,
             const QSet<QString>& affectedTables);

    /**
     * @brief 使某个表相关的所有缓存失效
     * @param tableName 表名
     * @return 失效的缓存条目数量
     *
     * 使用场景：INSERT, UPDATE, DELETE, DROP TABLE 后调用
     */
    int invalidateTable(const QString& tableName);

    /**
     * @brief 使某个数据库相关的所有缓存失效
     * @param databaseName 数据库名
     * @return 失效的缓存条目数量
     *
     * 使用场景：DROP DATABASE 后调用
     */
    int invalidateDatabase(const QString& databaseName);

    /**
     * @brief 清空所有缓存
     */
    void clear();

    /**
     * @brief 获取缓存统计信息
     */
    struct Statistics {
        uint64_t totalEntries;       // 当前缓存条目数
        uint64_t totalHits;          // 缓存命中次数
        uint64_t totalMisses;        // 缓存未命中次数
        uint64_t totalEvictions;     // 缓存驱逐次数
        uint64_t totalMemoryBytes;   // 总内存占用（字节）
        double hitRate;              // 命中率 (hits / (hits + misses))

        Statistics()
            : totalEntries(0), totalHits(0), totalMisses(0),
              totalEvictions(0), totalMemoryBytes(0), hitRate(0.0) {}
    };

    Statistics getStatistics() const;

    /**
     * @brief 启用/禁用查询缓存
     * @param enabled true 启用，false 禁用
     */
    void setEnabled(bool enabled);

    /**
     * @brief 查询缓存是否启用
     */
    bool isEnabled() const;

    /**
     * @brief 标准化 SQL 查询文本（去除多余空格、统一大小写）
     * @param sql 原始 SQL 文本
     * @return 标准化后的 SQL 文本（用作缓存键）
     */
    static QString normalizeQuery(const QString& sql);

private:
    /**
     * @brief 估算查询结果的内存占用
     * @param result 查询结果
     * @return 估算的字节数
     */
    uint64_t estimateMemorySize(const QueryResult& result) const;

    /**
     * @brief 检查缓存条目是否过期
     * @param entry 缓存条目
     * @return true 如果已过期，false 否则
     */
    bool isExpired(const CacheEntry& entry) const;

    /**
     * @brief 驱逐最少使用的缓存条目（LRU）
     * @return 被驱逐的条目数量
     */
    int evictLRU();

    /**
     * @brief 驱逐足够的条目以腾出指定的内存空间
     * @param requiredBytes 需要的字节数
     * @return 被驱逐的条目数量
     */
    int evictToFreeMemory(uint64_t requiredBytes);

    // 缓存存储：SQL 文本 -> 缓存条目
    QHash<QString, CacheEntry> cache_;

    // 表名 -> 查询SQL列表映射（用于快速失效）
    QHash<QString, QSet<QString>> tableToQueries_;

    // 配置参数
    uint64_t maxEntries_;         // 最大条目数
    uint64_t maxMemoryBytes_;     // 最大内存字节数
    uint64_t ttlSeconds_;         // 生存时间（秒）
    bool enabled_;                // 是否启用缓存

    // 统计信息
    mutable uint64_t totalHits_;
    mutable uint64_t totalMisses_;
    mutable uint64_t totalEvictions_;
    mutable uint64_t totalMemoryBytes_;

    // 线程安全锁
    mutable QMutex mutex_;
};

} // namespace qindb

#endif // QINDB_QUERY_CACHE_H
