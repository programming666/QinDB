#ifndef QINDB_DISK_MANAGER_H
#define QINDB_DISK_MANAGER_H

#include "common.h"
#include "page.h"
#include <QFile>
#include <QMutex>
#include <memory>

namespace qindb {

/**
 * @brief 磁盘管理器
 *
 * 职责：
 * 1. 管理数据库文件的读写
 * 2. 分配和回收页
 * 3. 维护空闲页列表
 * 4. 确保数据持久化
 */
class DiskManager {
public:
    explicit DiskManager(const QString& dbFile);
    ~DiskManager();

    /**
     * @brief 读取一个页
     * @param pageId 页ID
     * @param page 输出参数，读取的页数据
     * @return 是否成功
     */
    bool readPage(PageId pageId, Page* page);

    /**
     * @brief 写入一个页
     * @param pageId 页ID
     * @param page 要写入的页数据
     * @return 是否成功
     */
    bool writePage(PageId pageId, const Page* page);

    /**
     * @brief 分配一个新页
     * @return 新页的ID，失败返回 INVALID_PAGE_ID
     */
    PageId allocatePage();

    /**
     * @brief 释放一个页
     * @param pageId 要释放的页ID
     */
    void deallocatePage(PageId pageId);

    /**
     * @brief 获取文件大小（页数）
     */
    size_t getNumPages() const;

    /**
     * @brief 刷新所有缓冲数据到磁盘
     */
    void flush();

    /**
     * @brief 关闭数据库文件
     */
    void close();

    /**
     * @brief 数据库文件是否打开
     */
    bool isOpen() const;

    /**
     * @brief 写入数据库魔数到文件头
     * @param catalogUseDb Catalog是否使用数据库模式
     * @param walUseDb WAL是否使用数据库模式
     * @return 是否成功
     */
    bool writeMagicNumber(bool catalogUseDb, bool walUseDb);

    /**
     * @brief 读取数据库魔数
     * @param magic 输出：魔数
     * @return 是否成功
     */
    bool readMagicNumber(uint64_t& magic);

    /**
     * @brief 验证数据库魔数并解析模式
     * @param catalogUseDb 输出：Catalog是否使用数据库模式
     * @param walUseDb 输出：WAL是否使用数据库模式
     * @return 是否成功（魔数有效）
     */
    bool verifyAndParseMagic(bool& catalogUseDb, bool& walUseDb);

private:
    /**
     * @brief 扩展文件大小
     * @param numPages 要扩展的页数
     */
    bool extendFile(size_t numPages);

    /**
     * @brief 初始化数据库文件（创建文件头）
     */
    bool initializeFile();

    QString dbFileName_;           // 数据库文件名
    QFile dbFile_;                 // 数据库文件对象
    size_t numPages_;              // 文件中的页数
    PageId nextPageId_;            // 下一个可分配的页ID
    mutable QMutex mutex_;         // 互斥锁（保护并发访问）
};

} // namespace qindb

#endif // QINDB_DISK_MANAGER_H
