#include "qindb/undo_log.h"
#include <QDataStream>
#include <QIODevice>

namespace qindb {

QByteArray UndoRecord::serialize() const {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);

    // 写入操作类型
    stream << static_cast<uint8_t>(opType);

    // 写入表名
    stream << tableName;

    // 写入页面ID和槽位索引
    stream << pageId;
    stream << slotIndex;

    // 写入 LSN
    stream << lsn;

    // 写入旧值数量
    stream << static_cast<int>(oldValues.size());

    // 写入每个旧值
    for (const QVariant& value : oldValues) {
        stream << value;
    }

    return data;
}

UndoRecord UndoRecord::deserialize(const QByteArray& data) {
    UndoRecord undo;
    QDataStream stream(data);
    stream.setVersion(QDataStream::Qt_6_0);

    // 读取操作类型
    uint8_t opTypeVal;
    stream >> opTypeVal;
    undo.opType = static_cast<UndoOperationType>(opTypeVal);

    // 读取表名
    stream >> undo.tableName;

    // 读取页面ID和槽位索引
    stream >> undo.pageId;
    stream >> undo.slotIndex;

    // 读取 LSN
    stream >> undo.lsn;

    // 读取旧值数量
    int valueCount;
    stream >> valueCount;

    // 读取每个旧值
    undo.oldValues.reserve(valueCount);
    for (int i = 0; i < valueCount; ++i) {
        QVariant value;
        stream >> value;
        undo.oldValues.append(value);
    }

    return undo;
}

} // namespace qindb
