#include "qindb/wal_db_backend.h"
#include "qindb/system_tables.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include "qindb/table_page.h"
#include "qindb/logger.h"
#include <QDataStream>

namespace qindb {

WalDbBackend::WalDbBackend(BufferPoolManager* bufferPool, DiskManager* diskManager)
    : bufferPool_(bufferPool)
    , diskManager_(diskManager)
    , sysWalLogsFirstPage_(INVALID_PAGE_ID)
    , sysWalMetaFirstPage_(INVALID_PAGE_ID)
{
}

WalDbBackend::~WalDbBackend() {
}

bool WalDbBackend::initialize() {
    if (!bufferPool_ || !diskManager_) {
        LOG_ERROR("Invalid buffer pool or disk manager");
        return false;
    }

    // 检查系统表是否已存在
    if (systemTablesExist()) {
        LOG_INFO("WAL system tables already exist");
        return true;
    }

    // 创建系统表
    return createSystemTables();
}

bool WalDbBackend::systemTablesExist() {
    // 检查页面4和5（预留给WAL系统表）
    // 首先检查磁盘管理器中是否有足够的页面
    if (diskManager_->getNumPages() < 5) {
        // 页面4和5还不存在
        return false;
    }

    // 尝试获取页面4并检查其类型
    Page* page = bufferPool_->fetchPage(4);
    if (!page) {
        return false;
    }

    bool exists = (page->getPageType() == PageType::TABLE_PAGE);
    bufferPool_->unpinPage(4, false);

    return exists;
}

bool WalDbBackend::createSystemTables() {
    LOG_INFO("Creating system tables for WAL storage");

    // 创建sys_wal_logs表的第一个页面
    PageId sysWalLogsPageId = INVALID_PAGE_ID;
    Page* sysWalLogsPage = bufferPool_->newPage(&sysWalLogsPageId);
    if (!sysWalLogsPage) {
        LOG_ERROR("Failed to create sys_wal_logs page");
        return false;
    }
    sysWalLogsPage->setPageType(PageType::TABLE_PAGE);
    TablePage::initialize(sysWalLogsPage);
    sysWalLogsFirstPage_ = sysWalLogsPageId;
    bufferPool_->unpinPage(sysWalLogsPageId, true);

    // 创建sys_wal_meta表的第一个页面
    PageId sysWalMetaPageId = INVALID_PAGE_ID;
    Page* sysWalMetaPage = bufferPool_->newPage(&sysWalMetaPageId);
    if (!sysWalMetaPage) {
        LOG_ERROR("Failed to create sys_wal_meta page");
        return false;
    }
    sysWalMetaPage->setPageType(PageType::TABLE_PAGE);
    TablePage::initialize(sysWalMetaPage);
    sysWalMetaFirstPage_ = sysWalMetaPageId;
    bufferPool_->unpinPage(sysWalMetaPageId, true);

    // 初始化current_lsn为0
    if (!setCurrentLSN(0)) {
        LOG_ERROR("Failed to initialize current LSN");
        return false;
    }

    LOG_INFO(QString("WAL system tables created: sys_wal_logs=%1, sys_wal_meta=%2")
        .arg(sysWalLogsFirstPage_)
        .arg(sysWalMetaFirstPage_));

    return true;
}

bool WalDbBackend::writeRecord(const WALRecord& record) {
    // 确保系统表已初始化
    if (sysWalLogsFirstPage_ == INVALID_PAGE_ID) {
        LOG_ERROR("WAL system tables not initialized");
        return false;
    }

    // 序列化WAL记录
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    stream << static_cast<qint64>(record.header.lsn);
    stream << static_cast<qint32>(record.header.type);
    stream << static_cast<qint64>(record.header.txnId);
    stream << static_cast<qint64>(record.header.checksum);
    stream << static_cast<qint32>(record.header.dataSize);
    stream << record.data;

    // 插入到sys_wal_logs表
    Page* page = bufferPool_->fetchPage(sysWalLogsFirstPage_);
    if (!page) {
        LOG_ERROR("Failed to fetch sys_wal_logs page");
        return false;
    }

    RowId rowId = 1;
    bool success = TablePage::insertTuple(page, data, &rowId);
    bufferPool_->unpinPage(sysWalLogsFirstPage_, true);

    if (!success) {
        LOG_ERROR("Failed to insert WAL record");
        return false;
    }

    LOG_DEBUG(QString("WAL record written to DB: LSN=%1, Type=%2, TxnID=%3")
        .arg(record.header.lsn)
        .arg(static_cast<int>(record.header.type))
        .arg(record.header.txnId));

    return true;
}

bool WalDbBackend::readAllRecords(QVector<WALRecord>& records) {
    // 确保系统表已初始化
    if (sysWalLogsFirstPage_ == INVALID_PAGE_ID) {
        LOG_ERROR("WAL system tables not initialized");
        return false;
    }

    records.clear();

    Page* page = bufferPool_->fetchPage(sysWalLogsFirstPage_);
    if (!page) {
        LOG_ERROR("Failed to fetch sys_wal_logs page");
        return false;
    }

    // 遍历所有元组
    uint16_t slotCount = TablePage::getSlotCount(page);

    for (uint16_t i = 0; i < slotCount; ++i) {
        QByteArray tupleData;
        if (!TablePage::getTuple(page, i, tupleData)) {
            continue;  // 跳过已删除的槽位
        }

        // 反序列化WAL记录
        QDataStream stream(tupleData);
        qint64 lsn, txnId, checksum;
        qint32 type, dataSize;
        QByteArray data;

        stream >> lsn >> type >> txnId >> checksum >> dataSize >> data;

        WALRecord record;
        record.header.lsn = static_cast<uint64_t>(lsn);
        record.header.type = static_cast<WALRecordType>(type);
        record.header.txnId = static_cast<TransactionId>(txnId);
        record.header.checksum = static_cast<uint32_t>(checksum);
        record.header.dataSize = static_cast<uint16_t>(dataSize);
        record.data = data;

        records.append(record);
    }

    bufferPool_->unpinPage(sysWalLogsFirstPage_, false);

    LOG_INFO(QString("Read %1 WAL records from database").arg(records.size()));

    return true;
}

uint64_t WalDbBackend::getCurrentLSN() {
    uint64_t lsn = 0;
    if (!getMetaValue(WalMetaKeys::CURRENT_LSN, lsn)) {
        LOG_WARN("Failed to get current LSN, returning 0");
        return 0;
    }
    return lsn;
}

bool WalDbBackend::setCurrentLSN(uint64_t lsn) {
    return setMetaValue(WalMetaKeys::CURRENT_LSN, lsn);
}

bool WalDbBackend::flush() {
    // 对于数据库后端，刷新意味着将所有脏页写回磁盘
    bufferPool_->flushAllPages();
    LOG_DEBUG("WAL database backend flushed");
    return true;
}

bool WalDbBackend::truncate() {
    LOG_INFO("Truncating WAL logs");

    // 确保系统表已初始化
    if (sysWalLogsFirstPage_ == INVALID_PAGE_ID) {
        LOG_ERROR("WAL system tables not initialized");
        return false;
    }

    return clearWalLogs();
}

bool WalDbBackend::getMetaValue(const QString& key, uint64_t& value) {
    // 确保系统表已初始化
    if (sysWalMetaFirstPage_ == INVALID_PAGE_ID) {
        LOG_ERROR("WAL system tables not initialized");
        return false;
    }

    Page* page = bufferPool_->fetchPage(sysWalMetaFirstPage_);
    if (!page) {
        LOG_ERROR("Failed to fetch sys_wal_meta page");
        return false;
    }

    // 遍历所有元组查找匹配的key
    uint16_t slotCount = TablePage::getSlotCount(page);
    bool found = false;

    for (uint16_t i = 0; i < slotCount; ++i) {
        QByteArray tupleData;
        if (!TablePage::getTuple(page, i, tupleData)) {
            continue;
        }

        QDataStream stream(tupleData);
        QString storedKey;
        qint64 storedValue;

        stream >> storedKey >> storedValue;

        if (storedKey == key) {
            value = static_cast<uint64_t>(storedValue);
            found = true;
            break;
        }
    }

    bufferPool_->unpinPage(sysWalMetaFirstPage_, false);

    if (!found) {
        LOG_DEBUG(QString("Meta key '%1' not found").arg(key));
        value = 0;
    }

    return true;
}

bool WalDbBackend::setMetaValue(const QString& key, uint64_t value) {
    // 确保系统表已初始化
    if (sysWalMetaFirstPage_ == INVALID_PAGE_ID) {
        LOG_ERROR("WAL system tables not initialized");
        return false;
    }

    Page* page = bufferPool_->fetchPage(sysWalMetaFirstPage_);
    if (!page) {
        LOG_ERROR("Failed to fetch sys_wal_meta page");
        return false;
    }

    // 查找是否已存在该key
    uint16_t slotCount = TablePage::getSlotCount(page);
    uint16_t existingSlot = UINT16_MAX;

    for (uint16_t i = 0; i < slotCount; ++i) {
        QByteArray tupleData;
        if (!TablePage::getTuple(page, i, tupleData)) {
            continue;
        }

        QDataStream stream(tupleData);
        QString storedKey;
        stream >> storedKey;

        if (storedKey == key) {
            existingSlot = i;
            break;
        }
    }

    // 如果存在，先删除旧记录（逻辑删除）
    if (existingSlot != UINT16_MAX) {
        TablePage::deleteRecord(page, existingSlot);
    }

    // 插入新记录
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << key;
    stream << static_cast<qint64>(value);

    RowId rowId = 1;
    bool success = TablePage::insertTuple(page, data, &rowId);

    bufferPool_->unpinPage(sysWalMetaFirstPage_, true);

    if (success) {
        LOG_DEBUG(QString("Set meta value: %1=%2").arg(key).arg(value));
    } else {
        LOG_ERROR(QString("Failed to set meta value: %1").arg(key));
    }

    return success;
}

bool WalDbBackend::clearWalLogs() {
    Page* page = bufferPool_->fetchPage(sysWalLogsFirstPage_);
    if (!page) {
        LOG_ERROR("Failed to fetch sys_wal_logs page for clearing");
        return false;
    }

    // 重新初始化页面，清除所有数据
    TablePage::initialize(page);
    bufferPool_->unpinPage(sysWalLogsFirstPage_, true);

    LOG_INFO("WAL logs cleared");
    return true;
}

} // namespace qindb
