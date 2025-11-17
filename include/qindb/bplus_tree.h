#ifndef QINDB_BPLUS_TREE_H  // 防止重复包含该头文件
#define QINDB_BPLUS_TREE_H

#include "common.h"        // 包含通用定义和类型
#include "page.h"          // 包含页面相关的定义
#include "buffer_pool_manager.h"  // 包含缓冲池管理器相关定义
#include <QVector>         // Qt的动态数组容器
#include <QPair>           // Qt的键值对容器
#include <functional>      // 函数对象相关头文件

namespace qindb {          // 命名空间，避免命名冲突

/**
 * @brief B+ 树节点类型枚举
 * 定义了B+树中可能出现的节点类型
 */
enum class BPlusTreeNodeType : uint8_t {
    INVALID = 0,      // 无效节点类型
    INTERNAL_NODE,  // 内部节点（索引节点）
    LEAF_NODE       // 叶子节点（数据节点）
};

/**
 * @brief B+ 树页头部结构体
 *
 * B+ 树页布局:
 * +-------------------+
 * |BPlusTreePageHeader| 48 字节
 * +-------------------+
 * | Keys & Values     | 8144 字节
 * +-------------------+
 * Total: 8192 字节
 */
#pragma pack(push, 1)  // 设置1字节对齐，确保结构体紧凑排列
struct BPlusTreePageHeader {
    BPlusTreeNodeType nodeType;    // 节点类型 (1 字节)
    uint8_t reserved1;              // 保留 (1 字节)
    uint16_t numKeys;               // 键的数量 (2 字节)
    uint16_t maxKeys;               // 最大键数量 (2 字节)
    uint16_t reserved2;             // 保留 (2 字节)
    PageId pageId;                  // 当前页ID (4 字节)
    PageId parentPageId;            // 父节点页ID (4 字节)
    PageId nextPageId;              // 下一个叶子节点页ID（仅叶子节点使用） (4 字节)
    PageId prevPageId;              // 上一个叶子节点页ID（仅叶子节点使用） (4 字节)
    uint64_t reserved3;             // 保留 (8 字节)
    uint64_t reserved4;             // 保留 (8 字节)
    uint64_t reserved5;             // 保留 (8 字节)

    BPlusTreePageHeader()
        : nodeType(BPlusTreeNodeType::INVALID)
        , reserved1(0)
        , numKeys(0)
        , maxKeys(0)
        , reserved2(0)
        , pageId(INVALID_PAGE_ID)
        , parentPageId(INVALID_PAGE_ID)
        , nextPageId(INVALID_PAGE_ID)
        , prevPageId(INVALID_PAGE_ID)
        , reserved3(0)
        , reserved4(0)
        , reserved5(0)
    {}
};
#pragma pack(pop)  // 恢复默认对齐方式

static_assert(sizeof(BPlusTreePageHeader) == 48, "BPlusTreePageHeader must be 48 bytes");  // 编译时断言，确保结构体大小正确

/**
 * @brief B+ 树键值对（用于整数键）
 */
struct BPlusTreeEntry {
    int64_t key;        // 键（8字节整数）
    RowId value;        // 值（行ID，8字节）

    BPlusTreeEntry() : key(0), value(INVALID_ROW_ID) {}
    BPlusTreeEntry(int64_t k, RowId v) : key(k), value(v) {}

    bool operator<(const BPlusTreeEntry& other) const {
        return key < other.key;
    }
    bool operator==(const BPlusTreeEntry& other) const {
        return key == other.key;
    }
};

/**
 * @brief B+ 树内部节点条目
 */
struct BPlusTreeInternalEntry {
    int64_t key;        // 分隔键
    PageId childPageId; // 子节点页ID

    BPlusTreeInternalEntry() : key(0), childPageId(INVALID_PAGE_ID) {}
    BPlusTreeInternalEntry(int64_t k, PageId p) : key(k), childPageId(p) {}
};

/**
 * @brief B+ 树索引
 *
 * 特性：
 * - 支持整数键（int64_t）
 * - 自平衡
 * - 所有数据存储在叶子节点
 * - 叶子节点通过双向链表连接，支持范围查询
 * - 支持并发访问（页级锁）
 */
class BPlusTree {
public:
    /**
     * @brief 构造函数
     * @param bufferPoolManager 缓冲池管理器
     * @param rootPageId 根节点页ID（如果是新索引则为 INVALID_PAGE_ID）
     * @param order B+ 树的阶（每个节点最多的键数）
     */
    BPlusTree(BufferPoolManager* bufferPoolManager,
              PageId rootPageId = INVALID_PAGE_ID,
              int order = 200);

    ~BPlusTree();

    /**
     * @brief 插入键值对
     * @param key 键
     * @param value 值（行ID）
     * @return 是否成功
     */
    bool insert(int64_t key, RowId value);

    /**
     * @brief 删除键
     * @param key 要删除的键
     * @return 是否成功
     */
    bool remove(int64_t key);

    /**
     * @brief 查找键对应的值
     * @param key 要查找的键
     * @param value 输出参数，查找到的值
     * @return 是否找到
     */
    bool search(int64_t key, RowId& value);

    /**
     * @brief 范围查询
     * @param minKey 最小键（包含）
     * @param maxKey 最大键（包含）
     * @param results 输出参数，查找结果
     * @return 是否成功
     */
    bool rangeSearch(int64_t minKey, int64_t maxKey, QVector<BPlusTreeEntry>& results);

    /**
     * @brief 获取根节点页ID
     */
    PageId getRootPageId() const { return rootPageId_; }

    /**
     * @brief 获取索引统计信息
     */
    struct Stats {
        size_t numKeys;         // 总键数
        size_t numLeafPages;    // 叶子节点页数
        size_t numInternalPages;// 内部节点页数
        int treeHeight;         // 树高度
    };

    Stats getStats() const;

    /**
     * @brief 打印树结构（调试用）
     */
    void printTree() const;

private:
    /**
     * @brief 查找叶子节点
     * @param key 键
     * @return 包含该键的叶子节点页ID
     */
    PageId findLeafPage(int64_t key);

    /**
     * @brief 在叶子节点中插入
     * @param leafPageId 叶子节点页ID
     * @param key 键
     * @param value 值
     * @return 是否成功
     */
    bool insertIntoLeaf(PageId leafPageId, int64_t key, RowId value);

    /**
     * @brief 分裂叶子节点
     * @param leafPageId 要分裂的叶子节点页ID
     * @param newLeafPageId 输出参数，新叶子节点页ID
     * @param middleKey 输出参数，分裂后的中间键
     * @return 是否成功
     */
    bool splitLeafNode(PageId leafPageId, PageId& newLeafPageId, int64_t& middleKey);

    /**
     * @brief 分裂内部节点
     * @param internalPageId 要分裂的内部节点页ID
     * @param newInternalPageId 输出参数，新内部节点页ID
     * @param middleKey 输出参数，分裂后的中间键
     * @return 是否成功
     */
    bool splitInternalNode(PageId internalPageId, PageId& newInternalPageId, int64_t& middleKey);

    /**
     * @brief 在父节点中插入
     * @param parentPageId 父节点页ID
     * @param key 键
     * @param leftPageId 左子节点页ID
     * @param rightPageId 右子节点页ID
     * @return 是否成功
     */
    bool insertIntoParent(PageId parentPageId, int64_t key, PageId leftPageId, PageId rightPageId);

    /**
     * @brief 创建新根节点
     * @param leftPageId 左子节点页ID
     * @param key 分隔键
     * @param rightPageId 右子节点页ID
     * @return 新根节点页ID
     */
    PageId createNewRoot(PageId leftPageId, int64_t key, PageId rightPageId);

    /**
     * @brief 在叶子节点中查找键的位置
     * @param page 叶子节点页
     * @param key 键
     * @return 键的位置（如果不存在，返回应该插入的位置）
     */
    int findKeyPositionInLeaf(Page* page, int64_t key);

    /**
     * @brief 在内部节点中查找键的位置
     * @param page 内部节点页
     * @param key 键
     * @return 子节点的位置
     */
    int findKeyPositionInInternal(Page* page, int64_t key);

    /**
     * @brief 从叶子节点读取条目
     * @param page 叶子节点页
     * @param entries 输出参数，读取的条目
     */
    void readLeafEntries(Page* page, QVector<BPlusTreeEntry>& entries);

    /**
     * @brief 写入叶子节点条目
     * @param page 叶子节点页
     * @param entries 要写入的条目
     */
    void writeLeafEntries(Page* page, const QVector<BPlusTreeEntry>& entries);

    /**
     * @brief 从内部节点读取条目
     * @param page 内部节点页
     * @param entries 输出参数，读取的条目
     * @param firstChild 输出参数，第一个子节点页ID
     */
    void readInternalEntries(Page* page, QVector<BPlusTreeInternalEntry>& entries, PageId& firstChild);

    /**
     * @brief 写入内部节点条目
     * @param page 内部节点页
     * @param entries 要写入的条目
     * @param firstChild 第一个子节点页ID
     */
    void writeInternalEntries(Page* page, const QVector<BPlusTreeInternalEntry>& entries, PageId firstChild);

    /**
     * @brief 递归打印树（调试用）
     */
    void printTreeRecursive(PageId pageId, int level) const;

    BufferPoolManager* bufferPoolManager_;  // 缓冲池管理器
    PageId rootPageId_;                     // 根节点页ID
    int order_;                             // B+ 树的阶
    mutable QMutex mutex_;                  // 树级锁（简化实现）
};

} // namespace qindb

#endif // QINDB_BPLUS_TREE_H
