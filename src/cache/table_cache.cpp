#include "qindb/table_cache.h"
#include "qindb/catalog.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/page.h"
#include "qindb/logger.h"
#include <QMutexLocker>
#include <QDebug>

namespace qindb {

TableCache::TableCache(uint64_t maxTableSizeBytes, uint64_t maxTotalMemoryBytes)
    : maxTableSizeBytes_(maxTableSizeBytes),
      maxTotalMemoryBytes_(maxTotalMemoryBytes),
      enabled_(true),
      cacheHits_(0),
      cacheMisses_(0),
      invalidations_(0),
      totalMemoryBytes_(0) {
}

TableCache::~TableCache() {
    clear();
}

QString TableCache::makeKey(const QString& dbName, const QString& tableName) const {
    return dbName + "." + tableName;
}

bool TableCache::isTableCached(const QString& dbName, const QString& tableName) const {
    if (!enabled_) {
        return false;
    }

    QMutexLocker locker(&mutex_);
    QString key = makeKey(dbName, tableName);
    auto it = cache_.find(key);
    return (it != cache_.end() && it->isValid);
}

bool TableCache::getTableData(const QString& dbName,
                              const QString& tableName,
                              QVector<QVector<QVariant>>& rows,
                              QVector<RecordHeader>& headers) {
    if (!enabled_) {
        cacheMisses_++;
        return false;
    }

    QMutexLocker locker(&mutex_);
    QString key = makeKey(dbName, tableName);

    auto it = cache_.find(key);
    if (it == cache_.end() || !it->isValid) {
        cacheMisses_++;
        return false;
    }

    // 缓存命中
    cacheHits_++;
    rows = it->rows;
    headers = it->headers;

    return true;
}

uint64_t TableCache::estimateTableSize(TableDef* table, BufferPoolManager* bufferPool) {
    if (!table || !bufferPool || table->firstPageId == INVALID_PAGE_ID) {
        return 0;
    }

    uint64_t totalSize = 0;
    PageId currentPageId = table->firstPageId;
    int pageCount = 0;

    // 遍历所有数据页，累加大小
    while (currentPageId != INVALID_PAGE_ID && pageCount < 1000) {  // 限制最多扫描1000页
        Page* page = bufferPool->fetchPage(currentPageId);
        if (!page) {
            break;
        }

        totalSize += PAGE_SIZE;  // 8192 字节

        // 获取下一页
        PageHeader* header = page->getHeader();
        PageId nextPageId = header->nextPageId;
        bufferPool->unpinPage(currentPageId, false);

        currentPageId = nextPageId;
        pageCount++;
    }

    return totalSize;
}

bool TableCache::loadTable(const QString& dbName,
                           TableDef* table,
                           BufferPoolManager* bufferPool) {
    if (!enabled_ || !table || !bufferPool) {
        return false;
    }

    // 先估算表大小
    uint64_t tableSize = estimateTableSize(table, bufferPool);

    // 如果表太大，不缓存
    if (tableSize > maxTableSizeBytes_) {
        qDebug() << "Table" << table->name << "is too large ("
                 << tableSize << "bytes), skip caching";
        return false;
    }

    QMutexLocker locker(&mutex_);

    // 检查总内存限制
    while (totalMemoryBytes_ + tableSize > maxTotalMemoryBytes_ && !cache_.isEmpty()) {
        evictLRU();
    }

    QString key = makeKey(dbName, table->name);
    TableCacheEntry entry;

    // 加载所有行数据
    PageId currentPageId = table->firstPageId;
    int pageCount = 0;

    while (currentPageId != INVALID_PAGE_ID && pageCount < 1000) {
        Page* page = bufferPool->fetchPage(currentPageId);
        if (!page) {
            qWarning() << "Failed to fetch page" << currentPageId
                       << "while loading table" << table->name;
            break;
        }

        // 从页中读取所有记录
        QVector<QVector<QVariant>> pageRecords;
        QVector<RecordHeader> pageHeaders;

        if (TablePage::getAllRecords(page, table, pageRecords, pageHeaders)) {
            entry.rows.append(pageRecords);
            entry.headers.append(pageHeaders);
        }

        // 移动到下一页
        PageHeader* header = page->getHeader();
        PageId nextPageId = header->nextPageId;
        bufferPool->unpinPage(currentPageId, false);

        currentPageId = nextPageId;
        pageCount++;
    }

    // 估算内存占用
    entry.memorySizeBytes = estimateMemorySize(entry.rows);
    entry.rowCount = entry.rows.size();
    entry.loadedAt = QDateTime::currentDateTime();
    entry.isValid = true;

    // 插入缓存
    cache_[key] = entry;
    totalMemoryBytes_ += entry.memorySizeBytes;

    qDebug() << "Cached table" << table->name << "from database" << dbName
             << "- Rows:" << entry.rowCount << "Memory:" << entry.memorySizeBytes << "bytes";

    return true;
}

void TableCache::invalidateTable(const QString& dbName, const QString& tableName) {
    QMutexLocker locker(&mutex_);
    QString key = makeKey(dbName, tableName);

    auto it = cache_.find(key);
    if (it != cache_.end()) {
        totalMemoryBytes_ -= it->memorySizeBytes;
        cache_.erase(it);
        invalidations_++;

        qDebug() << "Invalidated table cache for" << tableName
                 << "from database" << dbName;
    }
}

void TableCache::invalidateDatabase(const QString& dbName) {
    QMutexLocker locker(&mutex_);

    QList<QString> keysToRemove;
    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if (it.key().startsWith(dbName + ".")) {
            keysToRemove.append(it.key());
            totalMemoryBytes_ -= it->memorySizeBytes;
            invalidations_++;
        }
    }

    for (const QString& key : keysToRemove) {
        cache_.remove(key);
    }

    qDebug() << "Invalidated all table caches for database" << dbName
             << "- Removed" << keysToRemove.size() << "tables";
}

void TableCache::clear() {
    QMutexLocker locker(&mutex_);
    cache_.clear();
    totalMemoryBytes_ = 0;
    LOG_INFO("Cleared all table caches");
}

void TableCache::setMaxTableSize(uint64_t bytes) {
    QMutexLocker locker(&mutex_);
    maxTableSizeBytes_ = bytes;
}

void TableCache::setMaxTotalMemory(uint64_t bytes) {
    QMutexLocker locker(&mutex_);
    maxTotalMemoryBytes_ = bytes;

    // 如果当前内存占用超过新限制，驱逐表
    while (totalMemoryBytes_ > maxTotalMemoryBytes_ && !cache_.isEmpty()) {
        evictLRU();
    }
}

void TableCache::setEnabled(bool enabled) {
    QMutexLocker locker(&mutex_);
    enabled_ = enabled;
    if (!enabled) {
        clear();
    }
}

TableCache::Statistics TableCache::getStatistics() const {
    QMutexLocker locker(&mutex_);

    Statistics stats;
    stats.totalCachedTables = cache_.size();
    stats.totalMemoryBytes = totalMemoryBytes_;
    stats.cacheHits = cacheHits_;
    stats.cacheMisses = cacheMisses_;
    stats.invalidations = invalidations_;

    uint64_t totalAccess = cacheHits_ + cacheMisses_;
    stats.hitRate = (totalAccess > 0) ? (double)cacheHits_ / totalAccess : 0.0;

    return stats;
}

uint64_t TableCache::estimateMemorySize(const QVector<QVector<QVariant>>& rows) const {
    if (rows.isEmpty()) {
        return 0;
    }

    uint64_t size = 0;

    // 估算每行的大小
    for (const auto& row : rows) {
        for (const auto& value : row) {
            switch (value.type()) {
                case QVariant::Int:
                case QVariant::UInt:
                    size += sizeof(int);
                    break;
                case QVariant::LongLong:
                case QVariant::ULongLong:
                    size += sizeof(qint64);
                    break;
                case QVariant::Double:
                    size += sizeof(double);
                    break;
                case QVariant::String:
                    size += value.toString().size() * sizeof(QChar);
                    break;
                case QVariant::ByteArray:
                    size += value.toByteArray().size();
                    break;
                default:
                    size += 8;  // 默认8字节
                    break;
            }
        }
        size += sizeof(QVector<QVariant>);  // 行对象本身的开销
    }

    // 加上容器开销
    size += sizeof(QVector<QVector<QVariant>>);
    size += rows.capacity() * sizeof(QVector<QVariant>);

    return size;
}

void TableCache::evictLRU() {
    // 找到最早加载的表进行驱逐
    if (cache_.isEmpty()) {
        return;
    }

    auto oldestIt = cache_.begin();
    QDateTime oldestTime = oldestIt->loadedAt;

    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if (it->loadedAt < oldestTime) {
            oldestTime = it->loadedAt;
            oldestIt = it;
        }
    }

    qDebug() << "Evicting table cache" << oldestIt.key()
             << "to free memory (" << oldestIt->memorySizeBytes << "bytes)";

    totalMemoryBytes_ -= oldestIt->memorySizeBytes;
    cache_.erase(oldestIt);
}

} // namespace qindb
