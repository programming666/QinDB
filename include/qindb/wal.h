#ifndef QINDB_WAL_H
#define QINDB_WAL_H

#include "common.h"
#include <QString>
#include <QFile>
#include <QMutex>
#include <memory>

namespace qindb {

// 前向声明
class WalDbBackend;
class BufferPoolManager;
class DiskManager;

/**
 * @brief WAL 日志记录类型
 */
enum class WALRecordType : uint8_t {
    INVALID = 0,
    INSERT,          // 插入记录
    UPDATE,          // 更新记录
    DELETE,          // 删除记录
    BEGIN_TXN,       // 事务开始
    COMMIT_TXN,      // 事务提交
    ABORT_TXN,       // 事务回滚
    CHECKPOINT       // 检查点
};

/**
 * @brief WAL 日志记录头部
 */
#pragma pack(push, 1)
struct WALRecordHeader {
    WALRecordType type;         // 记录类型 (1 字节)
    uint8_t reserved1;          // 保留 (1 字节)
    uint16_t dataSize;          // 数据大小 (2 字节)
    TransactionId txnId;        // 事务ID (8 字节)
    uint64_t lsn;               // 日志序列号 (8 字节)
    uint32_t checksum;          // 校验和 (4 字节)
    uint32_t reserved2;         // 保留 (4 字节)

    WALRecordHeader()
        : type(WALRecordType::INVALID)
        , reserved1(0)
        , dataSize(0)
        , txnId(INVALID_TXN_ID)
        , lsn(0)
        , checksum(0)
        , reserved2(0)
    {}
};
#pragma pack(pop)

static_assert(sizeof(WALRecordHeader) == 28, "WALRecordHeader must be 28 bytes");

/**
 * @brief WAL 日志记录
 */
struct WALRecord {
    WALRecordHeader header;
    QByteArray data;

    WALRecord() {}
    WALRecord(WALRecordType type, TransactionId txnId, const QByteArray& d = QByteArray())
        : data(d)
    {
        header.type = type;
        header.txnId = txnId;
        header.dataSize = data.size();
        header.checksum = calculateChecksum();
    }

    uint32_t calculateChecksum() const;
    bool verifyChecksum() const;
};

/**
 * @brief Write-Ahead Log 管理器
 *
 * 职责：
 * 1. 记录所有事务操作到日志文件
 * 2. 在系统崩溃后恢复数据
 * 3. 支持检查点机制
 * 4. 保证事务的持久性
 */
class Catalog;
class BufferPoolManager;

class WALManager {
public:
    /**
     * @brief 构造函数
     * @param walFilePath WAL 文件路径
     */
    explicit WALManager(const QString& walFilePath);

    ~WALManager();

    /**
     * @brief 设置数据库后端（用于存储WAL到数据库内部）
     * @param bufferPool 缓冲池管理器
     * @param diskManager 磁盘管理器
     */
    void setDatabaseBackend(BufferPoolManager* bufferPool, DiskManager* diskManager);

    /**
     * @brief 初始化 WAL
     */
    bool initialize();

    /**
     * @brief 写入日志记录
     * @param record 日志记录
     * @return LSN（日志序列号）
     */
    uint64_t writeRecord(WALRecord& record);

    /**
     * @brief 刷新日志到磁盘
     */
    bool flush();

    /**
     * @brief 创建检查点
     */
    bool checkpoint();

    /**
     * @brief 恢复数据（系统启动时调用）
     * @param catalog 目录指针（用于获取表定义）
     * @param bufferPool 缓冲池指针（用于修改页面）
     * @return 是否成功
     */
    bool recover(Catalog* catalog, BufferPoolManager* bufferPool);

    /**
     * @brief 获取当前 LSN
     */
    uint64_t getCurrentLSN() const { return currentLSN_; }

    /**
     * @brief 开始事务
     * @param txnId 事务ID
     */
    bool beginTransaction(TransactionId txnId);

    /**
     * @brief 提交事务
     * @param txnId 事务ID
     */
    bool commitTransaction(TransactionId txnId);

    /**
     * @brief 回滚事务
     * @param txnId 事务ID
     */
    bool abortTransaction(TransactionId txnId);

private:
    QString walFilePath_;           // WAL 文件路径
    std::unique_ptr<QFile> walFile_; // WAL 文件对象
    uint64_t currentLSN_;           // 当前 LSN
    mutable QMutex mutex_;          // 互斥锁

    std::unique_ptr<WalDbBackend> dbBackend_; // 数据库存储后端
    bool useDatabase_;                        // 是否使用数据库存储（false=文件存储）

    /**
     * @brief 重放INSERT操作
     */
    bool replayInsert(Catalog* catalog, BufferPoolManager* bufferPool, const WALRecord& record);

    /**
     * @brief 重放UPDATE操作
     */
    bool replayUpdate(Catalog* catalog, BufferPoolManager* bufferPool, const WALRecord& record);

    /**
     * @brief 重放DELETE操作
     */
    bool replayDelete(Catalog* catalog, BufferPoolManager* bufferPool, const WALRecord& record);

    /**
     * @brief 写入记录到文件
     */
    uint64_t writeRecordToFile(WALRecord& record);

    /**
     * @brief 写入记录到数据库
     */
    uint64_t writeRecordToDatabase(WALRecord& record);

    /**
     * @brief 从文件恢复
     */
    bool recoverFromFile(Catalog* catalog, BufferPoolManager* bufferPool);

    /**
     * @brief 从数据库恢复
     */
    bool recoverFromDatabase(Catalog* catalog, BufferPoolManager* bufferPool);
};

} // namespace qindb

#endif // QINDB_WAL_H
