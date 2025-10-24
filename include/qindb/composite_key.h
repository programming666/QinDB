#ifndef QINDB_COMPOSITE_KEY_H
#define QINDB_COMPOSITE_KEY_H

#include "common.h"
#include <QVariant>
#include <QVector>
#include <QByteArray>
#include <QDataStream>

namespace qindb {

/**
 * @brief 复合键 - 用于多列索引
 *
 * 特性：
 * - 支持任意数量的列
 * - 支持不同数据类型的列
 * - 支持序列化/反序列化
 * - 支持字典序比较
 */
class CompositeKey {
public:
    /**
     * @brief 默认构造函数
     */
    CompositeKey();

    /**
     * @brief 构造函数
     * @param values 键值列表
     * @param types 对应的数据类型列表
     */
    CompositeKey(const QVector<QVariant>& values, const QVector<DataType>& types);

    /**
     * @brief 添加一个键值
     * @param value 键值
     * @param type 数据类型
     */
    void addValue(const QVariant& value, DataType type);

    /**
     * @brief 获取键值数量
     */
    int size() const { return values_.size(); }

    /**
     * @brief 获取第i个键值
     */
    const QVariant& getValue(int index) const { return values_[index]; }

    /**
     * @brief 获取第i个键值的类型
     */
    DataType getType(int index) const { return types_[index]; }

    /**
     * @brief 获取所有键值
     */
    const QVector<QVariant>& getValues() const { return values_; }

    /**
     * @brief 获取所有类型
     */
    const QVector<DataType>& getTypes() const { return types_; }

    /**
     * @brief 序列化为字节数组
     * @return 序列化后的字节数组
     */
    QByteArray serialize() const;

    /**
     * @brief 从字节数组反序列化
     * @param data 序列化的字节数组
     * @return 是否成功
     */
    bool deserialize(const QByteArray& data);

    /**
     * @brief 比较两个复合键（字典序）
     * @param other 另一个复合键
     * @return < 0 表示 this < other
     *         == 0 表示 this == other
     *         > 0 表示 this > other
     */
    int compare(const CompositeKey& other) const;

    /**
     * @brief 计算哈希值
     */
    uint64_t hash() const;

    /**
     * @brief 检查是否为空
     */
    bool isEmpty() const { return values_.isEmpty(); }

    /**
     * @brief 清空
     */
    void clear();

    /**
     * @brief 相等运算符
     */
    bool operator==(const CompositeKey& other) const {
        return compare(other) == 0;
    }

    /**
     * @brief 小于运算符
     */
    bool operator<(const CompositeKey& other) const {
        return compare(other) < 0;
    }

    /**
     * @brief 大于运算符
     */
    bool operator>(const CompositeKey& other) const {
        return compare(other) > 0;
    }

    /**
     * @brief 转换为字符串（用于调试）
     */
    QString toString() const;

private:
    QVector<QVariant> values_;     // 键值列表
    QVector<DataType> types_;      // 对应的数据类型列表
};

} // namespace qindb

#endif // QINDB_COMPOSITE_KEY_H
