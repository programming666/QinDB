#ifndef QINDB_KEY_COMPARATOR_H
#define QINDB_KEY_COMPARATOR_H

#include "common.h"
#include <QVariant>
#include <QByteArray>

namespace qindb {

/**
 * @brief 键比较器 - 为所有数据类型提供比较功能
 *
 * 用于B+树、哈希索引等索引结构中的键比较
 */
class KeyComparator {
public:
    /**
     * @brief 比较两个QVariant类型的键
     * @param key1 第一个键
     * @param key2 第二个键
     * @param type 键的数据类型
     * @return < 0 表示 key1 < key2
     *         == 0 表示 key1 == key2
     *         > 0 表示 key1 > key2
     */
    static int compare(const QVariant& key1, const QVariant& key2, DataType type);

    /**
     * @brief 比较两个序列化后的键
     * @param serializedKey1 第一个键的序列化数据
     * @param serializedKey2 第二个键的序列化数据
     * @param type 键的数据类型
     * @return < 0 表示 key1 < key2
     *         == 0 表示 key1 == key2
     *         > 0 表示 key1 > key2
     */
    static int compareSerialized(const QByteArray& serializedKey1,
                                const QByteArray& serializedKey2,
                                DataType type);

    /**
     * @brief 检查数据类型是否可以用作索引键
     * @param type 数据类型
     * @return 是否可以用作索引键
     */
    static bool isIndexableType(DataType type);

    /**
     * @brief 计算键的哈希值（用于哈希索引）
     * @param key 键
     * @param type 数据类型
     * @return 哈希值
     */
    static uint64_t hash(const QVariant& key, DataType type);

private:
    // 各类型的比较实现
    static int compareInteger(const QVariant& key1, const QVariant& key2, DataType type);
    static int compareFloat(const QVariant& key1, const QVariant& key2, DataType type);
    static int compareDecimal(const QVariant& key1, const QVariant& key2);
    static int compareString(const QVariant& key1, const QVariant& key2, DataType type);
    static int compareBinary(const QVariant& key1, const QVariant& key2);
    static int compareDateTime(const QVariant& key1, const QVariant& key2, DataType type);
    static int compareBoolean(const QVariant& key1, const QVariant& key2);
    static int compareUUID(const QVariant& key1, const QVariant& key2);

    // 各类型的哈希实现
    static uint64_t hashInteger(const QVariant& key, DataType type);
    static uint64_t hashFloat(const QVariant& key, DataType type);
    static uint64_t hashDecimal(const QVariant& key);
    static uint64_t hashString(const QVariant& key);
    static uint64_t hashBinary(const QVariant& key);
    static uint64_t hashDateTime(const QVariant& key, DataType type);
    static uint64_t hashBoolean(const QVariant& key);
    static uint64_t hashUUID(const QVariant& key);
};

} // namespace qindb

#endif // QINDB_KEY_COMPARATOR_H
