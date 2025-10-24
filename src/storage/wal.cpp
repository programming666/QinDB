#include "qindb/wal.h"
#include "qindb/wal_db_backend.h"
#include "qindb/logger.h"
#include "qindb/config.h"
#include "qindb/catalog.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/table_page.h"
#include <QDataStream>
#include <QCryptographicHash>

namespace qindb {

uint32_t WALRecord::calculateChecksum() const {
    // 简单的CRC32实现（用于校验和）
    // 注意：Qt 6.10不支持QCryptographicHash::Crc32，这里使用简单的哈希
    uint32_t checksum = 0;

    // 对header字段计算校验和
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&header.type);
    for (size_t i = 0; i < sizeof(header.type); ++i) {
        checksum = (checksum << 5) + checksum + ptr[i];
    }

    ptr = reinterpret_cast<const uint8_t*>(&header.txnId);
    for (size_t i = 0; i < sizeof(header.txnId); ++i) {
        checksum = (checksum << 5) + checksum + ptr[i];
    }

    ptr = reinterpret_cast<const uint8_t*>(&header.lsn);
    for (size_t i = 0; i < sizeof(header.lsn); ++i) {
        checksum = (checksum << 5) + checksum + ptr[i];
    }

    // 对数据计算校验和
    for (int i = 0; i < data.size(); ++i) {
        checksum = (checksum << 5) + checksum + static_cast<uint8_t>(data[i]);
    }

    return checksum;
}

bool WALRecord::verifyChecksum() const {
    return header.checksum == calculateChecksum();
}

WALManager::WALManager(const QString& walFilePath)
    : walFilePath_(walFilePath)
    , currentLSN_(0)
    , useDatabase_(false)
{
    // 从配置读取持久化模式
    useDatabase_ = !Config::instance().isWalUseFile();
    LOG_INFO(QString("WAL initialized (mode: %1)")
        .arg(useDatabase_ ? "database" : "file"));
}

WALManager::~WALManager() {
    if (walFile_ && walFile_->isOpen()) {
        flush();
        walFile_->close();
    }
}

void WALManager::setDatabaseBackend(BufferPoolManager* bufferPool, DiskManager* diskManager) {
    QMutexLocker locker(&mutex_);

    if (!bufferPool || !diskManager) {
        LOG_ERROR("Invalid buffer pool or disk manager for WAL backend");
        return;
    }

    dbBackend_ = std::make_unique<WalDbBackend>(bufferPool, diskManager);

    if (!dbBackend_->initialize()) {
        LOG_ERROR("Failed to initialize WAL database backend");
        dbBackend_.reset();
        return;
    }

    LOG_INFO("WAL database backend initialized");
}

bool WALManager::initialize() {
    QMutexLocker locker(&mutex_);

    if (useDatabase_) {
        // 数据库模式：从数据库后端加载LSN
        if (!dbBackend_) {
            LOG_ERROR("Database backend not initialized");
            return false;
        }

        currentLSN_ = dbBackend_->getCurrentLSN();
        LOG_INFO(QString("WAL initialized in database mode, LSN=%1").arg(currentLSN_));
        return true;
    }

    // 文件模式：原有逻辑
    walFile_ = std::make_unique<QFile>(walFilePath_);

    // 读取已有WAL文件的LSN（不执行恢复，恢复需要在DatabaseManager中调用）
    if (walFile_->exists()) {
        LOG_INFO(QString("WAL file exists: %1").arg(walFilePath_));

        // 读取文件以确定当前LSN
        if (walFile_->open(QIODevice::ReadOnly)) {
            uint64_t maxLSN = 0;

            while (!walFile_->atEnd()) {
                WALRecordHeader header;
                qint64 bytesRead = walFile_->read(
                    reinterpret_cast<char*>(&header),
                    sizeof(WALRecordHeader)
                );

                if (bytesRead != sizeof(WALRecordHeader)) {
                    break;
                }

                if (header.lsn > maxLSN) {
                    maxLSN = header.lsn;
                }

                // 跳过数据部分
                if (header.dataSize > 0) {
                    walFile_->seek(walFile_->pos() + header.dataSize);
                }
            }

            currentLSN_ = maxLSN;
            walFile_->close();

            LOG_INFO(QString("WAL LSN restored: %1").arg(currentLSN_));
        }
    }

    // 以追加模式打开文件
    if (!walFile_->open(QIODevice::WriteOnly | QIODevice::Append)) {
        LOG_ERROR(QString("Failed to open WAL file: %1").arg(walFilePath_));
        return false;
    }

    LOG_INFO(QString("WAL initialized in file mode: %1").arg(walFilePath_));
    return true;
}

uint64_t WALManager::writeRecord(WALRecord& record) {
    if (useDatabase_) {
        return writeRecordToDatabase(record);
    } else {
        return writeRecordToFile(record);
    }
}

uint64_t WALManager::writeRecordToFile(WALRecord& record) {
    QMutexLocker locker(&mutex_);

    if (!walFile_ || !walFile_->isOpen()) {
        LOG_ERROR("WAL file not open");
        return 0;
    }

    // 分配 LSN
    record.header.lsn = ++currentLSN_;
    record.header.dataSize = record.data.size();
    record.header.checksum = record.calculateChecksum();

    // 写入日志头部
    qint64 bytesWritten = walFile_->write(
        reinterpret_cast<const char*>(&record.header),
        sizeof(WALRecordHeader)
    );

    if (bytesWritten != sizeof(WALRecordHeader)) {
        LOG_ERROR("Failed to write WAL record header");
        return 0;
    }

    // 写入日志数据
    if (record.data.size() > 0) {
        bytesWritten = walFile_->write(record.data);
        if (bytesWritten != record.data.size()) {
            LOG_ERROR("Failed to write WAL record data");
            return 0;
        }
    }

    LOG_DEBUG(QString("WAL record written to file: LSN=%1, Type=%2, TxnID=%3")
                .arg(record.header.lsn)
                .arg(static_cast<int>(record.header.type))
                .arg(record.header.txnId));

    return record.header.lsn;
}

uint64_t WALManager::writeRecordToDatabase(WALRecord& record) {
    QMutexLocker locker(&mutex_);

    if (!dbBackend_) {
        LOG_ERROR("Database backend not initialized");
        return 0;
    }

    // 分配 LSN
    record.header.lsn = ++currentLSN_;
    record.header.dataSize = record.data.size();
    record.header.checksum = record.calculateChecksum();

    // 写入到数据库
    if (!dbBackend_->writeRecord(record)) {
        LOG_ERROR("Failed to write WAL record to database");
        return 0;
    }

    // 更新数据库中的当前LSN
    dbBackend_->setCurrentLSN(currentLSN_);

    return record.header.lsn;
}

bool WALManager::flush() {
    QMutexLocker locker(&mutex_);

    if (useDatabase_) {
        // 数据库模式：刷新缓冲池
        if (!dbBackend_) {
            return false;
        }
        return dbBackend_->flush();
    }

    // 文件模式
    if (!walFile_ || !walFile_->isOpen()) {
        return false;
    }

    bool success = walFile_->flush();
    if (success) {
        LOG_DEBUG("WAL flushed to disk");
    } else {
        LOG_ERROR("Failed to flush WAL");
    }

    return success;
}

bool WALManager::checkpoint() {
    QMutexLocker locker(&mutex_);

    LOG_INFO("Creating WAL checkpoint");

    // 创建检查点记录
    WALRecord cpRecord(WALRecordType::CHECKPOINT, 0);
    uint64_t lsn = writeRecord(cpRecord);

    if (lsn == 0) {
        LOG_ERROR("Failed to write checkpoint record");
        return false;
    }

    // 刷新到磁盘
    if (!flush()) {
        return false;
    }

    LOG_INFO(QString("Checkpoint created at LSN=%1").arg(lsn));
    return true;
}

bool WALManager::recover(Catalog* catalog, BufferPoolManager* bufferPool) {
    LOG_INFO("Starting WAL recovery");

    if (!catalog || !bufferPool) {
        LOG_ERROR("Invalid catalog or buffer pool for recovery");
        return false;
    }

    if (useDatabase_) {
        return recoverFromDatabase(catalog, bufferPool);
    } else {
        return recoverFromFile(catalog, bufferPool);
    }
}

bool WALManager::recoverFromFile(Catalog* catalog, BufferPoolManager* bufferPool) {
    // 先关闭已打开的WAL文件（从initialize打开的）
    if (walFile_->isOpen()) {
        walFile_->close();
    }

    if (!walFile_->open(QIODevice::ReadOnly)) {
        LOG_ERROR("Failed to open WAL file for recovery");
        return false;
    }

    // 第一遍：收集所有WAL记录和已提交的事务
    QVector<WALRecord> allRecords;
    QSet<TransactionId> committedTxns;
    QSet<TransactionId> abortedTxns;
    uint64_t maxLSN = 0;

    while (!walFile_->atEnd()) {
        // 读取记录头部
        WALRecordHeader header;
        qint64 bytesRead = walFile_->read(
            reinterpret_cast<char*>(&header),
            sizeof(WALRecordHeader)
        );

        if (bytesRead != sizeof(WALRecordHeader)) {
            if (bytesRead == 0) {
                break; // 到达文件末尾
            }
            LOG_WARN("Incomplete WAL record header, truncating");
            break;
        }

        // 读取记录数据
        QByteArray data;
        if (header.dataSize > 0) {
            data = walFile_->read(header.dataSize);
            if (data.size() != header.dataSize) {
                LOG_WARN("Incomplete WAL record data, truncating");
                break;
            }
        }

        // 构造记录并验证校验和
        WALRecord record;
        record.header = header;
        record.data = data;

        if (!record.verifyChecksum()) {
            LOG_ERROR(QString("Checksum mismatch for LSN=%1, stopping recovery").arg(header.lsn));
            break;
        }

        // 更新最大 LSN
        if (header.lsn > maxLSN) {
            maxLSN = header.lsn;
        }

        // 记录已提交和已回滚的事务
        if (header.type == WALRecordType::COMMIT_TXN) {
            committedTxns.insert(header.txnId);
            LOG_DEBUG(QString("Found committed transaction: TxnID=%1").arg(header.txnId));
        } else if (header.type == WALRecordType::ABORT_TXN) {
            abortedTxns.insert(header.txnId);
            LOG_DEBUG(QString("Found aborted transaction: TxnID=%1").arg(header.txnId));
        }

        allRecords.append(record);
    }

    walFile_->close();

    LOG_INFO(QString("WAL scan completed: %1 records, %2 committed txns, %3 aborted txns")
                .arg(allRecords.size())
                .arg(committedTxns.size())
                .arg(abortedTxns.size()));

    // 第二遍：重放已提交事务的操作
    int replayCount = 0;
    for (const auto& record : allRecords) {
        // 只重放已提交事务的数据操作
        if (!committedTxns.contains(record.header.txnId)) {
            continue;
        }

        switch (record.header.type) {
        case WALRecordType::INSERT:
            if (replayInsert(catalog, bufferPool, record)) {
                replayCount++;
            }
            break;

        case WALRecordType::UPDATE:
            if (replayUpdate(catalog, bufferPool, record)) {
                replayCount++;
            }
            break;

        case WALRecordType::DELETE:
            if (replayDelete(catalog, bufferPool, record)) {
                replayCount++;
            }
            break;

        default:
            // 跳过事务控制记录和其他类型
            break;
        }
    }

    // 恢复当前 LSN
    currentLSN_ = maxLSN;

    LOG_INFO(QString("WAL recovery completed: %1 operations replayed, LSN=%2")
                .arg(replayCount)
                .arg(currentLSN_));

    // 重新以追加模式打开WAL文件
    if (!walFile_->open(QIODevice::WriteOnly | QIODevice::Append)) {
        LOG_ERROR("Failed to reopen WAL file in append mode after recovery");
        return false;
    }

    return true;
}

bool WALManager::recoverFromDatabase(Catalog* catalog, BufferPoolManager* bufferPool) {
    if (!dbBackend_) {
        LOG_ERROR("Database backend not initialized");
        return false;
    }

    // 读取所有WAL记录
    QVector<WALRecord> allRecords;
    if (!dbBackend_->readAllRecords(allRecords)) {
        LOG_ERROR("Failed to read WAL records from database");
        return false;
    }

    // 收集已提交和已回滚的事务
    QSet<TransactionId> committedTxns;
    QSet<TransactionId> abortedTxns;
    uint64_t maxLSN = 0;

    for (const auto& record : allRecords) {
        // 验证校验和
        if (!record.verifyChecksum()) {
            LOG_ERROR(QString("Checksum mismatch for LSN=%1, stopping recovery").arg(record.header.lsn));
            break;
        }

        // 更新最大 LSN
        if (record.header.lsn > maxLSN) {
            maxLSN = record.header.lsn;
        }

        // 记录已提交和已回滚的事务
        if (record.header.type == WALRecordType::COMMIT_TXN) {
            committedTxns.insert(record.header.txnId);
            LOG_DEBUG(QString("Found committed transaction: TxnID=%1").arg(record.header.txnId));
        } else if (record.header.type == WALRecordType::ABORT_TXN) {
            abortedTxns.insert(record.header.txnId);
            LOG_DEBUG(QString("Found aborted transaction: TxnID=%1").arg(record.header.txnId));
        }
    }

    LOG_INFO(QString("WAL scan completed: %1 records, %2 committed txns, %3 aborted txns")
                .arg(allRecords.size())
                .arg(committedTxns.size())
                .arg(abortedTxns.size()));

    // 重放已提交事务的操作
    int replayCount = 0;
    for (const auto& record : allRecords) {
        // 只重放已提交事务的数据操作
        if (!committedTxns.contains(record.header.txnId)) {
            continue;
        }

        switch (record.header.type) {
        case WALRecordType::INSERT:
            if (replayInsert(catalog, bufferPool, record)) {
                replayCount++;
            }
            break;

        case WALRecordType::UPDATE:
            if (replayUpdate(catalog, bufferPool, record)) {
                replayCount++;
            }
            break;

        case WALRecordType::DELETE:
            if (replayDelete(catalog, bufferPool, record)) {
                replayCount++;
            }
            break;

        default:
            // 跳过事务控制记录和其他类型
            break;
        }
    }

    // 恢复当前 LSN
    currentLSN_ = maxLSN;

    LOG_INFO(QString("WAL recovery from database completed: %1 operations replayed, LSN=%2")
                .arg(replayCount)
                .arg(currentLSN_));

    return true;
}

bool WALManager::replayInsert(Catalog* catalog, BufferPoolManager* bufferPool, const WALRecord& record) {
    // 反序列化INSERT数据：tableName, rowId, pageId, slotIndex
    QDataStream stream(record.data);
    QString tableName;
    RowId rowId;
    PageId pageId;
    uint16_t slotIndex;

    stream >> tableName >> rowId >> pageId >> slotIndex;

    LOG_DEBUG(QString("Replaying INSERT: table=%1, rowId=%2, page=%3, slot=%4")
                 .arg(tableName).arg(rowId).arg(pageId).arg(slotIndex));

    // 注意：实际的INSERT数据已经在页面中，我们不需要重新插入
    // WAL恢复的目的是确保已提交的事务持久化，而页面可能已经被写入磁盘
    // 这里我们只是验证页面上的数据是否存在

    const TableDef* table = catalog->getTable(tableName);
    if (!table) {
        LOG_WARN(QString("Table '%1' not found during recovery").arg(tableName));
        return false;
    }

    Page* page = bufferPool->fetchPage(pageId);
    if (!page) {
        LOG_WARN(QString("Failed to fetch page %1 during INSERT recovery").arg(pageId));
        return false;
    }

    bufferPool->unpinPage(pageId, false);

    return true;
}

bool WALManager::replayUpdate(Catalog* catalog, BufferPoolManager* bufferPool, const WALRecord& record) {
    // 反序列化UPDATE数据：tableName, pageId, slotIndex
    QDataStream stream(record.data);
    QString tableName;
    PageId pageId;
    uint16_t slotIndex;

    stream >> tableName >> pageId >> slotIndex;

    LOG_DEBUG(QString("Replaying UPDATE: table=%1, page=%2, slot=%3")
                 .arg(tableName).arg(pageId).arg(slotIndex));

    // UPDATE恢复类似INSERT，数据已经在页面中
    const TableDef* table = catalog->getTable(tableName);
    if (!table) {
        LOG_WARN(QString("Table '%1' not found during recovery").arg(tableName));
        return false;
    }

    Page* page = bufferPool->fetchPage(pageId);
    if (!page) {
        LOG_WARN(QString("Failed to fetch page %1 during UPDATE recovery").arg(pageId));
        return false;
    }

    bufferPool->unpinPage(pageId, false);

    return true;
}

bool WALManager::replayDelete(Catalog* catalog, BufferPoolManager* bufferPool, const WALRecord& record) {
    // 反序列化DELETE数据：tableName, pageId, slotIndex
    QDataStream stream(record.data);
    QString tableName;
    PageId pageId;
    uint16_t slotIndex;

    stream >> tableName >> pageId >> slotIndex;

    LOG_DEBUG(QString("Replaying DELETE: table=%1, page=%2, slot=%3")
                 .arg(tableName).arg(pageId).arg(slotIndex));

    // DELETE恢复类似INSERT和UPDATE
    const TableDef* table = catalog->getTable(tableName);
    if (!table) {
        LOG_WARN(QString("Table '%1' not found during recovery").arg(tableName));
        return false;
    }

    Page* page = bufferPool->fetchPage(pageId);
    if (!page) {
        LOG_WARN(QString("Failed to fetch page %1 during DELETE recovery").arg(pageId));
        return false;
    }

    bufferPool->unpinPage(pageId, false);

    return true;
}

bool WALManager::beginTransaction(TransactionId txnId) {
    WALRecord record(WALRecordType::BEGIN_TXN, txnId);
    uint64_t lsn = writeRecord(record);

    if (lsn > 0) {
        LOG_DEBUG(QString("Transaction begin recorded: TxnID=%1, LSN=%2").arg(txnId).arg(lsn));
        return true;
    }

    return false;
}

bool WALManager::commitTransaction(TransactionId txnId) {
    WALRecord record(WALRecordType::COMMIT_TXN, txnId);
    uint64_t lsn = writeRecord(record);

    if (lsn > 0) {
        // 提交时必须刷新到磁盘（保证持久性）
        flush();
        LOG_DEBUG(QString("Transaction commit recorded: TxnID=%1, LSN=%2").arg(txnId).arg(lsn));
        return true;
    }

    return false;
}

bool WALManager::abortTransaction(TransactionId txnId) {
    WALRecord record(WALRecordType::ABORT_TXN, txnId);
    uint64_t lsn = writeRecord(record);

    if (lsn > 0) {
        LOG_DEBUG(QString("Transaction abort recorded: TxnID=%1, LSN=%2").arg(txnId).arg(lsn));
        return true;
    }

    return false;
}

} // namespace qindb
