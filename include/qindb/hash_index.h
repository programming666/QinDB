#ifndef QINDB_HASH_INDEX_H
#define QINDB_HASH_INDEX_H

#include "common.h"
#include "buffer_pool_manager.h"
#include "type_serializer.h"
#include <QString>
#include <QVariant>
#include <QMutex>
#include <vector>

namespace qindb {

/**
 * @brief 哈希索引类
 *
 * 特点：
 * - 等值查询 O(1) 平均时间复杂度
 * - 不支持范围查询
 * - 不支持排序
 * - 使用开链法处理冲突
 *
 * 存储结构：
 * - 目录页（Directory Page）：存储桶指针数组
 * - 桶页（Bucket Page）：存储键值对
 * - 溢出页（Overflow Page）：当桶满时链接额外页面
 */
class HashIndex {
public:
    /**
     * @brief 构造函数
     * @param indexName 索引名称
     * @param keyType 键的数据类型
     * @param bufferPool 缓冲池管理器
     * @param numBuckets 初始桶数量（必须是2的幂）
     */
    HashIndex(const QString& indexName,
              DataType keyType,
              BufferPoolManager* bufferPool,
              uint32_t numBuckets = 256);

    ~HashIndex();

    /**
     * @brief 插入键值对
     * @param key 键
     * @param value 值（RowId）
     * @return true 插入成功，false 插入失败
     */
    bool insert(const QVariant& key, RowId value);

    /**
     * @brief 查找键对应的值
     * @param key 键
     * @param value 输出参数，找到的值
     * @return true 找到，false 未找到
     */
    bool search(const QVariant& key, RowId& value);

    /**
     * @brief 查找键对应的所有值（支持重复键）
     * @param key 键
     * @param values 输出参数，所有匹配的值
     * @return true 找到至少一个，false 未找到
     */
    bool searchAll(const QVariant& key, std::vector<RowId>& values);

    /**
     * @brief 删除键值对
     * @param key 键
     * @param value 值（如果为 INVALID_ROW_ID，删除所有匹配的键）
     * @return true 删除成功，false 键不存在
     */
    bool remove(const QVariant& key, RowId value = INVALID_ROW_ID);

    /**
     * @brief 获取索引名称
     */
    const QString& getIndexName() const { return indexName_; }

    /**
     * @brief 获取键类型
     */
    DataType getKeyType() const { return keyType_; }

    /**
     * @brief 获取桶数量
     */
    uint32_t getNumBuckets() const { return numBuckets_; }

    /**
     * @brief 获取目录页ID
     */
    PageId getDirectoryPageId() const { return directoryPageId_; }

    /**
     * @brief 设置目录页ID（用于加载已存在的索引）
     */
    void setDirectoryPageId(PageId pageId) { directoryPageId_ = pageId; }

    /**
     * @brief 获取索引统计信息
     */
    struct Statistics {
        uint32_t numBuckets;      // 桶数量
        uint32_t numEntries;      // 总条目数
        uint32_t numOverflowPages; // 溢出页数量
        double avgBucketSize;     // 平均桶大小
        double loadFactor;        // 负载因子
    };
    Statistics getStatistics() const;

private:
    /**
     * @brief 哈希函数
     * @param key 序列化后的键
     * @return 哈希值（桶索引）
     */
    uint32_t hash(const QByteArray& key) const;

    /**
     * @brief 获取或创建桶页
     * @param bucketIndex 桶索引
     * @return 桶页ID
     */
    PageId getBucketPageId(uint32_t bucketIndex);

    /**
     * @brief 创建新的溢出页
     * @return 新页面ID
     */
    PageId createOverflowPage();

    /**
     * @brief 初始化目录页
     */
    void initializeDirectory();

    /**
     * @brief 序列化键
     */
    QByteArray serializeKey(const QVariant& key) const;

private:
    QString indexName_;           // 索引名称
    DataType keyType_;            // 键的数据类型
    BufferPoolManager* bufferPool_; // 缓冲池管理器
    uint32_t numBuckets_;         // 桶数量
    PageId directoryPageId_;      // 目录页ID
    mutable QMutex mutex_;        // 线程安全互斥锁
};

} // namespace qindb

#endif // QINDB_HASH_INDEX_H
