#include "qindb/table_page.h"
#include "qindb/logger.h"
#include <QDataStream>
#include <QByteArray>

namespace qindb {

void TablePage::init(Page* page, PageId pageId) {
    page->reset();
    PageHeader* header = page->getHeader();
    header->pageType = PageType::TABLE_PAGE;  // 修改为TABLE_PAGE
    header->pageId = pageId;
    header->slotCount = 0;
    header->freeSpaceOffset = PAGE_SIZE;  // 记录从页面末尾向前写入
    header->freeSpaceSize = PAGE_SIZE - sizeof(PageHeader);
    header->nextPageId = INVALID_PAGE_ID;
    header->prevPageId = INVALID_PAGE_ID;
}

bool TablePage::insertRecord(Page* page, const TableDef* tableDef, RowId rowId,
                            const QVector<QVariant>& values,
                            TransactionId txnId) {
    if (!page || !tableDef) {
        return false;
    }

    // 序列化记录
    QByteArray recordData;
    if (!serializeRecord(tableDef, rowId, values, recordData, txnId)) {
        LOG_ERROR("Failed to serialize record");
        return false;
    }

    uint16_t recordSize = static_cast<uint16_t>(recordData.size());
    uint16_t requiredSpace = recordSize + sizeof(Slot);

    // 检查是否有足够空间
    PageHeader* header = page->getHeader();

    // 验证页头数据有效性
    if (header->freeSpaceOffset > PAGE_SIZE) {
        LOG_ERROR(QString("Page %1 has corrupted freeSpaceOffset: %2 (page size: %3)")
                     .arg(page->getPageId())
                     .arg(header->freeSpaceOffset)
                     .arg(PAGE_SIZE));
        return false;
    }

    if (header->slotCount > (PAGE_SIZE - sizeof(PageHeader)) / sizeof(Slot)) {
        LOG_ERROR(QString("Page %1 has corrupted slotCount: %2")
                     .arg(page->getPageId())
                     .arg(header->slotCount));
        return false;
    }

    uint16_t slotsEndOffset = sizeof(PageHeader) + (header->slotCount + 1) * sizeof(Slot);

    // 验证slots区域不会超出页面边界
    if (slotsEndOffset > PAGE_SIZE) {
        LOG_ERROR(QString("Page %1 slot array would exceed page boundary: slotsEndOffset=%2")
                     .arg(page->getPageId())
                     .arg(slotsEndOffset));
        return false;
    }

    // 验证freeSpaceOffset在有效范围内
    if (header->freeSpaceOffset < slotsEndOffset) {
        LOG_ERROR(QString("Page %1 has invalid freeSpaceOffset: %2 < slotsEndOffset: %3")
                     .arg(page->getPageId())
                     .arg(header->freeSpaceOffset)
                     .arg(slotsEndOffset));
        return false;
    }

    uint16_t availableSpace = header->freeSpaceOffset - slotsEndOffset;

    if (availableSpace < requiredSpace) {
        LOG_DEBUG(QString("Page %1 does not have enough space: available=%2, required=%3")
                     .arg(page->getPageId())
                     .arg(availableSpace)
                     .arg(requiredSpace));
        return false;
    }

    // 找到记录的存储位置（从后向前）
    uint16_t recordOffset = header->freeSpaceOffset - recordSize;

    // 验证recordOffset在有效范围内
    if (recordOffset < slotsEndOffset || recordOffset + recordSize > PAGE_SIZE) {
        LOG_ERROR(QString("Page %1 calculated recordOffset is invalid: offset=%2, size=%3")
                     .arg(page->getPageId())
                     .arg(recordOffset)
                     .arg(recordSize));
        return false;
    }

    // 写入记录数据
    memcpy(page->getData() + recordOffset, recordData.constData(), recordSize);

    // 添加槽位
    Slot* slotArray = getSlotArray(page);
    slotArray[header->slotCount].offset = recordOffset;
    slotArray[header->slotCount].length = recordSize;
    header->slotCount++;

    // 更新页头
    header->freeSpaceOffset = recordOffset;
    header->freeSpaceSize = availableSpace - requiredSpace;

    // 标记页为脏
    page->setDirty(true);

    LOG_DEBUG(QString("Inserted record into page %1, slot %2, size %3 bytes")
                 .arg(page->getPageId())
                 .arg(header->slotCount - 1)
                 .arg(recordSize));

    return true;
}

bool TablePage::getAllRecords(Page* page, const TableDef* tableDef,
                             QVector<QVector<QVariant>>& records) {
    if (!page || !tableDef) {
        return false;
    }

    records.clear();

    PageHeader* header = page->getHeader();
    if (header->slotCount == 0) {
        return true; // 空页
    }

    Slot* slotArray = getSlotArray(page);

    for (uint16_t i = 0; i < header->slotCount; ++i) {
        const Slot& slot = slotArray[i];

        if (slot.length == 0) {
            continue; // 空槽位（已删除的记录）
        }

        QVector<QVariant> row;
        const char* recordData = page->getData() + slot.offset;

        if (!deserializeRecord(tableDef, recordData, slot.length, row)) {
            LOG_ERROR(QString("Failed to deserialize record from slot %1").arg(i));
            continue;
        }

        records.append(row);
    }

    return true;
}

bool TablePage::getAllRecords(Page* page, const TableDef* tableDef,
                             QVector<QVector<QVariant>>& records,
                             QVector<RowId>* rowIds) {
    if (!page || !tableDef) {
        return false;
    }

    records.clear();
    if (rowIds) {
        rowIds->clear();
    }

    PageHeader* header = page->getHeader();
    if (header->slotCount == 0) {
        return true; // 空页
    }

    Slot* slotArray = getSlotArray(page);

    for (uint16_t i = 0; i < header->slotCount; ++i) {
        const Slot& slot = slotArray[i];

        if (slot.length == 0) {
            continue; // 空槽位（已删除的记录）
        }

        const char* recordData = page->getData() + slot.offset;

        // 读取记录头以获取rowId
        QByteArray byteArray = QByteArray::fromRawData(recordData, slot.length);
        QDataStream stream(byteArray);
        stream.setByteOrder(QDataStream::LittleEndian);

        RecordHeader recordHeader;
        if (stream.readRawData(reinterpret_cast<char*>(&recordHeader), sizeof(RecordHeader)) != sizeof(RecordHeader)) {
            LOG_ERROR(QString("Failed to read record header from slot %1").arg(i));
            continue;
        }

        // 检查是否被删除
        if (recordHeader.deleteTxnId != INVALID_TXN_ID) {
            continue; // 记录已被删除，跳过
        }

        // 反序列化记录
        QVector<QVariant> row;
        if (!deserializeRecord(tableDef, recordData, slot.length, row)) {
            LOG_ERROR(QString("Failed to deserialize record from slot %1").arg(i));
            continue;
        }

        records.append(row);
        if (rowIds) {
            rowIds->append(recordHeader.rowId);
        }
    }

    return true;
}

bool TablePage::getAllRecords(Page* page, const TableDef* tableDef,
                             QVector<QVector<QVariant>>& records,
                             QVector<RecordHeader>& headers) {
    if (!page || !tableDef) {
        return false;
    }

    records.clear();
    headers.clear();

    PageHeader* header = page->getHeader();
    if (header->slotCount == 0) {
        return true; // 空页
    }

    Slot* slotArray = getSlotArray(page);
    const size_t pageSize = PAGE_SIZE;
    const size_t minRecordSize = sizeof(RecordHeader);

    for (uint16_t i = 0; i < header->slotCount; ++i) {
        const Slot& slot = slotArray[i];

        if (slot.length == 0) {
            continue; // 空槽位（已删除的记录）
        }

        // 边界检查1: 验证slot.offset和slot.length是否在页面范围内
        if (slot.offset >= pageSize) {
            LOG_ERROR(QString("Invalid slot offset %1 (page size: %2) at slot %3")
                .arg(slot.offset).arg(pageSize).arg(i));
            continue;
        }

        if (slot.offset + slot.length > pageSize) {
            LOG_ERROR(QString("Slot data exceeds page boundary (offset: %1, length: %2, page size: %3) at slot %4")
                .arg(slot.offset).arg(slot.length).arg(pageSize).arg(i));
            continue;
        }

        // 边界检查2: 验证长度是否至少包含RecordHeader
        if (slot.length < minRecordSize) {
            LOG_ERROR(QString("Slot length %1 is too small for RecordHeader (min: %2) at slot %3")
                .arg(slot.length).arg(minRecordSize).arg(i));
            continue;
        }

        const char* recordData = page->getData() + slot.offset;

        // 读取记录头
        QByteArray byteArray = QByteArray::fromRawData(recordData, slot.length);
        QDataStream stream(byteArray);
        stream.setByteOrder(QDataStream::LittleEndian);

        RecordHeader recordHeader;
        if (stream.readRawData(reinterpret_cast<char*>(&recordHeader), sizeof(RecordHeader)) != sizeof(RecordHeader)) {
            LOG_ERROR(QString("Failed to read record header from slot %1").arg(i));
            continue;
        }

        // 检查stream状态
        if (stream.status() != QDataStream::Ok) {
            LOG_ERROR(QString("Stream error after reading header from slot %1 (status: %2)")
                .arg(i).arg(stream.status()));
            continue;
        }

        // 注意：不在这里过滤已删除的记录，让调用者通过VisibilityChecker判断
        // 这样可以支持MVCC可见性检查

        // 反序列化记录 - 跳过 deleteTxnId 检查
        QVector<QVariant> row;
        bool deserializationSuccess = true;

        for (int j = 0; j < tableDef->columns.size(); ++j) {
            // 检查stream是否还有数据可读
            if (stream.atEnd() && j < tableDef->columns.size()) {
                LOG_ERROR(QString("Stream reached end prematurely at field %1/%2 in slot %3")
                    .arg(j).arg(tableDef->columns.size()).arg(i));
                deserializationSuccess = false;
                break;
            }

            QVariant value;
            if (!deserializeField(tableDef->columns[j], value, stream)) {
                LOG_ERROR(QString("Failed to deserialize field %1 (%2) from slot %3")
                    .arg(j).arg(tableDef->columns[j].name).arg(i));
                deserializationSuccess = false;
                break;
            }

            // 检查stream状态
            if (stream.status() != QDataStream::Ok) {
                LOG_ERROR(QString("Stream error after reading field %1 from slot %2 (status: %3)")
                    .arg(tableDef->columns[j].name).arg(i).arg(stream.status()));
                deserializationSuccess = false;
                break;
            }

            row.append(value);
        }

        if (deserializationSuccess && row.size() == tableDef->columns.size()) {
            records.append(row);
            headers.append(recordHeader);
        } else {
            LOG_WARN(QString("Skipping corrupted record at slot %1 (expected %2 fields, got %3)")
                .arg(i).arg(tableDef->columns.size()).arg(row.size()));
        }
    }

    return true;
}

uint16_t TablePage::getFreeSpace(Page* page) {
    if (!page) {
        return 0;
    }

    PageHeader* header = page->getHeader();
    uint16_t slotsEndOffset = sizeof(PageHeader) + header->slotCount * sizeof(Slot);
    uint16_t recordsStartOffset = header->freeSpaceOffset;

    if (recordsStartOffset <= slotsEndOffset) {
        return 0;
    }

    return recordsStartOffset - slotsEndOffset;
}

bool TablePage::hasEnoughSpace(Page* page, uint16_t recordSize) {
    return getFreeSpace(page) >= (recordSize + sizeof(Slot));
}

bool TablePage::serializeRecord(const TableDef* tableDef, RowId rowId,
                               const QVector<QVariant>& values,
                               QByteArray& output,
                               TransactionId txnId) {
    if (values.size() != tableDef->columns.size()) {
        LOG_ERROR(QString("Column count mismatch: expected %1, got %2")
                     .arg(tableDef->columns.size())
                     .arg(values.size()));
        return false;
    }

    output.clear();
    QDataStream stream(&output, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    // 写入记录头
    RecordHeader recordHeader;
    recordHeader.rowId = rowId;
    recordHeader.createTxnId = txnId;  // 使用实际的事务ID
    recordHeader.deleteTxnId = INVALID_TXN_ID;
    recordHeader.columnCount = static_cast<uint16_t>(tableDef->columns.size());

    stream.writeRawData(reinterpret_cast<const char*>(&recordHeader), sizeof(RecordHeader));

    // 写入每个字段
    for (int i = 0; i < tableDef->columns.size(); ++i) {
        if (!serializeField(tableDef->columns[i], values[i], stream)) {
            LOG_ERROR(QString("Failed to serialize field %1").arg(tableDef->columns[i].name));
            return false;
        }
    }

    return true;
}

bool TablePage::deserializeRecord(const TableDef* tableDef, const char* data,
                                  uint16_t length, QVector<QVariant>& values) {
    values.clear();

    QByteArray byteArray = QByteArray::fromRawData(data, length);
    QDataStream stream(byteArray);
    stream.setByteOrder(QDataStream::LittleEndian);

    // 读取记录头
    RecordHeader recordHeader;
    if (stream.readRawData(reinterpret_cast<char*>(&recordHeader), sizeof(RecordHeader)) != sizeof(RecordHeader)) {
        LOG_ERROR("Failed to read record header");
        return false;
    }

    // 检查是否被删除
    if (recordHeader.deleteTxnId != INVALID_TXN_ID) {
        // 记录已被删除，跳过
        return false;
    }

    // 读取每个字段
    for (int i = 0; i < tableDef->columns.size(); ++i) {
        QVariant value;
        if (!deserializeField(tableDef->columns[i], value, stream)) {
            LOG_ERROR(QString("Failed to deserialize field %1").arg(tableDef->columns[i].name));
            return false;
        }
        values.append(value);
    }

    return true;
}

uint16_t TablePage::calculateRecordSize(const TableDef* tableDef,
                                       const QVector<QVariant>& values) {
    QByteArray dummy;
    if (serializeRecord(tableDef, 0, values, dummy, INVALID_TXN_ID)) {
        return static_cast<uint16_t>(dummy.size());
    }
    return 0;
}

bool TablePage::serializeField(const ColumnDef& colDef, const QVariant& value,
                              QDataStream& stream) {
    // NULL标志位
    bool isNull = value.isNull();
    stream << isNull;

    if (isNull) {
        return true;
    }

    switch (colDef.type) {
        case DataType::INT: {
            qint32 v = value.toInt();
            stream << v;
            break;
        }

        case DataType::BIGINT: {
            qint64 v = value.toLongLong();
            stream << v;
            break;
        }

        case DataType::FLOAT: {
            float v = value.toFloat();
            stream << v;
            break;
        }

        case DataType::DOUBLE: {
            double v = value.toDouble();
            stream << v;
            break;
        }

        case DataType::BOOLEAN: {
            bool v = value.toBool();
            stream << v;
            break;
        }

        case DataType::CHAR:
        case DataType::VARCHAR:
        case DataType::TEXT: {
            QString v = value.toString();
            // 对于CHAR，截断或填充到固定长度
            if (colDef.type == DataType::CHAR && colDef.length > 0) {
                v = v.leftJustified(colDef.length, ' ', true);
            }
            stream << v;
            break;
        }

        case DataType::DATE:
        case DataType::TIME:
        case DataType::DATETIME: {
            // 存储为字符串格式
            QString v = value.toString();
            stream << v;
            break;
        }

        case DataType::DECIMAL: {
            // 存储为字符串以保持精度
            QString v = value.toString();
            stream << v;
            break;
        }

        case DataType::BLOB: {
            QByteArray v = value.toByteArray();
            stream << v;
            break;
        }

        default:
            LOG_ERROR(QString("Unsupported data type: %1").arg(static_cast<int>(colDef.type)));
            return false;
    }

    return true;
}

bool TablePage::deserializeField(const ColumnDef& colDef, QVariant& value,
                                QDataStream& stream) {
    // 检查stream状态
    if (stream.status() != QDataStream::Ok) {
        LOG_ERROR(QString("Stream is in error state before deserializing field %1").arg(colDef.name));
        return false;
    }

    // 检查是否还有数据可读
    if (stream.atEnd()) {
        LOG_ERROR(QString("Stream ended before deserializing field %1").arg(colDef.name));
        return false;
    }

    // 读取NULL标志位
    bool isNull;
    stream >> isNull;

    if (stream.status() != QDataStream::Ok) {
        LOG_ERROR(QString("Failed to read NULL flag for field %1").arg(colDef.name));
        return false;
    }

    if (isNull) {
        value = QVariant();
        return true;
    }

    switch (colDef.type) {
        case DataType::INT: {
            qint32 v;
            stream >> v;
            if (stream.status() != QDataStream::Ok) {
                LOG_ERROR(QString("Failed to read INT value for field %1").arg(colDef.name));
                return false;
            }
            value = v;
            break;
        }

        case DataType::BIGINT: {
            qint64 v;
            stream >> v;
            if (stream.status() != QDataStream::Ok) {
                LOG_ERROR(QString("Failed to read BIGINT value for field %1").arg(colDef.name));
                return false;
            }
            value = v;
            break;
        }

        case DataType::FLOAT: {
            float v;
            stream >> v;
            if (stream.status() != QDataStream::Ok) {
                LOG_ERROR(QString("Failed to read FLOAT value for field %1").arg(colDef.name));
                return false;
            }
            value = v;
            break;
        }

        case DataType::DOUBLE: {
            double v;
            stream >> v;
            if (stream.status() != QDataStream::Ok) {
                LOG_ERROR(QString("Failed to read DOUBLE value for field %1").arg(colDef.name));
                return false;
            }
            value = v;
            break;
        }

        case DataType::BOOLEAN: {
            bool v;
            stream >> v;
            if (stream.status() != QDataStream::Ok) {
                LOG_ERROR(QString("Failed to read BOOLEAN value for field %1").arg(colDef.name));
                return false;
            }
            value = v;
            break;
        }

        case DataType::CHAR:
        case DataType::VARCHAR:
        case DataType::TEXT: {
            QString v;
            stream >> v;
            if (stream.status() != QDataStream::Ok) {
                LOG_ERROR(QString("Failed to read STRING value for field %1").arg(colDef.name));
                return false;
            }
            // 对于CHAR，去除尾部空格
            if (colDef.type == DataType::CHAR) {
                v = v.trimmed();
            }
            value = v;
            break;
        }

        case DataType::DATE:
        case DataType::TIME:
        case DataType::DATETIME: {
            QString v;
            stream >> v;
            if (stream.status() != QDataStream::Ok) {
                LOG_ERROR(QString("Failed to read DATETIME value for field %1").arg(colDef.name));
                return false;
            }
            value = v;
            break;
        }

        case DataType::DECIMAL: {
            QString v;
            stream >> v;
            if (stream.status() != QDataStream::Ok) {
                LOG_ERROR(QString("Failed to read DECIMAL value for field %1").arg(colDef.name));
                return false;
            }
            value = v;
            break;
        }

        case DataType::BLOB: {
            QByteArray v;
            stream >> v;
            if (stream.status() != QDataStream::Ok) {
                LOG_ERROR(QString("Failed to read BLOB value for field %1").arg(colDef.name));
                return false;
            }
            value = v;
            break;
        }

        default:
            LOG_ERROR(QString("Unsupported data type: %1 for field %2")
                .arg(static_cast<int>(colDef.type)).arg(colDef.name));
            return false;
    }

    return true;
}

bool TablePage::deleteRecord(Page* page, int slotIndex, TransactionId txnId) {
    if (!page) {
        return false;
    }

    PageHeader* header = page->getHeader();
    if (slotIndex < 0 || slotIndex >= header->slotCount) {
        LOG_ERROR(QString("Invalid slot index: %1 (max: %2)").arg(slotIndex).arg(header->slotCount - 1));
        return false;
    }

    Slot* slotArray = getSlotArray(page);
    const Slot& slot = slotArray[slotIndex];

    if (slot.length == 0) {
        LOG_WARN(QString("Slot %1 is already empty").arg(slotIndex));
        return false;
    }

    // 获取记录数据位置
    char* recordData = page->getData() + slot.offset;

    // 修改记录头的deleteTxnId字段（逻辑删除）
    RecordHeader* recordHeader = reinterpret_cast<RecordHeader*>(recordData);

    if (recordHeader->deleteTxnId != INVALID_TXN_ID) {
        LOG_WARN(QString("Record in slot %1 is already deleted").arg(slotIndex));
        return false;
    }

    recordHeader->deleteTxnId = txnId;

    // 标记页为脏
    page->setDirty(true);

    LOG_DEBUG(QString("Deleted record from page %1, slot %2 (logical deletion, txnId=%3)")
                 .arg(page->getPageId())
                 .arg(slotIndex)
                 .arg(txnId));

    return true;
}

bool TablePage::updateRecord(Page* page, const TableDef* tableDef,
                            int slotIndex, const QVector<QVariant>& newValues,
                            TransactionId txnId) {
    if (!page || !tableDef) {
        return false;
    }

    PageHeader* header = page->getHeader();
    if (slotIndex < 0 || slotIndex >= header->slotCount) {
        LOG_ERROR(QString("Invalid slot index: %1 (max: %2)").arg(slotIndex).arg(header->slotCount - 1));
        return false;
    }

    Slot* slotArray = getSlotArray(page);
    Slot& slot = slotArray[slotIndex];

    if (slot.length == 0) {
        LOG_ERROR(QString("Slot %1 is empty").arg(slotIndex));
        return false;
    }

    // 获取旧记录数据
    char* oldRecordData = page->getData() + slot.offset;
    RecordHeader* oldRecordHeader = reinterpret_cast<RecordHeader*>(oldRecordData);

    if (oldRecordHeader->deleteTxnId != INVALID_TXN_ID) {
        LOG_ERROR(QString("Record in slot %1 is already deleted").arg(slotIndex));
        return false;
    }

    // 保存旧的rowId
    RowId rowId = oldRecordHeader->rowId;

    // 序列化新记录
    QByteArray newRecordData;
    if (!serializeRecord(tableDef, rowId, newValues, newRecordData, oldRecordHeader->createTxnId)) {
        LOG_ERROR("Failed to serialize new record data");
        return false;
    }

    uint16_t newRecordSize = static_cast<uint16_t>(newRecordData.size());
    uint16_t oldRecordSize = slot.length;

    // 简化版本：只支持原地更新（新记录大小 <= 旧记录大小）
    // 如果新记录更大，返回false（调用者需要删除后重新插入）
    if (newRecordSize > oldRecordSize) {
        LOG_DEBUG(QString("New record size (%1) > old size (%2), cannot update in place")
                     .arg(newRecordSize)
                     .arg(oldRecordSize));
        return false;
    }

    // 原地更新：覆盖旧数据
    memcpy(oldRecordData, newRecordData.constData(), newRecordSize);

    // 更新槽位长度（如果新记录更小）
    if (newRecordSize < oldRecordSize) {
        slot.length = newRecordSize;
        // 注意：这会造成内存碎片，但简化了实现
        // 真实数据库会进行页面压缩或重组
    }

    // 标记页为脏
    page->setDirty(true);

    LOG_DEBUG(QString("Updated record in page %1, slot %2 (in-place, old size=%3, new size=%4)")
                 .arg(page->getPageId())
                 .arg(slotIndex)
                 .arg(oldRecordSize)
                 .arg(newRecordSize));

    return true;
}

RecordHeader* TablePage::getRecordHeader(Page* page, int slotIndex) {
    if (!page) {
        return nullptr;
    }

    PageHeader* pageHeader = page->getHeader();
    if (slotIndex < 0 || slotIndex >= pageHeader->slotCount) {
        return nullptr;
    }

    // 获取槽位
    Slot* slotArray = getSlotArray(page);
    Slot& slot = slotArray[slotIndex];

    // 检查槽位是否有效
    if (slot.offset == 0 || slot.length == 0) {
        return nullptr;
    }

    // 获取记录数据
    char* recordData = page->getData() + slot.offset;

    // 记录头部在记录的开头
    RecordHeader* header = reinterpret_cast<RecordHeader*>(recordData);

    return header;
}

// ========== 底层 API 实现（供系统表使用）==========

void TablePage::initialize(Page* page) {
    if (!page) {
        return;
    }
    // 简化初始化，不需要 pageId（系统表页面ID已知）
    page->reset();
    PageHeader* header = page->getHeader();
    header->pageType = PageType::TABLE_PAGE;
    header->slotCount = 0;
    header->freeSpaceOffset = PAGE_SIZE;
    header->freeSpaceSize = PAGE_SIZE - sizeof(PageHeader);
    header->nextPageId = INVALID_PAGE_ID;
    header->prevPageId = INVALID_PAGE_ID;
}

bool TablePage::insertTuple(Page* page, const QByteArray& data, RowId* rowId) {
    if (!page || data.isEmpty()) {
        LOG_ERROR("Invalid page or empty data");
        return false;
    }

    PageHeader* header = page->getHeader();
    uint16_t recordSize = static_cast<uint16_t>(data.size());
    uint16_t requiredSpace = recordSize + sizeof(Slot);

    // 验证页头数据有效性
    if (header->freeSpaceOffset > PAGE_SIZE) {
        LOG_ERROR(QString("Page %1 has corrupted freeSpaceOffset: %2 (page size: %3)")
                     .arg(page->getPageId())
                     .arg(header->freeSpaceOffset)
                     .arg(PAGE_SIZE));
        return false;
    }

    if (header->slotCount > (PAGE_SIZE - sizeof(PageHeader)) / sizeof(Slot)) {
        LOG_ERROR(QString("Page %1 has corrupted slotCount: %2")
                     .arg(page->getPageId())
                     .arg(header->slotCount));
        return false;
    }

    // 检查空间
    uint16_t slotsEndOffset = sizeof(PageHeader) + (header->slotCount + 1) * sizeof(Slot);

    // 验证slots区域不会超出页面边界
    if (slotsEndOffset > PAGE_SIZE) {
        LOG_ERROR(QString("Page %1 slot array would exceed page boundary: slotsEndOffset=%2")
                     .arg(page->getPageId())
                     .arg(slotsEndOffset));
        return false;
    }

    // 验证freeSpaceOffset在有效范围内
    if (header->freeSpaceOffset < slotsEndOffset) {
        LOG_ERROR(QString("Page %1 has invalid freeSpaceOffset: %2 < slotsEndOffset: %3")
                     .arg(page->getPageId())
                     .arg(header->freeSpaceOffset)
                     .arg(slotsEndOffset));
        return false;
    }

    uint16_t availableSpace = header->freeSpaceOffset - slotsEndOffset;

    if (availableSpace < requiredSpace) {
        LOG_DEBUG(QString("Page %1 does not have enough space for raw tuple: available=%2, required=%3")
                     .arg(page->getPageId())
                     .arg(availableSpace)
                     .arg(requiredSpace));
        return false;
    }

    // 计算记录偏移（从后向前）
    uint16_t recordOffset = header->freeSpaceOffset - recordSize;

    // 验证recordOffset在有效范围内
    if (recordOffset < slotsEndOffset || recordOffset + recordSize > PAGE_SIZE) {
        LOG_ERROR(QString("Page %1 calculated recordOffset is invalid: offset=%2, size=%3")
                     .arg(page->getPageId())
                     .arg(recordOffset)
                     .arg(recordSize));
        return false;
    }

    // 写入数据
    memcpy(page->getData() + recordOffset, data.constData(), recordSize);

    // 添加槽位
    Slot* slotArray = getSlotArray(page);
    uint16_t slotIndex = header->slotCount;
    slotArray[slotIndex].offset = recordOffset;
    slotArray[slotIndex].length = recordSize;

    // 更新页头
    header->slotCount++;
    header->freeSpaceOffset = recordOffset;
    header->freeSpaceSize = availableSpace - requiredSpace;

    // 标记页为脏
    page->setDirty(true);

    // 生成行ID（简化版：pageId << 16 | slotIndex）
    if (rowId) {
        *rowId = (static_cast<RowId>(page->getPageId()) << 16) | slotIndex;
    }

    LOG_DEBUG(QString("Inserted raw tuple into page %1, slot %2, size %3 bytes")
                 .arg(page->getPageId())
                 .arg(slotIndex)
                 .arg(recordSize));

    return true;
}

uint16_t TablePage::getSlotCount(Page* page) {
    if (!page) {
        return 0;
    }
    PageHeader* header = page->getHeader();
    return header->slotCount;
}

bool TablePage::getTuple(Page* page, int slotIndex, QByteArray& data) {
    if (!page) {
        LOG_ERROR("Invalid page");
        return false;
    }

    PageHeader* header = page->getHeader();
    if (slotIndex < 0 || slotIndex >= static_cast<int>(header->slotCount)) {
        LOG_ERROR(QString("Invalid slot index: %1 (max: %2)").arg(slotIndex).arg(header->slotCount - 1));
        return false;
    }

    Slot* slotArray = getSlotArray(page);
    const Slot& slot = slotArray[slotIndex];

    if (slot.length == 0) {
        LOG_DEBUG(QString("Slot %1 is empty (deleted record)").arg(slotIndex));
        return false;
    }

    // 读取数据
    data = QByteArray(page->getData() + slot.offset, slot.length);

    LOG_DEBUG(QString("Retrieved raw tuple from page %1, slot %2, size %3 bytes")
                 .arg(page->getPageId())
                 .arg(slotIndex)
                 .arg(slot.length));

    return true;
}

} // namespace qindb
