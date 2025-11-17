#include "qindb/composite_index.h"  // 包含复合索引头文件
#include "qindb/logger.h"          // 包含日志记录头文件

namespace qindb {  // 定义qindb命名空间

CompositeIndex::CompositeIndex(BufferPoolManager* bufferPoolManager,
                              const QVector<DataType>& columnTypes,
                              PageId rootPageId)
    : bufferPoolManager_(bufferPoolManager)
    , columnTypes_(columnTypes)
{
    // 使用BINARY类型的B+树来存储序列化的CompositeKey
    tree_ = std::make_unique<GenericBPlusTree>(
        bufferPoolManager,
        DataType::BINARY,
        rootPageId,
        50  // 复合键可能较大，减少每页的键数
    );

    LOG_DEBUG(QString("CompositeIndex created with %1 columns").arg(columnTypes_.size()));
}

CompositeIndex::~CompositeIndex() {
}

bool CompositeIndex::insert(const CompositeKey& key, RowId rowId) {
    // 验证键的列数
    if (key.size() != columnTypes_.size()) {
        LOG_ERROR(QString("CompositeIndex::insert: key size mismatch (%1 vs %2)")
                     .arg(key.size()).arg(columnTypes_.size()));
        return false;
    }

    // 验证键的类型
    for (int i = 0; i < key.size(); ++i) {
        if (key.getType(i) != columnTypes_[i]) {
            LOG_ERROR(QString("CompositeIndex::insert: type mismatch at column %1").arg(i));
            return false;
        }
    }

    // 序列化复合键
    QVariant serializedKey = serializeKey(key);

    // 插入到底层B+树
    bool success = tree_->insert(serializedKey, rowId);

    if (success) {
        LOG_DEBUG(QString("CompositeIndex::insert: inserted key %1 -> rowId %2")
                     .arg(key.toString()).arg(rowId));
    } else {
        LOG_WARN(QString("CompositeIndex::insert: failed to insert key %1")
                    .arg(key.toString()));
    }

    return success;
}

bool CompositeIndex::remove(const CompositeKey& key) {
    // 验证键的列数
    if (key.size() != columnTypes_.size()) {
        LOG_ERROR(QString("CompositeIndex::remove: key size mismatch (%1 vs %2)")
                     .arg(key.size()).arg(columnTypes_.size()));
        return false;
    }

    // 序列化复合键
    QVariant serializedKey = serializeKey(key);

    // 从底层B+树删除
    bool success = tree_->remove(serializedKey);

    if (success) {
        LOG_DEBUG(QString("CompositeIndex::remove: removed key %1")
                     .arg(key.toString()));
    } else {
        LOG_WARN(QString("CompositeIndex::remove: failed to remove key %1")
                    .arg(key.toString()));
    }

    return success;
}

bool CompositeIndex::search(const CompositeKey& key, RowId& rowId) {
    // 验证键的列数
    if (key.size() != columnTypes_.size()) {
        LOG_ERROR(QString("CompositeIndex::search: key size mismatch (%1 vs %2)")
                     .arg(key.size()).arg(columnTypes_.size()));
        return false;
    }

    // 序列化复合键
    QVariant serializedKey = serializeKey(key);

    // 在底层B+树中查找
    return tree_->search(serializedKey, rowId);
}

bool CompositeIndex::rangeSearch(const CompositeKey& minKey, const CompositeKey& maxKey,
                                QVector<QPair<CompositeKey, RowId>>& results) {
    results.clear();

    // 验证键的列数
    if (minKey.size() != columnTypes_.size() || maxKey.size() != columnTypes_.size()) {
        LOG_ERROR("CompositeIndex::rangeSearch: key size mismatch");
        return false;
    }

    // 序列化最小和最大键
    QVariant serializedMinKey = serializeKey(minKey);
    QVariant serializedMaxKey = serializeKey(maxKey);

    // 在底层B+树中范围查询
    QVector<QPair<QVariant, RowId>> rawResults;
    if (!tree_->rangeSearch(serializedMinKey, serializedMaxKey, rawResults)) {
        return false;
    }

    // 反序列化结果
    results.reserve(rawResults.size());
    for (const auto& pair : rawResults) {
        CompositeKey key = deserializeKey(pair.first);
        results.append(qMakePair(key, pair.second));
    }

    LOG_DEBUG(QString("CompositeIndex::rangeSearch: found %1 results").arg(results.size()));
    return true;
}

bool CompositeIndex::prefixSearch(const CompositeKey& prefix,
                                 QVector<QPair<CompositeKey, RowId>>& results) {
    results.clear();

    // 前缀的列数必须 <= 索引的列数
    if (prefix.size() > columnTypes_.size()) {
        LOG_ERROR(QString("CompositeIndex::prefixSearch: prefix size too large (%1 vs %2)")
                     .arg(prefix.size()).arg(columnTypes_.size()));
        return false;
    }

    if (prefix.isEmpty()) {
        LOG_ERROR("CompositeIndex::prefixSearch: empty prefix");
        return false;
    }

    // 构造范围查询的最小和最大键
    // 例如：前缀 (name='Alice')
    // minKey = ('Alice', MIN_VALUE, MIN_VALUE, ...)
    // maxKey = ('Alice', MAX_VALUE, MAX_VALUE, ...)

    CompositeKey minKey = prefix;
    CompositeKey maxKey = prefix;

    // 填充剩余列为MIN/MAX值
    for (int i = prefix.size(); i < columnTypes_.size(); ++i) {
        DataType type = columnTypes_[i];

        // 对于MIN值，使用类型的最小值
        // 对于MAX值，使用类型的最大值
        // 这里使用一个简化的实现：用NULL表示MIN，用一个很大的值表示MAX

        // MIN值：使用该类型的"最小"表示
        QVariant minValue;
        switch (type) {
        case DataType::INT:
        case DataType::SMALLINT:
        case DataType::TINYINT:
            minValue = INT32_MIN;
            break;
        case DataType::BIGINT:
            minValue = INT64_MIN;
            break;
        case DataType::FLOAT:
        case DataType::DOUBLE:
        case DataType::DECIMAL:
            minValue = -std::numeric_limits<double>::max();
            break;
        case DataType::VARCHAR:
        case DataType::CHAR:
        case DataType::TEXT:
            minValue = QString("");
            break;
        case DataType::DATE:
        case DataType::DATETIME:
        case DataType::TIMESTAMP:
            minValue = QDateTime::fromMSecsSinceEpoch(0);
            break;
        default:
            minValue = QVariant();  // NULL
        }

        // MAX值：使用该类型的"最大"表示
        QVariant maxValue;
        switch (type) {
        case DataType::INT:
        case DataType::SMALLINT:
        case DataType::TINYINT:
            maxValue = INT32_MAX;
            break;
        case DataType::BIGINT:
            maxValue = INT64_MAX;
            break;
        case DataType::FLOAT:
        case DataType::DOUBLE:
        case DataType::DECIMAL:
            maxValue = std::numeric_limits<double>::max();
            break;
        case DataType::VARCHAR:
        case DataType::CHAR:
        case DataType::TEXT:
            // 使用一个很大的字符串（Unicode最大字符重复）
            maxValue = QString(100, QChar(0xFFFF));
            break;
        case DataType::DATE:
        case DataType::DATETIME:
        case DataType::TIMESTAMP:
            maxValue = QDateTime::fromMSecsSinceEpoch(LLONG_MAX);
            break;
        default:
            maxValue = QVariant();  // NULL
        }

        minKey.addValue(minValue, type);
        maxKey.addValue(maxValue, type);
    }

    // 使用范围查询
    return rangeSearch(minKey, maxKey, results);
}

PageId CompositeIndex::getRootPageId() const {
    return tree_->getRootPageId();
}

QVariant CompositeIndex::serializeKey(const CompositeKey& key) const {
    QByteArray data = key.serialize();
    return QVariant(data);
}

CompositeKey CompositeIndex::deserializeKey(const QVariant& variant) const {
    QByteArray data = variant.toByteArray();
    CompositeKey key;
    key.deserialize(data);
    return key;
}

} // namespace qindb
