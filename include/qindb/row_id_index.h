#ifndef QINDB_ROW_ID_INDEX_H
#define QINDB_ROW_ID_INDEX_H

#include "common.h"
#include <QHash>
#include <QMutex>
#include <QVector>

namespace qindb {

/**
 * @brief 行位置信息
 *
 * 记录行ID对应的物理位置（页ID + 槽位索引）
 */
struct RowLocation {
    PageId pageId;      // 页ID
    uint16_t slotIndex; // 槽位索引

    RowLocation()
        : pageId(INVALID_PAGE_ID)
        , slotIndex(0)
    {}

    RowLocation(PageId pid, uint16_t slot)
        : pageId(pid)
        , slotIndex(slot)
    {}

    bool isValid() const {
        return pageId != INVALID_PAGE_ID;
    }
};

/**
 * @brief RowId 索引
 *
 * 职责：
 * - 维护 rowId 到 (pageId, slotIndex) 的映射
 * - 支持快速查找行的物理位置
 * - 支持索引的持久化和恢复
 *
 * 注意：这是一个内存结构，在数据库启动时需要扫描所有表页来重建
 */
class RowIdIndex {
public:
    RowIdIndex();
    ~RowIdIndex();

    /**
     * @brief 添加行位置映射
     *
     * @param rowId 行ID
     * @param location 行位置
     */
    void insert(RowId rowId, const RowLocation& location);

    /**
     * @brief 删除行位置映射
     *
     * @param rowId 行ID
     */
    void remove(RowId rowId);

    /**
     * @brief 查找行位置
     *
     * @param rowId 行ID
     * @param location 输出位置信息
     * @return 是否找到
     */
    bool lookup(RowId rowId, RowLocation& location) const;

    /**
     * @brief 更新行位置
     *
     * @param rowId 行ID
     * @param newLocation 新位置
     * @return 是否成功
     */
    bool update(RowId rowId, const RowLocation& newLocation);

    /**
     * @brief 清空所有映射
     */
    void clear();

    /**
     * @brief 获取映射数量
     */
    int size() const;

    /**
     * @brief 获取所有 rowId（用于调试）
     */
    QVector<RowId> getAllRowIds() const;

private:
    QHash<RowId, RowLocation> index_;   // rowId -> location 映射
    mutable QMutex mutex_;               // 线程安全
};

} // namespace qindb

#endif // QINDB_ROW_ID_INDEX_H
