#ifndef QINDB_TABLE_PAGE_H
#define QINDB_TABLE_PAGE_H

#include "common.h"
#include "page.h"
#include "catalog.h"
#include <QVector>
#include <QVariant>

namespace qindb {

/**
 * @brief 表页记录槽位
 *
 * 槽位数组存储在页头之后，每个槽位记录一个记录的偏移和长度
 */
#pragma pack(push, 1)
struct Slot {
    uint16_t offset;    // 记录在页中的偏移
    uint16_t length;    // 记录长度
};
#pragma pack(pop)

/**
 * @brief 记录头（存储在每条记录前面）
 */
#pragma pack(push, 1)
struct RecordHeader {
    RowId rowId;                    // 行ID (8 字节)
    TransactionId createTxnId;      // 创建该记录的事务ID (8 字节)
    TransactionId deleteTxnId;      // 删除该记录的事务ID (8 字节)
    uint16_t columnCount;           // 列数量 (2 字节)

    RecordHeader()
        : rowId(INVALID_ROW_ID)
        , createTxnId(INVALID_TXN_ID)
        , deleteTxnId(INVALID_TXN_ID)
        , columnCount(0)
    {}
};
#pragma pack(pop)

static_assert(sizeof(RecordHeader) == 26, "RecordHeader must be 26 bytes");

/**
 * @brief 表页管理器
 *
 * 职责：
 * - 在表页中插入记录
 * - 从表页中读取记录
 * - 更新和删除记录
 * - 管理页内空闲空间
 *
 * 表页布局（使用槽位页设计）：
 * +-------------------+
 * | PageHeader        | 32 字节
 * +-------------------+
 * | Slot Array        | slotCount * 4 字节（从前向后增长）
 * +-------------------+
 * |                   |
 * | Free Space        |
 * |                   |
 * +-------------------+
 * | Records           | （从后向前增长）
 * +-------------------+
 */
class TablePage {
public:
    /**
     * @brief 初始化表页
     */
    static void init(Page* page, PageId pageId);

    /**
     * @brief 插入一条记录
     *
     * @param page 页对象
     * @param tableDef 表定义
     * @param rowId 行ID
     * @param values 列值
     * @param txnId 创建事务ID（默认为INVALID_TXN_ID）
     * @return 是否成功插入
     */
    static bool insertRecord(Page* page, const TableDef* tableDef, RowId rowId,
                            const QVector<QVariant>& values,
                            TransactionId txnId = INVALID_TXN_ID);

    /**
     * @brief 获取页中的所有记录
     *
     * @param page 页对象
     * @param tableDef 表定义
     * @param records 输出记录列表（每条记录是一个QVariant数组）
     * @return 是否成功
     */
    static bool getAllRecords(Page* page, const TableDef* tableDef,
                             QVector<QVector<QVariant>>& records);

    /**
     * @brief 获取页中的所有记录（包含行ID）
     *
     * @param page 页对象
     * @param tableDef 表定义
     * @param records 输出记录列表（每条记录是一个QVariant数组）
     * @param rowIds 输出行ID列表（可选）
     * @return 是否成功
     */
    static bool getAllRecords(Page* page, const TableDef* tableDef,
                             QVector<QVector<QVariant>>& records,
                             QVector<RowId>* rowIds);

    /**
     * @brief 获取页中的所有记录（包含RecordHeader）
     *
     * @param page 页对象
     * @param tableDef 表定义
     * @param records 输出记录列表（每条记录是一个QVariant数组）
     * @param headers 输出记录头列表（包含xmin/xmax等MVCC信息）
     * @return 是否成功
     */
    static bool getAllRecords(Page* page, const TableDef* tableDef,
                             QVector<QVector<QVariant>>& records,
                             QVector<RecordHeader>& headers);

    /**
     * @brief 获取页中剩余可用空间
     */
    static uint16_t getFreeSpace(Page* page);

    /**
     * @brief 检查页是否有足够空间插入记录
     *
     * @param page 页对象
     * @param recordSize 需要插入的记录大小
     * @return 是否有足够空间
     */
    static bool hasEnoughSpace(Page* page, uint16_t recordSize);

    /**
     * @brief 计算记录需要的空间大小
     */
    static uint16_t calculateRecordSize(const TableDef* tableDef,
                                       const QVector<QVariant>& values);

    /**
     * @brief 删除记录（逻辑删除：设置deleteTxnId）
     *
     * @param page 页对象
     * @param slotIndex 槽位索引
     * @param txnId 删除事务ID（默认为1）
     * @return 是否成功删除
     */
    static bool deleteRecord(Page* page, int slotIndex, TransactionId txnId = 1);

    /**
     * @brief 更新记录（简化版本：删除旧记录，插入新记录）
     *
     * @param page 页对象
     * @param tableDef 表定义
     * @param slotIndex 槽位索引
     * @param newValues 新的列值
     * @param txnId 事务ID（默认为1）
     * @return 是否成功更新
     */
    static bool updateRecord(Page* page, const TableDef* tableDef,
                            int slotIndex, const QVector<QVariant>& newValues,
                            TransactionId txnId = 1);

    /**
     * @brief 获取记录头部（用于 Undo Log 回滚）
     *
     * @param page 页对象
     * @param slotIndex 槽位索引
     * @return 记录头部指针，如果失败返回 nullptr
     */
    static RecordHeader* getRecordHeader(Page* page, int slotIndex);

    // ========== 底层 API（供系统表使用）==========

    /**
     * @brief 初始化表页（别名方法，供系统表使用）
     * @param page 页对象
     */
    static void initialize(Page* page);

    /**
     * @brief 插入原始元组数据（无需 TableDef，供系统表使用）
     * @param page 页对象
     * @param data 原始二进制数据
     * @param rowId 输出：分配的行ID
     * @return 是否成功
     */
    static bool insertTuple(Page* page, const QByteArray& data, RowId* rowId);

    /**
     * @brief 获取槽位数量
     * @param page 页对象
     * @return 槽位数量
     */
    static uint16_t getSlotCount(Page* page);

    /**
     * @brief 获取指定槽位的原始元组数据
     * @param page 页对象
     * @param slotIndex 槽位索引
     * @param data 输出：元组数据
     * @return 是否成功
     */
    static bool getTuple(Page* page, int slotIndex, QByteArray& data);

private:
    /**
     * @brief 序列化记录（将QVariant数组序列化为字节流）
     */
    static bool serializeRecord(const TableDef* tableDef, RowId rowId,
                               const QVector<QVariant>& values,
                               QByteArray& output,
                               TransactionId txnId = INVALID_TXN_ID);

    /**
     * @brief 反序列化记录（从字节流恢复为QVariant数组）
     */
    static bool deserializeRecord(const TableDef* tableDef, const char* data,
                                  uint16_t length, QVector<QVariant>& values);

    /**
     * @brief 获取槽位数组起始位置
     */
    static Slot* getSlotArray(Page* page) {
        return reinterpret_cast<Slot*>(page->getData() + sizeof(PageHeader));
    }

    /**
     * @brief 序列化单个字段
     */
    static bool serializeField(const ColumnDef& colDef, const QVariant& value,
                              QDataStream& stream);

    /**
     * @brief 反序列化单个字段
     */
    static bool deserializeField(const ColumnDef& colDef, QVariant& value,
                                QDataStream& stream);
};

} // namespace qindb

#endif // QINDB_TABLE_PAGE_H
