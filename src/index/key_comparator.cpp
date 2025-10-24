#include "qindb/key_comparator.h"
#include "qindb/type_serializer.h"
#include "qindb/logger.h"
#include <QDateTime>
#include <QCryptographicHash>
#include <cmath>
#include <limits>

namespace qindb {

// ============ 主比较函数 ============

int KeyComparator::compare(const QVariant& key1, const QVariant& key2, DataType type) {
    // NULL 值处理：NULL < 任何非 NULL 值，NULL == NULL
    bool isNull1 = key1.isNull();
    bool isNull2 = key2.isNull();

    if (isNull1 && isNull2) {
        return 0;  // NULL == NULL
    }
    if (isNull1) {
        return -1;  // NULL < 非NULL
    }
    if (isNull2) {
        return 1;   // 非NULL > NULL
    }

    // 根据类型分发
    if (isIntegerType(type)) {
        return compareInteger(key1, key2, type);
    } else if (isFloatType(type)) {
        return compareFloat(key1, key2, type);
    } else if (type == DataType::DECIMAL || type == DataType::NUMERIC) {
        return compareDecimal(key1, key2);
    } else if (isStringType(type)) {
        return compareString(key1, key2, type);
    } else if (isBinaryType(type)) {
        return compareBinary(key1, key2);
    } else if (isDateTimeType(type)) {
        return compareDateTime(key1, key2, type);
    } else if (type == DataType::BOOLEAN || type == DataType::BOOL) {
        return compareBoolean(key1, key2);
    } else if (type == DataType::UUID || type == DataType::UNIQUEIDENTIFIER) {
        return compareUUID(key1, key2);
    }

    LOG_ERROR(QString("Unsupported key type for comparison: %1").arg(static_cast<int>(type)));
    return 0;
}

int KeyComparator::compareSerialized(const QByteArray& serializedKey1,
                                    const QByteArray& serializedKey2,
                                    DataType type) {
    // 反序列化后比较
    QVariant key1, key2;
    if (!TypeSerializer::deserialize(serializedKey1, type, key1)) {
        LOG_ERROR("Failed to deserialize key1");
        return 0;
    }
    if (!TypeSerializer::deserialize(serializedKey2, type, key2)) {
        LOG_ERROR("Failed to deserialize key2");
        return 0;
    }
    return compare(key1, key2, type);
}

bool KeyComparator::isIndexableType(DataType type) {
    // 大多数类型都可以索引，除了一些特殊类型
    switch (type) {
        case DataType::GEOMETRY:
        case DataType::GEOGRAPHY:
            // 空间类型需要R-树，不能用B+树索引
            return false;
        case DataType::JSON:
        case DataType::JSONB:
        case DataType::XML:
            // JSON/XML可以索引，但作为字符串处理
            return true;
        case DataType::LONGTEXT:
        case DataType::MEDIUMTEXT:
        case DataType::LONGBLOB:
        case DataType::MEDIUMBLOB:
            // 超大字段不建议索引，但技术上可以
            return true;
        default:
            return true;
    }
}

// ============ 整数比较 ============

int KeyComparator::compareInteger(const QVariant& key1, const QVariant& key2, DataType type) {
    Q_UNUSED(type);

    // 统一转换为int64_t比较
    qint64 v1 = key1.toLongLong();
    qint64 v2 = key2.toLongLong();

    if (v1 < v2) return -1;
    if (v1 > v2) return 1;
    return 0;
}

// ============ 浮点比较 ============

int KeyComparator::compareFloat(const QVariant& key1, const QVariant& key2, DataType type) {
    Q_UNUSED(type);

    double v1 = key1.toDouble();
    double v2 = key2.toDouble();

    // 处理NaN和无穷大
    bool isNan1 = std::isnan(v1);
    bool isNan2 = std::isnan(v2);

    if (isNan1 && isNan2) return 0;
    if (isNan1) return 1;  // NaN > 任何值
    if (isNan2) return -1;

    bool isInf1 = std::isinf(v1);
    bool isInf2 = std::isinf(v2);

    if (isInf1 && isInf2) {
        if (v1 > 0 && v2 > 0) return 0;  // +Inf == +Inf
        if (v1 < 0 && v2 < 0) return 0;  // -Inf == -Inf
        return (v1 > 0) ? 1 : -1;
    }

    // 使用epsilon比较浮点数
    const double epsilon = 1e-10;
    double diff = v1 - v2;

    if (std::abs(diff) < epsilon) {
        return 0;
    }
    return (diff < 0) ? -1 : 1;
}

// ============ 定点数比较 ============

int KeyComparator::compareDecimal(const QVariant& key1, const QVariant& key2) {
    // DECIMAL存储为字符串，需要数值比较
    QString s1 = key1.toString();
    QString s2 = key2.toString();

    // 简化实现：转换为double比较
    // 生产环境应使用高精度decimal库
    bool ok1, ok2;
    double v1 = s1.toDouble(&ok1);
    double v2 = s2.toDouble(&ok2);

    if (!ok1 || !ok2) {
        // 如果转换失败，使用字符串比较
        return s1.compare(s2);
    }

    if (v1 < v2) return -1;
    if (v1 > v2) return 1;
    return 0;
}

// ============ 字符串比较 ============

int KeyComparator::compareString(const QVariant& key1, const QVariant& key2, DataType type) {
    QString s1 = key1.toString();
    QString s2 = key2.toString();

    // CHAR类型：去除尾部空格后比较
    if (type == DataType::CHAR) {
        s1 = s1.trimmed();
        s2 = s2.trimmed();
    }

    // 使用本地化比较（支持Unicode）
    return s1.compare(s2, Qt::CaseSensitive);
}

// ============ 二进制比较 ============

int KeyComparator::compareBinary(const QVariant& key1, const QVariant& key2) {
    QByteArray b1 = key1.toByteArray();
    QByteArray b2 = key2.toByteArray();

    // 字节序列比较
    int minLen = qMin(b1.size(), b2.size());

    for (int i = 0; i < minLen; ++i) {
        uint8_t byte1 = static_cast<uint8_t>(b1[i]);
        uint8_t byte2 = static_cast<uint8_t>(b2[i]);
        if (byte1 < byte2) return -1;
        if (byte1 > byte2) return 1;
    }

    // 如果前缀相同，长度短的小
    if (b1.size() < b2.size()) return -1;
    if (b1.size() > b2.size()) return 1;
    return 0;
}

// ============ 日期时间比较 ============

int KeyComparator::compareDateTime(const QVariant& key1, const QVariant& key2, DataType type) {
    switch (type) {
        case DataType::DATE: {
            QDate d1 = key1.toDate();
            QDate d2 = key2.toDate();
            if (d1 < d2) return -1;
            if (d1 > d2) return 1;
            return 0;
        }

        case DataType::TIME: {
            QTime t1 = key1.toTime();
            QTime t2 = key2.toTime();
            if (t1 < t2) return -1;
            if (t1 > t2) return 1;
            return 0;
        }

        case DataType::DATETIME:
        case DataType::DATETIME2:
        case DataType::TIMESTAMP:
        case DataType::TIMESTAMP_TZ:
        case DataType::DATETIMEOFFSET:
        case DataType::SMALLDATETIME: {
            QDateTime dt1 = key1.toDateTime();
            QDateTime dt2 = key2.toDateTime();
            if (dt1 < dt2) return -1;
            if (dt1 > dt2) return 1;
            return 0;
        }

        default:
            return 0;
    }
}

// ============ 布尔比较 ============

int KeyComparator::compareBoolean(const QVariant& key1, const QVariant& key2) {
    bool b1 = key1.toBool();
    bool b2 = key2.toBool();

    // false < true
    if (!b1 && b2) return -1;
    if (b1 && !b2) return 1;
    return 0;
}

// ============ UUID比较 ============

int KeyComparator::compareUUID(const QVariant& key1, const QVariant& key2) {
    QString u1 = key1.toString();
    QString u2 = key2.toString();

    // 移除格式化字符，只比较十六进制值
    u1.remove('{').remove('}').remove('-');
    u2.remove('{').remove('}').remove('-');

    // 字符串比较（UUID是固定长度的十六进制字符串）
    return u1.compare(u2, Qt::CaseInsensitive);
}

// ============ 哈希函数 ============

uint64_t KeyComparator::hash(const QVariant& key, DataType type) {
    if (key.isNull()) {
        return 0;  // NULL值的哈希为0
    }

    if (isIntegerType(type)) {
        return hashInteger(key, type);
    } else if (isFloatType(type)) {
        return hashFloat(key, type);
    } else if (type == DataType::DECIMAL || type == DataType::NUMERIC) {
        return hashDecimal(key);
    } else if (isStringType(type)) {
        return hashString(key);
    } else if (isBinaryType(type)) {
        return hashBinary(key);
    } else if (isDateTimeType(type)) {
        return hashDateTime(key, type);
    } else if (type == DataType::BOOLEAN || type == DataType::BOOL) {
        return hashBoolean(key);
    } else if (type == DataType::UUID || type == DataType::UNIQUEIDENTIFIER) {
        return hashUUID(key);
    }

    return 0;
}

uint64_t KeyComparator::hashInteger(const QVariant& key, DataType type) {
    Q_UNUSED(type);
    return static_cast<uint64_t>(key.toLongLong());
}

uint64_t KeyComparator::hashFloat(const QVariant& key, DataType type) {
    Q_UNUSED(type);

    double v = key.toDouble();

    // 处理特殊值
    if (std::isnan(v)) {
        return std::numeric_limits<uint64_t>::max();
    }
    if (std::isinf(v)) {
        return (v > 0) ? (std::numeric_limits<uint64_t>::max() - 1) : 0;
    }

    // 将double转换为uint64_t（通过memcpy避免类型双关）
    uint64_t hash;
    std::memcpy(&hash, &v, sizeof(uint64_t));
    return hash;
}

uint64_t KeyComparator::hashDecimal(const QVariant& key) {
    QString s = key.toString();
    return qHash(s);
}

uint64_t KeyComparator::hashString(const QVariant& key) {
    QString s = key.toString();
    return qHash(s);
}

uint64_t KeyComparator::hashBinary(const QVariant& key) {
    QByteArray b = key.toByteArray();

    // 使用简单的FNV-1a哈希算法
    uint64_t hash = 14695981039346656037ULL;  // FNV offset basis
    const uint64_t prime = 1099511628211ULL;  // FNV prime

    for (int i = 0; i < b.size(); ++i) {
        hash ^= static_cast<uint8_t>(b[i]);
        hash *= prime;
    }

    return hash;
}

uint64_t KeyComparator::hashDateTime(const QVariant& key, DataType type) {
    switch (type) {
        case DataType::DATE: {
            QDate d = key.toDate();
            return static_cast<uint64_t>(d.toJulianDay());
        }

        case DataType::TIME: {
            QTime t = key.toTime();
            return static_cast<uint64_t>(t.msecsSinceStartOfDay());
        }

        case DataType::DATETIME:
        case DataType::DATETIME2:
        case DataType::TIMESTAMP:
        case DataType::TIMESTAMP_TZ:
        case DataType::DATETIMEOFFSET:
        case DataType::SMALLDATETIME: {
            QDateTime dt = key.toDateTime();
            return static_cast<uint64_t>(dt.toMSecsSinceEpoch());
        }

        default:
            return 0;
    }
}

uint64_t KeyComparator::hashBoolean(const QVariant& key) {
    return key.toBool() ? 1 : 0;
}

uint64_t KeyComparator::hashUUID(const QVariant& key) {
    QString u = key.toString();
    u.remove('{').remove('}').remove('-');

    // 取UUID的前8字节作为哈希值
    bool ok;
    uint64_t hash = u.left(16).toULongLong(&ok, 16);
    return ok ? hash : qHash(u);
}

} // namespace qindb
