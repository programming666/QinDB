#include "qindb/disk_manager.h"
#include "qindb/logger.h"
#include <QFileInfo>

namespace qindb {

DiskManager::DiskManager(const QString& dbFile)
    : dbFileName_(dbFile)
    , dbFile_(dbFile)
    , numPages_(0)
    , nextPageId_(1)  // 页ID从1开始（0是无效页）
{
    LOG_INFO(QString("Initializing DiskManager for file: %1").arg(dbFile));

    // 检查文件是否存在
    QFileInfo fileInfo(dbFile);
    bool fileExists = fileInfo.exists();

    // 打开文件（读写模式）
    if (!dbFile_.open(QIODevice::ReadWrite)) {
        LOG_ERROR(QString("Failed to open database file: %1").arg(dbFile));
        return;
    }

    if (fileExists) {
        // 文件已存在，计算页数
        // 前8字节是魔数，之后是页面数据
        qint64 fileSize = dbFile_.size();
        if (fileSize >= 8) {
            numPages_ = static_cast<size_t>((fileSize - 8) / PAGE_SIZE);
        } else {
            numPages_ = 0;
        }
        nextPageId_ = static_cast<PageId>(numPages_ + 1);

        LOG_INFO(QString("Opened existing database file with %1 pages").arg(numPages_));
    } else {
        // 新文件，初始化
        if (!initializeFile()) {
            LOG_ERROR("Failed to initialize database file");
            dbFile_.close();
            return;
        }

        LOG_INFO("Created new database file");
    }
}

DiskManager::~DiskManager() {
    close();
}

bool DiskManager::readPage(PageId pageId, Page* page) {
    QMutexLocker locker(&mutex_);

    if (pageId == INVALID_PAGE_ID || pageId > numPages_) {
        LOG_ERROR(QString("Invalid page ID: %1 (total pages: %2)").arg(pageId).arg(numPages_));
        return false;
    }

    if (!dbFile_.isOpen()) {
        LOG_ERROR("Database file is not open");
        return false;
    }

    // 计算页在文件中的偏移
    // 前8字节保留给魔数，所以页面从offset=8开始
    qint64 offset = 8 + static_cast<qint64>(pageId - 1) * PAGE_SIZE;

    // 移动到指定位置
    if (!dbFile_.seek(offset)) {
        LOG_ERROR(QString("Failed to seek to page %1").arg(pageId));
        return false;
    }

    // 读取页数据
    qint64 bytesRead = dbFile_.read(page->getData(), PAGE_SIZE);
    if (bytesRead != PAGE_SIZE) {
        LOG_ERROR(QString("Failed to read page %1: expected %2 bytes, got %3")
                      .arg(pageId).arg(PAGE_SIZE).arg(bytesRead));
        return false;
    }

    // 验证校验和
    if (!page->verifyChecksum()) {
        LOG_WARN(QString("Checksum mismatch for page %1").arg(pageId));
        // 继续使用，但记录警告
    }

    LOG_DEBUG(QString("Read page %1 successfully").arg(pageId));
    return true;
}

bool DiskManager::writePage(PageId pageId, const Page* page) {
    QMutexLocker locker(&mutex_);

    if (pageId == INVALID_PAGE_ID) {
        LOG_ERROR("Invalid page ID for write");
        return false;
    }

    if (!dbFile_.isOpen()) {
        LOG_ERROR("Database file is not open");
        return false;
    }

    // 如果页ID超出当前范围，扩展文件
    if (pageId > numPages_) {
        size_t pagesToExtend = pageId - numPages_;
        if (!extendFile(pagesToExtend)) {
            LOG_ERROR(QString("Failed to extend file for page %1").arg(pageId));
            return false;
        }
    }

    // 计算页在文件中的偏移
    // 前8字节保留给魔数，所以页面从offset=8开始
    qint64 offset = 8 + static_cast<qint64>(pageId - 1) * PAGE_SIZE;

    // 移动到指定位置
    if (!dbFile_.seek(offset)) {
        LOG_ERROR(QString("Failed to seek to page %1 for writing").arg(pageId));
        return false;
    }

    // 写入页数据
    qint64 bytesWritten = dbFile_.write(page->getData(), PAGE_SIZE);
    if (bytesWritten != PAGE_SIZE) {
        LOG_ERROR(QString("Failed to write page %1: expected %2 bytes, wrote %3")
                      .arg(pageId).arg(PAGE_SIZE).arg(bytesWritten));
        return false;
    }

    // 刷新到磁盘以确保数据持久化
    dbFile_.flush();

    LOG_DEBUG(QString("Wrote page %1 successfully").arg(pageId));
    return true;
}

PageId DiskManager::allocatePage() {
    QMutexLocker locker(&mutex_);

    // 简单策略：总是分配新页
    PageId newPageId = nextPageId_++;

    // 扩展文件
    if (newPageId > numPages_) {
        if (!extendFile(1)) {
            LOG_ERROR("Failed to extend file for new page");
            nextPageId_--;
            return INVALID_PAGE_ID;
        }
    }

    LOG_DEBUG(QString("Allocated new page: %1").arg(newPageId));
    return newPageId;
}

void DiskManager::deallocatePage(PageId pageId) {
    QMutexLocker locker(&mutex_);

    // TODO: 实现空闲页列表管理
    // 当前简单实现：不重用释放的页

    LOG_DEBUG(QString("Deallocated page: %1 (not yet reused)").arg(pageId));
}

size_t DiskManager::getNumPages() const {
    QMutexLocker locker(&mutex_);
    return numPages_;
}

void DiskManager::flush() {
    QMutexLocker locker(&mutex_);

    if (dbFile_.isOpen()) {
        dbFile_.flush();
        LOG_DEBUG("Flushed database file to disk");
    }
}

void DiskManager::close() {
    QMutexLocker locker(&mutex_);

    if (dbFile_.isOpen()) {
        dbFile_.flush();
        dbFile_.close();
        LOG_INFO(QString("Closed database file: %1").arg(dbFileName_));
    }
}

bool DiskManager::isOpen() const {
    QMutexLocker locker(&mutex_);
    return dbFile_.isOpen();
}

bool DiskManager::extendFile(size_t numPages) {
    // 注意：此函数假设已经持有 mutex_

    // 创建空白页
    char emptyPage[PAGE_SIZE];
    std::memset(emptyPage, 0, PAGE_SIZE);

    // 移动到文件末尾
    if (!dbFile_.seek(dbFile_.size())) {
        LOG_ERROR("Failed to seek to end of file");
        return false;
    }

    // 写入空白页
    for (size_t i = 0; i < numPages; ++i) {
        qint64 bytesWritten = dbFile_.write(emptyPage, PAGE_SIZE);
        if (bytesWritten != PAGE_SIZE) {
            LOG_ERROR(QString("Failed to extend file: wrote %1 bytes, expected %2")
                          .arg(bytesWritten).arg(PAGE_SIZE));
            return false;
        }
        numPages_++;
    }

    dbFile_.flush();
    LOG_DEBUG(QString("Extended file by %1 pages (total: %2)").arg(numPages).arg(numPages_));
    return true;
}

bool DiskManager::initializeFile() {
    // 注意：此函数假设已经持有 mutex_

    // 首先写入8字节的魔数占位符（稍后通过writeMagicNumber设置实际值）
    uint64_t placeholderMagic = 0x0000000000000000ULL;
    qint64 bytesWritten = dbFile_.write(reinterpret_cast<const char*>(&placeholderMagic), 8);
    if (bytesWritten != 8) {
        LOG_ERROR("Failed to write magic number placeholder");
        return false;
    }

    // 创建文件头页（页0）
    Page headerPage;
    headerPage.setPageId(0);
    headerPage.setPageType(PageType::HEADER_PAGE);

    // 文件头页包含数据库元信息
    PageHeader* header = headerPage.getHeader();
    header->nextPageId = 1;  // 第一个可用的数据页

    headerPage.updateChecksum();

    // 写入文件头
    char emptyPage[PAGE_SIZE];
    std::memcpy(emptyPage, headerPage.getData(), PAGE_SIZE);

    bytesWritten = dbFile_.write(emptyPage, PAGE_SIZE);
    if (bytesWritten != PAGE_SIZE) {
        LOG_ERROR("Failed to write header page");
        return false;
    }

    numPages_ = 1;
    nextPageId_ = 1;

    dbFile_.flush();
    return true;
}

bool DiskManager::writeMagicNumber(bool catalogUseDb, bool walUseDb) {
    QMutexLocker locker(&mutex_);

    if (!dbFile_.isOpen()) {
        LOG_ERROR("Database file is not open");
        return false;
    }

    // 计算魔数
    uint64_t magic = calculateDbMagic(catalogUseDb, walUseDb);

    // 魔数写入文件开头（前8字节）
    if (!dbFile_.seek(0)) {
        LOG_ERROR("Failed to seek to file beginning for magic number");
        return false;
    }

    qint64 bytesWritten = dbFile_.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    if (bytesWritten != sizeof(magic)) {
        LOG_ERROR(QString("Failed to write magic number: wrote %1 bytes, expected %2")
                      .arg(bytesWritten).arg(sizeof(magic)));
        return false;
    }

    dbFile_.flush();

    LOG_INFO(QString("Wrote magic number: 0x%1 (Catalog=%2, WAL=%3)")
                 .arg(magic, 16, 16, QChar('0'))
                 .arg(catalogUseDb ? "DB" : "File")
                 .arg(walUseDb ? "DB" : "File"));

    return true;
}

bool DiskManager::readMagicNumber(uint64_t& magic) {
    QMutexLocker locker(&mutex_);

    if (!dbFile_.isOpen()) {
        LOG_ERROR("Database file is not open");
        return false;
    }

    // 读取文件开头的8字节
    if (!dbFile_.seek(0)) {
        LOG_ERROR("Failed to seek to file beginning for magic number");
        return false;
    }

    qint64 bytesRead = dbFile_.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (bytesRead != sizeof(magic)) {
        LOG_ERROR(QString("Failed to read magic number: read %1 bytes, expected %2")
                      .arg(bytesRead).arg(sizeof(magic)));
        return false;
    }

    LOG_DEBUG(QString("Read magic number: 0x%1").arg(magic, 16, 16, QChar('0')));
    return true;
}

bool DiskManager::verifyAndParseMagic(bool& catalogUseDb, bool& walUseDb) {
    uint64_t magic = 0;

    if (!readMagicNumber(magic)) {
        LOG_WARN("Failed to read magic number, assuming new database");
        return false;
    }

    // 验证魔数
    if (!isValidDbMagic(magic)) {
        LOG_ERROR(QString("Invalid database magic number: 0x%1").arg(magic, 16, 16, QChar('0')));
        return false;
    }

    // 解析模式
    parseDbMagic(magic, catalogUseDb, walUseDb);

    LOG_INFO(QString("Parsed database mode from magic: Catalog=%1, WAL=%2")
                 .arg(catalogUseDb ? "DB" : "File")
                 .arg(walUseDb ? "DB" : "File"));

    return true;
}

} // namespace qindb
