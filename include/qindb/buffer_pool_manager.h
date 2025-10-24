#ifndef QINDB_BUFFER_POOL_MANAGER_H
#define QINDB_BUFFER_POOL_MANAGER_H

#include "common.h"
#include "page.h"
#include "disk_manager.h"
#include <QMutex>
#include <QHash>
#include <memory>
#include <list>

namespace qindb {

/**
 * @brief 缓冲池帧（Buffer Pool Frame）
 *
 * 缓冲池中的每个槽位，包含一个页和相关元数据
 */
struct Frame {
    Page* page;           // 页指针
    PageId pageId;        // 页ID
    bool isDirty;         // 脏页标志
    int pinCount;         // 引用计数
    size_t lastAccessTime; // 最后访问时间（用于替换策略）

    Frame()
        : page(nullptr)
        , pageId(INVALID_PAGE_ID)
        , isDirty(false)
        , pinCount(0)
        , lastAccessTime(0)
    {}
};

/**
 * @brief 缓冲池管理器
 *
 * 职责：
 * 1. 在内存中缓存页，减少磁盘I/O
 * 2. 管理页的分配和回收
 * 3. 实现页替换策略（LRU）
 * 4. 处理脏页的刷新
 * 5. 保证并发安全
 *
 * 替换策略：Clock (Second Chance) 算法
 */
class BufferPoolManager {
public:
    /**
     * @brief 构造函数
     * @param poolSize 缓冲池大小（页数）
     * @param diskManager 磁盘管理器
     */
    BufferPoolManager(size_t poolSize, DiskManager* diskManager);

    ~BufferPoolManager();

    /**
     * @brief 获取一个页（如果不在缓冲池中，从磁盘加载）
     * @param pageId 页ID
     * @return 页指针，失败返回 nullptr
     *
     * 注意：使用完毕后必须调用 unpinPage()
     */
    Page* fetchPage(PageId pageId);

    /**
     * @brief 创建一个新页
     * @param pageId 输出参数，新页的ID
     * @return 页指针，失败返回 nullptr
     */
    Page* newPage(PageId* pageId);

    /**
     * @brief 解除页的固定（减少引用计数）
     * @param pageId 页ID
     * @param isDirty 是否标记为脏页
     * @return 是否成功
     */
    bool unpinPage(PageId pageId, bool isDirty);

    /**
     * @brief 刷新页到磁盘
     * @param pageId 页ID
     * @return 是否成功
     */
    bool flushPage(PageId pageId);

    /**
     * @brief 刷新所有脏页到磁盘
     */
    void flushAllPages();

    /**
     * @brief 删除一个页
     * @param pageId 页ID
     * @return 是否成功
     */
    bool deletePage(PageId pageId);

    /**
     * @brief 获取缓冲池统计信息
     */
    struct Stats {
        size_t poolSize;        // 缓冲池大小
        size_t numPages;        // 当前页数
        size_t numDirtyPages;   // 脏页数
        size_t numPinnedPages;  // 被固定的页数
        size_t hitCount;        // 缓存命中次数
        size_t missCount;       // 缓存未命中次数
    };

    Stats getStats() const;

private:
    /**
     * @brief 找到一个可以替换的帧（Clock算法）
     * @return 帧索引，失败返回 -1
     */
    int findVictim();

    /**
     * @brief 从缓冲池中移除一个页
     * @param pageId 页ID
     * @return 是否成功
     */
    bool evictPage(PageId pageId);

    size_t poolSize_;                     // 缓冲池大小
    std::vector<Frame> frames_;           // 帧数组
    std::vector<Page*> pages_;            // 页对象池
    QHash<PageId, size_t> pageTable_;     // 页表：PageId -> Frame索引

    std::list<size_t> freeList_;          // 空闲帧列表
    size_t clockHand_;                    // Clock算法的时钟指针

    DiskManager* diskManager_;            // 磁盘管理器

    // 统计信息
    mutable size_t hitCount_;
    mutable size_t missCount_;

    mutable QMutex mutex_;                // 全局锁
};

} // namespace qindb

#endif // QINDB_BUFFER_POOL_MANAGER_H
