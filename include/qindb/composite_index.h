#ifndef QINDB_COMPOSITE_INDEX_H  // 防止重复包含的头文件保护宏
#define QINDB_COMPOSITE_INDEX_H

#include "common.h"          // 包含公共定义和类型
#include "composite_key.h"   // 包含复合键相关定义
#include "generic_bplustree.h" // 包含通用B+树实现
#include "buffer_pool_manager.h" // 包含缓冲池管理器
#include <QVector>           // Qt动态数组容器
#include <QPair>
#include <memory>           // 智能指针相关头文件

namespace qindb {           // 定义qindb命名空间

/**
 * @brief 复合索引 - 支持多列索引的B+树
 *
 * 特性：
 * - 支持任意数量的列组成复合键
 * - 使用字典序比较
 * - 基于 GenericBPlusTree 实现
 * - 支持范围查询
 *
 * 使用示例：
 * CREATE INDEX idx_name_age ON users(name, age);
 */
class CompositeIndex {      // 复合索引类定义
public:
    /**
     * @brief 构造函数
     * @param bufferPoolManager 缓冲池管理器指针
     * @param columnTypes 复合键各列的数据类型数组
     * @param rootPageId 根节点页ID（如果是新索引则为 INVALID_PAGE_ID）
     */
    CompositeIndex(BufferPoolManager* bufferPoolManager,
                  const QVector<DataType>& columnTypes,
                  PageId rootPageId = INVALID_PAGE_ID);

    ~CompositeIndex();      // 析构函数

    /**
     * @brief 插入复合键
     * @param key 复合键
     * @param rowId 行ID
     * @return 是否成功
     */
    bool insert(const CompositeKey& key, RowId rowId);

    /**
     * @brief 删除复合键
     * @param key 要删除的复合键
     * @return 是否成功
     */
    bool remove(const CompositeKey& key);

    /**
     * @brief 查找复合键对应的行ID
     * @param key 要查找的复合键
     * @param rowId 输出参数，找到的行ID
     * @return 是否找到
     */
    bool search(const CompositeKey& key, RowId& rowId);

    /**
     * @brief 范围查询
     * @param minKey 最小键（包含）
     * @param maxKey 最大键（包含）
     * @param results 输出参数，查找结果 (key, rowId)
     * @return 是否成功
     */
    bool rangeSearch(const CompositeKey& minKey, const CompositeKey& maxKey,
                    QVector<QPair<CompositeKey, RowId>>& results);

    /**
     * @brief 前缀查询（只匹配复合键的前几列）
     * @param prefix 前缀键（可以少于完整的列数）
     * @param results 输出参数，查找结果 (key, rowId)
     * @return 是否成功
     *
     * 示例：
     * 索引 (name, age, city)
     * 前缀查询 (name='Alice') 会返回所有 name='Alice' 的记录
     */
    bool prefixSearch(const CompositeKey& prefix,
                     QVector<QPair<CompositeKey, RowId>>& results);

    /**
     * @brief 获取根节点页ID
     */
    PageId getRootPageId() const;

    /**
     * @brief 获取列类型
     */
    const QVector<DataType>& getColumnTypes() const { return columnTypes_; }

    /**
     * @brief 获取列数量
     */
    int getColumnCount() const { return columnTypes_.size(); }

private:
    BufferPoolManager* bufferPoolManager_;
    QVector<DataType> columnTypes_;          // 各列的数据类型
    std::unique_ptr<GenericBPlusTree> tree_; // 底层B+树（使用BINARY类型存储序列化的CompositeKey）

    /**
     * @brief 将复合键序列化为QVariant（用于传递给GenericBPlusTree）
     */
    QVariant serializeKey(const CompositeKey& key) const;

    /**
     * @brief 从QVariant反序列化复合键
     */
    CompositeKey deserializeKey(const QVariant& variant) const;
};

} // namespace qindb

#endif // QINDB_COMPOSITE_INDEX_H
