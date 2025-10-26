#include "qindb/query_cache.h"
#include "qindb/logger.h"
#include <QMutexLocker>
#include <algorithm>

namespace qindb {

QueryCache::QueryCache(uint64_t maxEntries, uint64_t maxMemoryMB, uint64_t ttlSeconds)
    : maxEntries_(maxEntries)
    , maxMemoryBytes_(maxMemoryMB * 1024 * 1024)
    , ttlSeconds_(ttlSeconds)
    , enabled_(true)
    , totalHits_(0)
    , totalMisses_(0)
    , totalEvictions_(0)
    , totalMemoryBytes_(0)
{
    LOG_INFO(QString("QueryCache initialized: maxEntries=%1, maxMemory=%2MB, TTL=%3s")
                .arg(maxEntries_)
                .arg(maxMemoryMB)
                .arg(ttlSeconds_));
}

QueryCache::~QueryCache() {
    QMutexLocker locker(&mutex_);
    Statistics stats = getStatistics();
    LOG_INFO(QString("QueryCache destroyed: entries=%1, hits=%2, misses=%3, hitRate=%4%%")
                .arg(stats.totalEntries)
                .arg(stats.totalHits)
                .arg(stats.totalMisses)
                .arg(stats.hitRate * 100.0, 0, 'f', 2));
}

bool QueryCache::get(const QString& querySql, QueryResult& result) {
    QMutexLocker locker(&mutex_);

    if (!enabled_) {
        return false;
    }

    // 查找缓存
    auto it = cache_.find(querySql);
    if (it == cache_.end()) {
        totalMisses_++;
        LOG_DEBUG(QString("Cache MISS: %1").arg(querySql.left(50)));
        return false;
    }

    CacheEntry& entry = it.value();

    // 检查是否过期
    if (isExpired(entry)) {
        LOG_DEBUG(QString("Cache EXPIRED: %1").arg(querySql.left(50)));

        // 移除过期条目
        totalMemoryBytes_ -= entry.memorySizeBytes;

        // 从 tableToQueries_ 映射中删除
        for (const QString& table : entry.affectedTables) {
            if (tableToQueries_.contains(table)) {
                tableToQueries_[table].remove(querySql);
                if (tableToQueries_[table].isEmpty()) {
                    tableToQueries_.remove(table);
                }
            }
        }

        cache_.erase(it);
        totalMisses_++;
        return false;
    }

    // 缓存命中
    totalHits_++;
    entry.accessCount++;
    entry.lastAccessedAt = QDateTime::currentDateTime();

    result = entry.result;

    LOG_DEBUG(QString("Cache HIT: %1 (accessCount=%2)")
                 .arg(querySql.left(50))
                 .arg(entry.accessCount));

    return true;
}

bool QueryCache::put(const QString& querySql,
                     const QueryResult& result,
                     const QSet<QString>& affectedTables) {
    QMutexLocker locker(&mutex_);

    if (!enabled_) {
        return false;
    }

    // 只缓存成功的查询结果
    if (!result.success) {
        LOG_DEBUG(QString("Skip caching failed query: %1").arg(querySql.left(50)));
        return false;
    }

    // 估算内存占用
    uint64_t memorySize = estimateMemorySize(result);

    // 如果单个结果超过最大内存的 50%，拒绝缓存
    if (memorySize > maxMemoryBytes_ / 2) {
        LOG_WARN(QString("Query result too large to cache: %1 bytes (limit: %2 bytes)")
                    .arg(memorySize)
                    .arg(maxMemoryBytes_ / 2));
        return false;
    }

    // 检查是否已存在（更新）
    if (cache_.contains(querySql)) {
        CacheEntry& existing = cache_[querySql];
        totalMemoryBytes_ -= existing.memorySizeBytes;

        // 更新条目
        existing.result = result;
        existing.createdAt = QDateTime::currentDateTime();
        existing.lastAccessedAt = QDateTime::currentDateTime();
        existing.accessCount = 0;
        existing.affectedTables = affectedTables;
        existing.memorySizeBytes = memorySize;

        totalMemoryBytes_ += memorySize;

        LOG_DEBUG(QString("Cache UPDATED: %1").arg(querySql.left(50)));
        return true;
    }

    // 检查是否需要驱逐（内存限制）
    if (totalMemoryBytes_ + memorySize > maxMemoryBytes_) {
        int evicted = evictToFreeMemory(memorySize);
        LOG_INFO(QString("Evicted %1 entries to free memory for new cache entry").arg(evicted));
    }

    // 检查是否需要驱逐（条目数限制）
    if (cache_.size() >= static_cast<int>(maxEntries_)) {
        int evicted = evictLRU();
        LOG_INFO(QString("Evicted %1 LRU entries (max entries reached)").arg(evicted));
    }

    // 创建新条目
    CacheEntry entry;
    entry.result = result;
    entry.affectedTables = affectedTables;
    entry.memorySizeBytes = memorySize;

    // 插入缓存
    cache_[querySql] = entry;
    totalMemoryBytes_ += memorySize;

    // 更新 tableToQueries_ 映射
    for (const QString& table : affectedTables) {
        tableToQueries_[table].insert(querySql);
    }

    LOG_DEBUG(QString("Cache PUT: %1 (memory=%2 bytes, tables=%3)")
                 .arg(querySql.left(50))
                 .arg(memorySize)
                 .arg(affectedTables.size()));

    return true;
}

int QueryCache::invalidateTable(const QString& tableName) {
    QMutexLocker locker(&mutex_);

    if (!tableToQueries_.contains(tableName)) {
        return 0;
    }

    QSet<QString> queriesToInvalidate = tableToQueries_[tableName];
    int invalidatedCount = 0;

    for (const QString& querySql : queriesToInvalidate) {
        auto it = cache_.find(querySql);
        if (it != cache_.end()) {
            totalMemoryBytes_ -= it.value().memorySizeBytes;
            cache_.erase(it);
            invalidatedCount++;
        }
    }

    // 清理 tableToQueries_ 映射
    for (const QString& querySql : queriesToInvalidate) {
        auto cacheIt = cache_.find(querySql);
        if (cacheIt == cache_.end()) {
            // 该查询已被删除，从所有表的映射中移除
            for (auto tableIt = tableToQueries_.begin(); tableIt != tableToQueries_.end(); ++tableIt) {
                tableIt.value().remove(querySql);
            }
        }
    }

    tableToQueries_.remove(tableName);

    LOG_INFO(QString("Invalidated %1 cache entries for table '%2'")
                .arg(invalidatedCount)
                .arg(tableName));

    return invalidatedCount;
}

int QueryCache::invalidateDatabase(const QString& databaseName) {
    QMutexLocker locker(&mutex_);

    int invalidatedCount = 0;

    // 遍历所有缓存条目，查找包含该数据库的查询
    // 简化实现：通过 SQL 文本匹配 "USE DATABASE xxx" 或 "databaseName.tableName"
    QVector<QString> queriesToRemove;

    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        const QString& querySql = it.key();

        // 检查 SQL 是否包含数据库名（简化匹配）
        if (querySql.contains(databaseName, Qt::CaseInsensitive)) {
            queriesToRemove.append(querySql);
        }
    }

    // 删除匹配的查询
    for (const QString& querySql : queriesToRemove) {
        auto it = cache_.find(querySql);
        if (it != cache_.end()) {
            CacheEntry& entry = it.value();
            totalMemoryBytes_ -= entry.memorySizeBytes;

            // 从 tableToQueries_ 映射中删除
            for (const QString& table : entry.affectedTables) {
                if (tableToQueries_.contains(table)) {
                    tableToQueries_[table].remove(querySql);
                    if (tableToQueries_[table].isEmpty()) {
                        tableToQueries_.remove(table);
                    }
                }
            }

            cache_.erase(it);
            invalidatedCount++;
        }
    }

    LOG_INFO(QString("Invalidated %1 cache entries for database '%2'")
                .arg(invalidatedCount)
                .arg(databaseName));

    return invalidatedCount;
}

void QueryCache::clear() {
    QMutexLocker locker(&mutex_);

    int entriesCleared = cache_.size();
    cache_.clear();
    tableToQueries_.clear();
    totalMemoryBytes_ = 0;

    LOG_INFO(QString("Cache cleared: %1 entries removed").arg(entriesCleared));
}

QueryCache::Statistics QueryCache::getStatistics() const {
    // 注意：调用者已持有锁（在 get/put 中调用）或需要在外部加锁
    Statistics stats;
    stats.totalEntries = cache_.size();
    stats.totalHits = totalHits_;
    stats.totalMisses = totalMisses_;
    stats.totalEvictions = totalEvictions_;
    stats.totalMemoryBytes = totalMemoryBytes_;

    uint64_t totalAccesses = totalHits_ + totalMisses_;
    if (totalAccesses > 0) {
        stats.hitRate = static_cast<double>(totalHits_) / static_cast<double>(totalAccesses);
    } else {
        stats.hitRate = 0.0;
    }

    return stats;
}

void QueryCache::setEnabled(bool enabled) {
    QMutexLocker locker(&mutex_);
    enabled_ = enabled;
    LOG_INFO(QString("QueryCache %1").arg(enabled ? "enabled" : "disabled"));
}

bool QueryCache::isEnabled() const {
    QMutexLocker locker(&mutex_);
    return enabled_;
}

QString QueryCache::normalizeQuery(const QString& sql) {
    // 标准化步骤：
    // 1. 去除前后空格
    // 2. 压缩多个空格为一个
    // 3. 统一换行符为空格
    // 4. 转换为大写（可选，这里保持原样以便调试）

    QString normalized = sql.trimmed();

    // 替换所有换行符和制表符为空格
    normalized.replace('\n', ' ');
    normalized.replace('\r', ' ');
    normalized.replace('\t', ' ');

    // 压缩多个空格为一个
    normalized = normalized.simplified();

    return normalized;
}

uint64_t QueryCache::estimateMemorySize(const QueryResult& result) const {
    uint64_t size = 0;

    // 基础结构大小
    size += sizeof(QueryResult);

    // 列名
    for (const QString& colName : result.columnNames) {
        size += colName.size() * sizeof(QChar);
    }

    // 行数据
    for (const auto& row : result.rows) {
        for (const QVariant& value : row) {
            // 估算 QVariant 大小
            size += sizeof(QVariant);

            // 根据类型估算值的大小
            switch (value.metaType().id()) {
                case QMetaType::QString:
                    size += value.toString().size() * sizeof(QChar);
                    break;
                case QMetaType::Int:
                case QMetaType::UInt:
                case QMetaType::LongLong:
                case QMetaType::ULongLong:
                case QMetaType::Double:
                case QMetaType::Bool:
                    size += 8; // 基本类型，最大 8 字节
                    break;
                case QMetaType::QByteArray:
                    size += value.toByteArray().size();
                    break;
                case QMetaType::QDate:
                case QMetaType::QTime:
                case QMetaType::QDateTime:
                    size += 16; // 日期时间类型估算
                    break;
                default:
                    size += 8; // 其他类型默认估算
                    break;
            }
        }
    }

    // 消息字符串
    size += result.message.size() * sizeof(QChar);

    return size;
}

bool QueryCache::isExpired(const CacheEntry& entry) const {
    if (ttlSeconds_ == 0) {
        return false; // 永不过期
    }

    QDateTime now = QDateTime::currentDateTime();
    qint64 ageSeconds = entry.createdAt.secsTo(now);

    return ageSeconds > static_cast<qint64>(ttlSeconds_);
}

int QueryCache::evictLRU() {
    if (cache_.isEmpty()) {
        return 0;
    }

    // 找到最少最近使用的条目（lastAccessedAt 最早的）
    QString lruKey;
    QDateTime oldestAccess = QDateTime::currentDateTime();

    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if (it.value().lastAccessedAt < oldestAccess) {
            oldestAccess = it.value().lastAccessedAt;
            lruKey = it.key();
        }
    }

    if (lruKey.isEmpty()) {
        return 0;
    }

    // 删除 LRU 条目
    auto it = cache_.find(lruKey);
    if (it != cache_.end()) {
        CacheEntry& entry = it.value();
        totalMemoryBytes_ -= entry.memorySizeBytes;

        // 从 tableToQueries_ 映射中删除
        for (const QString& table : entry.affectedTables) {
            if (tableToQueries_.contains(table)) {
                tableToQueries_[table].remove(lruKey);
                if (tableToQueries_[table].isEmpty()) {
                    tableToQueries_.remove(table);
                }
            }
        }

        cache_.erase(it);
        totalEvictions_++;

        LOG_DEBUG(QString("Evicted LRU entry: %1").arg(lruKey.left(50)));
        return 1;
    }

    return 0;
}

int QueryCache::evictToFreeMemory(uint64_t requiredBytes) {
    int evictedCount = 0;
    uint64_t freedBytes = 0;

    // 持续驱逐 LRU 条目，直到释放足够的内存
    while (freedBytes < requiredBytes && !cache_.isEmpty()) {
        // 找到 LRU 条目
        QString lruKey;
        QDateTime oldestAccess = QDateTime::currentDateTime();

        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it.value().lastAccessedAt < oldestAccess) {
                oldestAccess = it.value().lastAccessedAt;
                lruKey = it.key();
            }
        }

        if (lruKey.isEmpty()) {
            break;
        }

        // 删除 LRU 条目
        auto it = cache_.find(lruKey);
        if (it != cache_.end()) {
            CacheEntry& entry = it.value();
            uint64_t entrySize = entry.memorySizeBytes;

            totalMemoryBytes_ -= entrySize;
            freedBytes += entrySize;

            // 从 tableToQueries_ 映射中删除
            for (const QString& table : entry.affectedTables) {
                if (tableToQueries_.contains(table)) {
                    tableToQueries_[table].remove(lruKey);
                    if (tableToQueries_[table].isEmpty()) {
                        tableToQueries_.remove(table);
                    }
                }
            }

            cache_.erase(it);
            totalEvictions_++;
            evictedCount++;

            LOG_DEBUG(QString("Evicted entry to free memory: %1 (freed %2 bytes)")
                         .arg(lruKey.left(50))
                         .arg(entrySize));
        }
    }

    LOG_DEBUG(QString("Evicted %1 entries, freed %2 bytes (required %3 bytes)")
                 .arg(evictedCount)
                 .arg(freedBytes)
                 .arg(requiredBytes));

    return evictedCount;
}

} // namespace qindb
