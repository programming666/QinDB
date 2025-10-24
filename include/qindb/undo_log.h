#ifndef QINDB_UNDO_LOG_H
#define QINDB_UNDO_LOG_H

#include "common.h"
#include <QString>
#include <QByteArray>
#include <QVariant>
#include <QVector>

namespace qindb {

/**
 * @brief Undo 日志操作类型
 */
enum class UndoOperationType : uint8_t {
    INVALID = 0,
    INSERT,     // 插入操作 - 需要删除记录
    UPDATE,     // 更新操作 - 需要恢复旧值
    DELETE      // 删除操作 - 需要恢复记录
};

/**
 * @brief Undo 日志记录
 *
 * 用于在事务回滚时恢复数据到操作前的状态
 */
struct UndoRecord {
    UndoOperationType opType;       // 操作类型
    QString tableName;              // 表名
    PageId pageId;                  // 页面ID
    int slotIndex;                  // 槽位索引
    QVector<QVariant> oldValues;    // 旧值（UPDATE 和 DELETE 使用）
    uint64_t lsn;                   // 对应的 WAL LSN

    UndoRecord()
        : opType(UndoOperationType::INVALID)
        , pageId(INVALID_PAGE_ID)
        , slotIndex(-1)
        , lsn(0)
    {}

    /**
     * @brief 创建 INSERT 的 Undo 记录
     */
    static UndoRecord createInsertUndo(
        const QString& table,
        PageId pid,
        int slot,
        uint64_t walLsn
    ) {
        UndoRecord undo;
        undo.opType = UndoOperationType::INSERT;
        undo.tableName = table;
        undo.pageId = pid;
        undo.slotIndex = slot;
        undo.lsn = walLsn;
        return undo;
    }

    /**
     * @brief 创建 UPDATE 的 Undo 记录
     */
    static UndoRecord createUpdateUndo(
        const QString& table,
        PageId pid,
        int slot,
        const QVector<QVariant>& oldVals,
        uint64_t walLsn
    ) {
        UndoRecord undo;
        undo.opType = UndoOperationType::UPDATE;
        undo.tableName = table;
        undo.pageId = pid;
        undo.slotIndex = slot;
        undo.oldValues = oldVals;
        undo.lsn = walLsn;
        return undo;
    }

    /**
     * @brief 创建 DELETE 的 Undo 记录
     */
    static UndoRecord createDeleteUndo(
        const QString& table,
        PageId pid,
        int slot,
        const QVector<QVariant>& oldVals,
        uint64_t walLsn
    ) {
        UndoRecord undo;
        undo.opType = UndoOperationType::DELETE;
        undo.tableName = table;
        undo.pageId = pid;
        undo.slotIndex = slot;
        undo.oldValues = oldVals;
        undo.lsn = walLsn;
        return undo;
    }

    /**
     * @brief 序列化为字节数组
     */
    QByteArray serialize() const;

    /**
     * @brief 从字节数组反序列化
     */
    static UndoRecord deserialize(const QByteArray& data);
};

} // namespace qindb

#endif // QINDB_UNDO_LOG_H
