#ifndef QINDB_STATISTICS_H
#define QINDB_STATISTICS_H

#include "qindb/common.h"
#include <QVector>
#include <QMap>
#include <QString>
#include <QVariant>
#include <memory>

namespace qindb {

class Catalog;
class BufferPoolManager;

/**
 * @brief 列统计信息
 *
 * 存储单个列的统计数据，用于查询优化
 */
struct ColumnStats {
    QString columnName;              // 列名
    DataType dataType;               // 数据类型

    // 基础统计
    size_t numDistinctValues = 0;    // 不同值的数量（基数）
    size_t numNulls = 0;             // NULL 值的数量

    // 值范围（用于数值和日期类型）
    QVariant minValue;               // 最小值
    QVariant maxValue;               // 最大值

    // 采样数据（用于直方图）
    QVector<QVariant> sampleValues;  // 采样值

    // 最常见值（Most Common Values, MCV）
    QHash<QString, size_t> mcv;      // 值的字符串表示 → 出现次数

    ColumnStats() = default;
    ColumnStats(const QString& name, DataType type)
        : columnName(name), dataType(type) {}
};

/**
 * @brief 表统计信息
 *
 * 存储整个表的统计数据
 */
struct TableStats {
    QString tableName;                           // 表名
    size_t numRows = 0;                          // 总行数
    size_t numPages = 0;                         // 占用的页面数
    size_t avgRowSize = 0;                       // 平均行大小（字节）

    QMap<QString, ColumnStats> columnStats;      // 列名 → 列统计信息

    // 索引统计
    QMap<QString, size_t> indexSizes;            // 索引名 → 索引大小（页面数）

    TableStats() = default;
    explicit TableStats(const QString& name) : tableName(name) {}

    // 获取列统计
    const ColumnStats* getColumnStats(const QString& columnName) const {
        auto it = columnStats.find(columnName);
        return (it != columnStats.end()) ? &it.value() : nullptr;
    }

    // 估算选择率（0.0 - 1.0）
    double estimateSelectivity(const QString& columnName, const QVariant& value) const;

    // 估算范围查询选择率
    double estimateRangeSelectivity(const QString& columnName,
                                   const QVariant& minVal,
                                   const QVariant& maxVal) const;
};

/**
 * @brief 统计信息收集器
 *
 * 负责收集和维护数据库统计信息
 */
class StatisticsCollector {
public:
    StatisticsCollector(Catalog* catalog, BufferPoolManager* bufferPool);
    ~StatisticsCollector() = default;

    // 收集表的统计信息
    bool collectTableStats(const QString& tableName);

    // 收集所有表的统计信息
    bool collectAllStats();

    // 获取表统计信息
    const TableStats* getTableStats(const QString& tableName) const;

    // 更新表统计信息（增量更新）
    void updateTableStats(const QString& tableName,
                         size_t rowsInserted,
                         size_t rowsDeleted);

    // 清除统计信息
    void clearStats();

    // 保存/加载统计信息
    bool saveStats(const QString& filePath);
    bool loadStats(const QString& filePath);

private:
    Catalog* catalog_;
    BufferPoolManager* bufferPool_;

    QMap<QString, TableStats> tableStats_;  // 表名 → 表统计信息

    // 辅助方法
    bool collectColumnStats(const QString& tableName,
                           const QString& columnName,
                           ColumnStats& stats);

    // 采样方法
    QVector<QVariant> sampleColumn(const QString& tableName,
                                   const QString& columnName,
                                   size_t sampleSize);

    // 计算基数（不同值的数量）
    size_t estimateCardinality(const QVector<QVariant>& samples);

    // 构建直方图
    void buildHistogram(ColumnStats& stats, const QVector<QVariant>& allValues);
};

} // namespace qindb

#endif // QINDB_STATISTICS_H
