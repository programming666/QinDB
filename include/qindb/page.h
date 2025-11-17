#ifndef QINDB_PAGE_H  // 防止头文件重复包含
#define QINDB_PAGE_H

#include "common.h"  // 引入公共定义
#include <QMutex>    // Qt互斥锁，用于线程同步
#include <cstring>   // C字符串操作
#include <atomic>    // 原子操作，用于线程安全的计数器

namespace qindb {  // 定义qindb命名空间

/**
 * @brief 页类型枚举
 * 定义了数据库中所有可能的页面类型
 */
enum class PageType : uint8_t {
    INVALID = 0,          // 无效页，用于初始化或错误情况
    HEADER_PAGE = 1,      // 数据库头部页（固定为页面0），存储数据库全局信息
    METADATA_PAGE = 2,    // 元数据页（存储表定义、索引定义）
    TABLE_PAGE = 3,       // 表数据页
    INDEX_LEAF_PAGE = 4,  // B+树叶子页
    INDEX_INTERNAL_PAGE = 5, // B+树内部页
    HASH_BUCKET_PAGE = 6, // 哈希索引桶页
    TRIE_NODE_PAGE = 7,   // TRIE树节点页
    INVERTED_INDEX_PAGE = 8, // 倒排索引页
    RTREE_NODE_PAGE = 9,  // R-树节点页
    FREELIST_PAGE = 10,   // 空闲页列表
    OVERFLOW_PAGE = 11,   // 溢出页（超大记录）
    FREE_PAGE = 255       // 空闲页
};

/**
 * @brief 页头结构（每个页的元数据）
 *
 * 页布局:
 * +-------------------+
 * | PageHeader        |  32 字节
 * +-------------------+
 * | Page Data         |  8160 字节
 * +-------------------+
 * Total: 8192 字节
 */
#pragma pack(push, 1)
struct PageHeader {
    PageType pageType;        // 页类型 (1 字节)
    uint8_t reserved1;        // 保留 (1 字节)
    uint16_t slotCount;       // 槽位数量 (2 字节)
    uint16_t freeSpaceOffset; // 空闲空间偏移 (2 字节)
    uint16_t freeSpaceSize;   // 空闲空间大小 (2 字节)
    PageId pageId;            // 页ID (4 字节)
    PageId nextPageId;        // 下一页ID (4 字节)
    PageId prevPageId;        // 上一页ID (4 字节)
    TransactionId lastModifiedTxnId; // 最后修改事务ID (8 字节)
    uint32_t checksum;        // 校验和 (4 字节)

    PageHeader()
        : pageType(PageType::INVALID)
        , reserved1(0)
        , slotCount(0)
        , freeSpaceOffset(sizeof(PageHeader))
        , freeSpaceSize(PAGE_SIZE - sizeof(PageHeader))
        , pageId(INVALID_PAGE_ID)
        , nextPageId(INVALID_PAGE_ID)
        , prevPageId(INVALID_PAGE_ID)
        , lastModifiedTxnId(INVALID_TXN_ID)
        , checksum(0)
    {}
};
#pragma pack(pop)

static_assert(sizeof(PageHeader) == 32, "PageHeader must be 32 bytes");

/**
 * @brief 数据库页
 *
 * 每个页固定大小为 PAGE_SIZE (8192 字节)
 * 页是数据库系统中最小的I/O单位
 */
class Page {
public:
    Page();
    ~Page();

    /**
     * @brief 获取页数据指针
     */
    char* getData() { return data_; }
    const char* getData() const { return data_; }

    /**
     * @brief 获取页ID
     */
    PageId getPageId() const;

    /**
     * @brief 设置页ID
     */
    void setPageId(PageId pageId);

    /**
     * @brief 获取页类型
     */
    PageType getPageType() const;

    /**
     * @brief 设置页类型
     */
    void setPageType(PageType type);

    /**
     * @brief 获取下一页ID
     */
    PageId getNextPageId() const;

    /**
     * @brief 设置下一页ID
     */
    void setNextPageId(PageId nextPageId);

    /**
     * @brief 获取上一页ID
     */
    PageId getPrevPageId() const;

    /**
     * @brief 设置上一页ID
     */
    void setPrevPageId(PageId prevPageId);

    /**
     * @brief 获取页头
     */
    PageHeader* getHeader() { return reinterpret_cast<PageHeader*>(data_); }
    const PageHeader* getHeader() const { return reinterpret_cast<const PageHeader*>(data_); }

    /**
     * @brief 重置页（清空数据）
     */
    void reset();

    /**
     * @brief 获取引用计数（用于缓冲池管理）
     */
    int getPinCount() const { return pinCount_.load(); }

    /**
     * @brief 增加引用计数
     */
    void pin() { pinCount_++; }

    /**
     * @brief 减少引用计数
     */
    void unpin() {
        if (pinCount_ > 0) {
            pinCount_--;
        }
    }

    /**
     * @brief 是否为脏页
     */
    bool isDirty() const { return isDirty_.load(); }

    /**
     * @brief 标记为脏页
     */
    void setDirty(bool dirty) { isDirty_.store(dirty); }

    /**
     * @brief 获取页的读写锁
     */
    QMutex& getMutex() { return mutex_; }

    /**
     * @brief 计算页的校验和
     */
    uint32_t calculateChecksum() const;

    /**
     * @brief 验证页的校验和
     */
    bool verifyChecksum() const;

    /**
     * @brief 更新校验和
     */
    void updateChecksum();

private:
    char data_[PAGE_SIZE];           // 页数据
    std::atomic<int> pinCount_;      // 引用计数（正在使用该页的线程数）
    std::atomic<bool> isDirty_;      // 脏页标志
    QMutex mutex_;                   // 页级锁（用于并发控制）
};

/**
 * @brief 数据库头部结构（存储在页面0）
 *
 * 数据库文件的第一个页面（8KB）固定为头部页
 * 包含数据库的全局元数据
 */
#pragma pack(push, 1)
struct DatabaseHeader {
    // === 魔数和版本信息 (12 bytes) ===
    uint32_t magic;              // 魔数: 0x51494E44 ("QIND")
    uint16_t versionMajor;       // 主版本号
    uint16_t versionMinor;       // 次版本号
    uint16_t versionPatch;       // 补丁版本号
    uint16_t pageSize;           // 页大小（固定8192）

    // === 页面管理信息 (16 bytes) ===
    uint64_t totalPages;         // 总页数
    PageId metadataRootPageId;   // 元数据B+树根页ID
    PageId freePageListHead;     // 空闲页链表头

    // === 时间戳 (16 bytes) ===
    uint64_t createdAt;          // 创建时间戳（Unix微秒）
    uint64_t modifiedAt;         // 最后修改时间戳

    // === 数据库名称 (256 bytes) ===
    char databaseName[256];      // 数据库名称（UTF-8）

    // === 校验和 (4 bytes) ===
    uint32_t checksum;           // CRC32校验和（不包括此字段本身）

    // === 保留空间 (7888 bytes) ===
    char reserved[7888];         // 保留字段，填充到8KB

    DatabaseHeader()
        : magic(0x51494E44)      // "QIND"
        , versionMajor(QINDB_VERSION_MAJOR)
        , versionMinor(QINDB_VERSION_MINOR)
        , versionPatch(QINDB_VERSION_PATCH)
        , pageSize(PAGE_SIZE)
        , totalPages(1)
        , metadataRootPageId(INVALID_PAGE_ID)
        , freePageListHead(INVALID_PAGE_ID)
        , createdAt(0)
        , modifiedAt(0)
        , checksum(0)
    {
        std::memset(databaseName, 0, sizeof(databaseName));
        std::memset(reserved, 0, sizeof(reserved));
    }

    /**
     * @brief 计算校验和（CRC32）
     */
    uint32_t calculateChecksum() const;

    /**
     * @brief 验证校验和
     */
    bool verifyChecksum() const;

    /**
     * @brief 更新校验和
     */
    void updateChecksum();
};
#pragma pack(pop)

static_assert(sizeof(DatabaseHeader) == PAGE_SIZE, "DatabaseHeader must be exactly PAGE_SIZE (8192 bytes)");

} // namespace qindb

#endif // QINDB_PAGE_H
