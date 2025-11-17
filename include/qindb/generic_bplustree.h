#ifndef QINDB_GENERIC_BPLUSTREE_H  // 防止重复包含的头文件宏定义
#define QINDB_GENERIC_BPLUSTREE_H

#include "common.h"        // 包含通用定义
#include "page.h"          // 包含页面相关定义
#include "buffer_pool_manager.h"  // 包含缓冲池管理器
#include "key_comparator.h"      // 包含键比较器
#include "type_serializer.h"     // 包含类型序列化器
#include <QVector>        // 包含Qt向量容器
#include <QPair>          // 包含Qt对容器
#include <QMutex>         // 包含Qt互斥锁

namespace qindb {        // 定义qindb命名空间

/**
 * @brief 通用B+树 - 支持所有数据类型的键
 *
 * 特性：
 * - 支持所有60+种数据类型作为索引键
 * - 使用变长键存储
 * - 自平衡
 * - 所有数据存储在叶子节点
 * - 叶子节点通过双向链表连接，支持范围查询
 * - 支持并发访问（树级锁）
 */
class GenericBPlusTree {  // 通用B+树类定义
public:
    /**
     * @brief 构造函数
     * @param bufferPoolManager 缓冲池管理器
     * @param keyType 键的数据类型
     * @param rootPageId 根节点页ID（如果是新索引则为 INVALID_PAGE_ID）
     * @param maxKeysPerPage 每个页面最多的键数（默认100，变长键需要较小的值）
     */
    GenericBPlusTree(BufferPoolManager* bufferPoolManager,
                    DataType keyType,
                    PageId rootPageId = INVALID_PAGE_ID,
                    int maxKeysPerPage = 100);

    ~GenericBPlusTree();  // 析构函数

    /**
     * @brief 插入键值对
     * @param key 键
     * @param value 值（行ID）
     * @return 是否成功
     */
    bool insert(const QVariant& key, RowId value);

    /**
     * @brief 删除键
     * @param key 要删除的键
     * @return 是否成功
     */
    bool remove(const QVariant& key);

    /**
     * @brief 查找键对应的值
     * @param key 要查找的键
     * @param value 输出参数，查找到的值
     * @return 是否找到
     */
    bool search(const QVariant& key, RowId& value);

    /**
     * @brief 范围查询
     * @param minKey 最小键（包含）
     * @param maxKey 最大键（包含）
     * @param results 输出参数，查找结果 (key, rowId)
     * @return 是否成功
     */
    bool rangeSearch(const QVariant& minKey, const QVariant& maxKey,
                    QVector<QPair<QVariant, RowId>>& results);

    /**
     * @brief 获取根节点页ID
     */
    PageId getRootPageId() const { return rootPageId_; }  // 内联函数，获取根节点页ID

    /**
     * @brief 获取键的数据类型
     */
    DataType getKeyType() const { return keyType_; }      // 内联函数，获取键的数据类型

    /**
     * @brief 获取索引统计信息
     */
    struct Stats {        // 统计信息结构体
        size_t numKeys;         // 总键数
        size_t numLeafPages;    // 叶子节点页数
        size_t numInternalPages;// 内部节点页数
        int treeHeight;         // 树高度
        size_t totalKeySize;    // 键数据总大小（字节）
    };

    Stats getStats() const;  // 获取统计信息

    /**
     * @brief 打印树结构（调试用）
     */
    void printTree() const;  // 打印树结构

private:
    /**
     * @brief 通用键值对（叶子节点）
     */
    struct KeyValuePair {    // 键值对结构体
        QByteArray serializedKey;   // 序列化后的键
        RowId value;                // 行ID

        KeyValuePair() : value(INVALID_ROW_ID) {}  // 默认构造函数
        KeyValuePair(const QByteArray& k, RowId v) : serializedKey(k), value(v) {}  // 带参数构造函数
    };

    /**
     * @brief 通用内部节点条目
     */
    struct InternalEntry {   // 内部节点条目结构体
        QByteArray serializedKey;   // 分隔键
        PageId childPageId;         // 子节点页ID

        InternalEntry() : childPageId(INVALID_PAGE_ID) {}  // 默认构造函数
        InternalEntry(const QByteArray& k, PageId p) : serializedKey(k), childPageId(p) {}  // 带参数构造函数
    };

    /**
     * @brief 序列化键
     */
    QByteArray serializeKey(const QVariant& key);  // 序列化键

    /**
     * @brief 反序列化键
     */
    QVariant deserializeKey(const QByteArray& serializedKey);  // 反序列化键

    /**
     * @brief 比较两个序列化的键
     */
    int compareKeys(const QByteArray& key1, const QByteArray& key2);  // 比较两个键

    /**
     * @brief 查找叶子节点
     */
    PageId findLeafPage(const QByteArray& serializedKey);  // 查找叶子节点

    /**
     * @brief 在叶子节点中插入
     */
    bool insertIntoLeaf(PageId leafPageId, const QByteArray& serializedKey, RowId value);  // 在叶子节点中插入

    /**
     * @brief 分裂叶子节点
     */
    bool splitLeafNode(PageId leafPageId, PageId& newLeafPageId, QByteArray& middleKey);  // 分裂叶子节点

    /**
     * @brief 分裂内部节点
     */
    bool splitInternalNode(PageId internalPageId, PageId& newInternalPageId, QByteArray& middleKey);  // 分裂内部节点

    /**
     * @brief 在父节点中插入
     */
    bool insertIntoParent(PageId parentPageId, const QByteArray& key,
                         /*PageId leftPageId, */ PageId rightPageId);  // 在父节点中插入

    /**
     * @brief 创建新根节点
     */
    PageId createNewRoot(PageId leftPageId, const QByteArray& key, PageId rightPageId);  // 创建新根节点

    /**
     * @brief 初始化新的叶子节点页
     */
    void initializeLeafPage(Page* page, PageId pageId);  // 初始化叶子节点页

    /**
     * @brief 初始化新的内部节点页
     */
    void initializeInternalPage(Page* page, PageId pageId);  // 初始化内部节点页

    /**
     * @brief 从叶子节点读取键值对
     */
    bool readLeafEntries(Page* page, QVector<KeyValuePair>& entries);  // 从叶子节点读取键值对

    /**
     * @brief 写入叶子节点键值对
     */
    bool writeLeafEntries(Page* page, const QVector<KeyValuePair>& entries);  // 写入叶子节点键值对

    /**
     * @brief 从内部节点读取条目
     */
    bool readInternalEntries(Page* page, QVector<InternalEntry>& entries, PageId& firstChild);  // 从内部节点读取条目

    /**
     * @brief 写入内部节点条目
     */
    bool writeInternalEntries(Page* page, const QVector<InternalEntry>& entries, PageId firstChild);  // 写入内部节点条目

    /**
     * @brief 在叶子节点中查找键的位置
     */
    int findKeyPositionInLeaf(const QVector<KeyValuePair>& entries, const QByteArray& key);  // 在叶子节点中查找键的位置

    /**
     * @brief 在内部节点中查找子节点位置
     */
    int findChildPosition(const QVector<InternalEntry>& entries, const QByteArray& key);

    /**
     * @brief 递归打印树（调试用）
     */
    void printTreeRecursive(PageId pageId, int level) const;  // 递归打印树

    // ========== 删除操作相关辅助方法 ==========

    /**
     * @brief 检查节点是否下溢
     */
    bool isUnderflow(Page* page);

    /**
     * @brief 获取节点的最小键数
     */
    int getMinKeys(bool isLeaf);

    /**
     * @brief 从叶子节点删除键
     */
    bool deleteKeyFromLeaf(PageId leafPageId, const QByteArray& serializedKey);  // 从叶子节点删除键

    /**
     * @brief 从左兄弟借用（叶子节点）
     */
    bool borrowFromLeftSiblingLeaf(PageId nodePageId, PageId leftSiblingPageId,
                                   PageId parentPageId, int keyIndexInParent);  // 从左兄弟借用

    /**
     * @brief 从右兄弟借用（叶子节点）
     */
    bool borrowFromRightSiblingLeaf(PageId nodePageId, PageId rightSiblingPageId,
                                    PageId parentPageId, int keyIndexInParent);  // 从右兄弟借用

    /**
     * @brief 与左兄弟合并（叶子节点）
     */
    bool mergeWithLeftSiblingLeaf(PageId nodePageId, PageId leftSiblingPageId,
                                  PageId parentPageId);  // 与左兄弟合并

    /**
     * @brief 与右兄弟合并（叶子节点）
     */
    bool mergeWithRightSiblingLeaf(PageId nodePageId, PageId rightSiblingPageId,
                                   PageId parentPageId);  // 与右兄弟合并

    /**
     * @brief 从父节点删除键
     */
    bool deleteKeyFromInternal(PageId internalPageId, const QByteArray& serializedKey);  // 从父节点删除键

    /**
     * @brief 处理节点下溢（递归）
     */
    bool handleUnderflow(PageId nodePageId, PageId parentPageId);  // 处理节点下溢

    /**
     * @brief 获取兄弟节点页ID
     */
    bool getSiblings(PageId nodePageId, PageId parentPageId,
                    PageId& leftSiblingPageId, PageId& rightSiblingPageId,
                    int& keyIndexInParent);

    /**
     * @brief 更新根节点（如果根为空）
     */
    void updateRootIfEmpty();

    BufferPoolManager* bufferPoolManager_;  // 缓冲池管理器
    DataType keyType_;                      // 键的数据类型
    PageId rootPageId_;                     // 根节点页ID
    int maxKeysPerPage_;                    // 每页最多键数
    mutable QMutex mutex_;                  // 树级锁
};

} // namespace qindb

#endif // QINDB_GENERIC_BPLUSTREE_H
