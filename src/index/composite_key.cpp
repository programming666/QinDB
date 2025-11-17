/**
 * @file composite_key.h
 * @brief 复合键类的实现，用于存储和比较多个键值对
 */
#include "qindb/composite_key.h"
#include "qindb/key_comparator.h"
#include "qindb/type_serializer.h"
#include "qindb/logger.h"
#include <QDataStream>

namespace qindb {

/**
 * @brief 默认构造函数，创建一个空的复合键
 */
CompositeKey::CompositeKey() {
}

/**
 * @brief 使用给定的值和类型列表构造复合键
 * @param values 键值列表
 * @param types 键值对应的数据类型列表
 */
CompositeKey::CompositeKey(const QVector<QVariant>& values, const QVector<DataType>& types)
    : values_(values)
    , types_(types)
{
    // 检查值和类型的数量是否匹配，如果不匹配则清空
    if (values_.size() != types_.size()) {
        LOG_ERROR(QString("CompositeKey: values and types size mismatch (%1 vs %2)")
                     .arg(values_.size()).arg(types_.size()));
        values_.clear();
        types_.clear();
    }
}

/**
 * @brief 向复合键中添加一个键值对
 * @param value 要添加的键值
 * @param value的类型
 */
void CompositeKey::addValue(const QVariant& value, DataType type) {
    values_.append(value);
    types_.append(type);
}

/**
 * @brief 序列化复合键为字节数组
 * @return 序列化后的字节数组，如果序列化失败则返回空字节数组
 */
QByteArray CompositeKey::serialize() const {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);

    // 写入键的数量
    stream << static_cast<qint32>(values_.size());

    // 依次序列化每个键值
    for (int i = 0; i < values_.size(); ++i) {
        // 写入类型
        stream << static_cast<quint8>(types_[i]);

        // 序列化值
        QByteArray serializedValue;
        if (!TypeSerializer::serialize(values_[i], types_[i], serializedValue)) {
            LOG_ERROR("CompositeKey::serialize: failed to serialize value");
            return QByteArray();
        }
        stream << serializedValue;
    }

    return result;
}

/**
 * @brief 从字节数组反序列化复合键
 * @param data 包含序列化数据的字节数组
 * @return 反序列化成功返回true，否则返回false
 */
bool CompositeKey::deserialize(const QByteArray& data) {
    QDataStream stream(data);

    // 读取键的数量
    qint32 count;
    stream >> count;

    // 检查数量是否在合理范围内
    if (count < 0 || count > 100) {  // 合理性检查
        LOG_ERROR(QString("CompositeKey::deserialize: invalid count %1").arg(count));
        return false;
    }

    values_.clear();
    types_.clear();
    values_.reserve(count);
    types_.reserve(count);

    // 依次反序列化每个键值
    for (int i = 0; i < count; ++i) {
        // 读取类型
        quint8 typeValue;
        stream >> typeValue;
        DataType type = static_cast<DataType>(typeValue);

        // 反序列化值
        QByteArray serializedValue;
        stream >> serializedValue;

        QVariant value;
        if (!TypeSerializer::deserialize(serializedValue, type, value)) {
            LOG_ERROR("CompositeKey::deserialize: failed to deserialize value");
            return false;
        }

        values_.append(value);
        types_.append(type);
    }

    return true;
}

int CompositeKey::compare(const CompositeKey& other) const {
    // 使用字典序比较
    int minSize = qMin(values_.size(), other.values_.size());

    for (int i = 0; i < minSize; ++i) {
        int cmp = KeyComparator::compare(values_[i], other.values_[i], types_[i]);
        if (cmp != 0) {
            return cmp;
        }
    }

    // 如果前面的值都相等，较短的键小于较长的键
    return values_.size() - other.values_.size();
}

uint64_t CompositeKey::hash() const {
    uint64_t result = 0;

    // 组合各个键值的哈希值
    for (int i = 0; i < values_.size(); ++i) {
        uint64_t h = KeyComparator::hash(values_[i], types_[i]);
        // 使用简单的哈希组合算法
        result = result * 31 + h;
    }

    return result;
}

void CompositeKey::clear() {
    values_.clear();
    types_.clear();
}

QString CompositeKey::toString() const {
    if (values_.isEmpty()) {
        return "()";
    }

    QStringList parts;
    for (int i = 0; i < values_.size(); ++i) {
        QString valueStr = values_[i].toString();
        parts.append(valueStr);
    }

    return "(" + parts.join(", ") + ")";
}

} // namespace qindb
