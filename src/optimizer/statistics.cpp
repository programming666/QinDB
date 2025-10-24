#include "qindb/statistics.h"
#include "qindb/catalog.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/table_page.h"
#include "qindb/logger.h"
#include "qindb/key_comparator.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace qindb {

// ========== TableStats 实现 ==========

double TableStats::estimateSelectivity(const QString& columnName, const QVariant& value) const {
    const ColumnStats* colStats = getColumnStats(columnName);
    if (!colStats) {
        return 0.1;  // 默认选择率
    }

    // 如果值为 NULL
    if (value.isNull()) {
        if (numRows == 0) return 0.0;
        return static_cast<double>(colStats->numNulls) / numRows;
    }

    // 如果在 MCV (Most Common Values) 中
    QString valueStr = value.toString();
    auto it = colStats->mcv.find(valueStr);
    if (it != colStats->mcv.end()) {
        if (numRows == 0) return 0.0;
        return static_cast<double>(it.value()) / numRows;
    }

    // 使用基数估算
    if (colStats->numDistinctValues > 0) {
        return 1.0 / colStats->numDistinctValues;
    }

    return 0.1;  // 默认选择率
}

double TableStats::estimateRangeSelectivity(const QString& columnName,
                                            const QVariant& minVal,
                                            const QVariant& maxVal) const {
    const ColumnStats* colStats = getColumnStats(columnName);
    if (!colStats) {
        return 0.3;  // 默认范围选择率
    }

    // 如果列的值范围未知
    if (colStats->minValue.isNull() || colStats->maxValue.isNull()) {
        return 0.3;
    }

    // 计算范围重叠比例
    KeyComparator comparator;

    // 检查是否有重叠
    if (comparator.compare(maxVal, colStats->minValue, colStats->dataType) < 0 ||
        comparator.compare(minVal, colStats->maxValue, colStats->dataType) > 0) {
        return 0.0;  // 没有重叠
    }

    // 计算重叠区间
    QVariant actualMin = minVal;
    QVariant actualMax = maxVal;

    if (comparator.compare(minVal, colStats->minValue, colStats->dataType) < 0) {
        actualMin = colStats->minValue;
    }
    if (comparator.compare(maxVal, colStats->maxValue, colStats->dataType) > 0) {
        actualMax = colStats->maxValue;
    }

    // 对于数值类型，计算精确比例
    if (isNumericType(colStats->dataType)) {
        double rangeStart = actualMin.toDouble();
        double rangeEnd = actualMax.toDouble();
        double colMin = colStats->minValue.toDouble();
        double colMax = colStats->maxValue.toDouble();

        if (colMax == colMin) {
            return 1.0;  // 所有值相同
        }

        return (rangeEnd - rangeStart) / (colMax - colMin);
    }

    // 对于其他类型，返回估算值
    return 0.3;
}

// ========== StatisticsCollector 实现 ==========

StatisticsCollector::StatisticsCollector(Catalog* catalog, BufferPoolManager* bufferPool)
    : catalog_(catalog), bufferPool_(bufferPool) {
}

bool StatisticsCollector::collectTableStats(const QString& tableName) {
    if (!catalog_ || !bufferPool_) {
        LOG_ERROR("Catalog or BufferPool is null");
        return false;
    }

    const TableDef* tableDef = catalog_->getTable(tableName);
    if (!tableDef) {
        LOG_ERROR(QString("Table '%1' not found").arg(tableName));
        return false;
    }

    TableStats stats(tableName);

    // 收集行数和页面数
    size_t numRows = 0;
    size_t totalRowSize = 0;
    QVector<PageId> pageIds;

    // 遍历所有页面
    PageId currentPageId = tableDef->firstPageId;
    while (currentPageId != INVALID_PAGE_ID) {
        Page* page = bufferPool_->fetchPage(currentPageId);
        if (!page) {
            LOG_ERROR(QString("Failed to fetch page %1").arg(currentPageId));
            break;
        }

        // 统计该页面的记录数
        uint16_t slotCount = TablePage::getSlotCount(page);
        for (uint16_t i = 0; i < slotCount; i++) {
            QByteArray recordData;
            if (TablePage::getTuple(page, i, recordData)) {
                numRows++;
                totalRowSize += recordData.size();
            }
        }

        pageIds.append(currentPageId);

        // 获取下一个页面
        PageHeader* header = page->getHeader();
        currentPageId = header->nextPageId;

        bufferPool_->unpinPage(page->getPageId(), false);
    }

    stats.numRows = numRows;
    stats.numPages = pageIds.size();
    stats.avgRowSize = (numRows > 0) ? (totalRowSize / numRows) : 0;

    // 收集每列的统计信息
    for (const ColumnDef& colDef : tableDef->columns) {
        ColumnStats colStats(colDef.name, colDef.type);

        if (collectColumnStats(tableName, colDef.name, colStats)) {
            stats.columnStats[colDef.name] = colStats;
        }
    }

    // 保存统计信息
    tableStats_[tableName] = stats;

    LOG_INFO(QString("Collected statistics for table '%1': %2 rows, %3 pages")
                 .arg(tableName).arg(numRows).arg(pageIds.size()));

    return true;
}

bool StatisticsCollector::collectAllStats() {
    QVector<QString> tableNames = catalog_->getAllTableNames();

    for (const QString& tableName : tableNames) {
        if (!collectTableStats(tableName)) {
            LOG_WARN(QString("Failed to collect stats for table '%1'").arg(tableName));
        }
    }

    LOG_INFO(QString("Collected statistics for %1 tables").arg(tableStats_.size()));
    return true;
}

const TableStats* StatisticsCollector::getTableStats(const QString& tableName) const {
    auto it = tableStats_.find(tableName);
    return (it != tableStats_.end()) ? &it.value() : nullptr;
}

void StatisticsCollector::updateTableStats(const QString& tableName,
                                           size_t rowsInserted,
                                           size_t rowsDeleted) {
    auto it = tableStats_.find(tableName);
    if (it != tableStats_.end()) {
        TableStats& stats = it.value();
        stats.numRows += rowsInserted;
        if (stats.numRows >= rowsDeleted) {
            stats.numRows -= rowsDeleted;
        } else {
            stats.numRows = 0;
        }

        // 简单更新：如果变化超过10%，重新收集
        size_t threshold = stats.numRows / 10;
        if (rowsInserted > threshold || rowsDeleted > threshold) {
            collectTableStats(tableName);
        }
    }
}

void StatisticsCollector::clearStats() {
    tableStats_.clear();
}

bool StatisticsCollector::saveStats(const QString& filePath) {
    QJsonObject root;
    QJsonArray tablesArray;

    for (auto it = tableStats_.begin(); it != tableStats_.end(); ++it) {
        const TableStats& stats = it.value();
        QJsonObject tableObj;

        tableObj["tableName"] = stats.tableName;
        tableObj["numRows"] = static_cast<qint64>(stats.numRows);
        tableObj["numPages"] = static_cast<qint64>(stats.numPages);
        tableObj["avgRowSize"] = static_cast<qint64>(stats.avgRowSize);

        // 保存列统计
        QJsonArray columnsArray;
        for (auto colIt = stats.columnStats.begin(); colIt != stats.columnStats.end(); ++colIt) {
            const ColumnStats& colStats = colIt.value();
            QJsonObject colObj;

            colObj["columnName"] = colStats.columnName;
            colObj["dataType"] = static_cast<int>(colStats.dataType);
            colObj["numDistinctValues"] = static_cast<qint64>(colStats.numDistinctValues);
            colObj["numNulls"] = static_cast<qint64>(colStats.numNulls);

            // 保存 min/max (仅对数值类型)
            if (isNumericType(colStats.dataType)) {
                if (!colStats.minValue.isNull()) {
                    colObj["minValue"] = colStats.minValue.toDouble();
                }
                if (!colStats.maxValue.isNull()) {
                    colObj["maxValue"] = colStats.maxValue.toDouble();
                }
            }

            columnsArray.append(colObj);
        }
        tableObj["columns"] = columnsArray;

        tablesArray.append(tableObj);
    }

    root["tables"] = tablesArray;

    // 写入文件
    QJsonDocument doc(root);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR(QString("Failed to open stats file for writing: %1").arg(filePath));
        return false;
    }

    file.write(doc.toJson());
    file.close();

    LOG_INFO(QString("Saved statistics to %1").arg(filePath));
    return true;
}

bool StatisticsCollector::loadStats(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR(QString("Failed to open stats file for reading: %1").arg(filePath));
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        LOG_ERROR("Invalid stats file format");
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray tablesArray = root["tables"].toArray();

    clearStats();

    for (const QJsonValue& val : tablesArray) {
        QJsonObject tableObj = val.toObject();

        TableStats stats(tableObj["tableName"].toString());
        stats.numRows = tableObj["numRows"].toInt();
        stats.numPages = tableObj["numPages"].toInt();
        stats.avgRowSize = tableObj["avgRowSize"].toInt();

        // 加载列统计
        QJsonArray columnsArray = tableObj["columns"].toArray();
        for (const QJsonValue& colVal : columnsArray) {
            QJsonObject colObj = colVal.toObject();

            ColumnStats colStats;
            colStats.columnName = colObj["columnName"].toString();
            colStats.dataType = static_cast<DataType>(colObj["dataType"].toInt());
            colStats.numDistinctValues = colObj["numDistinctValues"].toInt();
            colStats.numNulls = colObj["numNulls"].toInt();

            if (colObj.contains("minValue")) {
                colStats.minValue = colObj["minValue"].toDouble();
            }
            if (colObj.contains("maxValue")) {
                colStats.maxValue = colObj["maxValue"].toDouble();
            }

            stats.columnStats[colStats.columnName] = colStats;
        }

        tableStats_[stats.tableName] = stats;
    }

    LOG_INFO(QString("Loaded statistics from %1").arg(filePath));
    return true;
}

// ========== 私有辅助方法 ==========

bool StatisticsCollector::collectColumnStats(const QString& tableName,
                                             const QString& columnName,
                                             ColumnStats& stats) {
    const TableDef* tableDef = catalog_->getTable(tableName);
    if (!tableDef) {
        return false;
    }

    // 找到列的索引
    int colIndex = -1;
    for (int i = 0; i < tableDef->columns.size(); i++) {
        if (tableDef->columns[i].name == columnName) {
            colIndex = i;
            break;
        }
    }

    if (colIndex == -1) {
        return false;
    }

    const ColumnDef& colDef = tableDef->columns[colIndex];
    stats.dataType = colDef.type;

    // 采样数据
    QVector<QVariant> sampleValues = sampleColumn(tableName, columnName, 1000);

    if (sampleValues.isEmpty()) {
        return true;
    }

    // 统计 NULL 值
    stats.numNulls = 0;
    QVector<QVariant> nonNullValues;
    for (const QVariant& val : sampleValues) {
        if (val.isNull()) {
            stats.numNulls++;
        } else {
            nonNullValues.append(val);
        }
    }

    // 估算基数
    stats.numDistinctValues = estimateCardinality(nonNullValues);

    // 计算 min/max (仅对数值类型)
    if (!nonNullValues.isEmpty()) {
        if (isNumericType(colDef.type)) {
            QVector<double> numericValues;
            for (const QVariant& val : nonNullValues) {
                numericValues.append(val.toDouble());
            }

            std::sort(numericValues.begin(), numericValues.end());
            stats.minValue = numericValues.first();
            stats.maxValue = numericValues.last();
        } else if (isStringType(colDef.type)) {
            QVector<QString> stringValues;
            for (const QVariant& val : nonNullValues) {
                stringValues.append(val.toString());
            }

            std::sort(stringValues.begin(), stringValues.end());
            stats.minValue = stringValues.first();
            stats.maxValue = stringValues.last();
        }
    }

    // 构建 MCV (Most Common Values) - 使用QString作为键
    QHash<QString, size_t> valueCounts;
    for (const QVariant& val : nonNullValues) {
        QString key = val.toString();
        valueCounts[key]++;
    }

    // 选择前10个最常见值
    QVector<QPair<QString, size_t>> sortedCounts;
    for (auto it = valueCounts.begin(); it != valueCounts.end(); ++it) {
        sortedCounts.append(qMakePair(it.key(), it.value()));
    }

    std::sort(sortedCounts.begin(), sortedCounts.end(),
              [](const QPair<QString, size_t>& a, const QPair<QString, size_t>& b) {
                  return a.second > b.second;
              });

    for (int i = 0; i < qMin(10, sortedCounts.size()); i++) {
        stats.mcv[sortedCounts[i].first] = sortedCounts[i].second;
    }

    return true;
}

QVector<QVariant> StatisticsCollector::sampleColumn(const QString& tableName,
                                                    const QString& columnName,
                                                    size_t sampleSize) {
    QVector<QVariant> samples;

    const TableDef* tableDef = catalog_->getTable(tableName);
    if (!tableDef) {
        return samples;
    }

    // 找到列的索引
    int colIndex = -1;
    for (int i = 0; i < tableDef->columns.size(); i++) {
        if (tableDef->columns[i].name == columnName) {
            colIndex = i;
            break;
        }
    }

    if (colIndex == -1) {
        return samples;
    }

    // 遍历表，采样数据
    size_t sampledCount = 0;
    size_t totalCount = 0;

    PageId currentPageId = tableDef->firstPageId;
    while (currentPageId != INVALID_PAGE_ID && sampledCount < sampleSize) {
        Page* page = bufferPool_->fetchPage(currentPageId);
        if (!page) {
            break;
        }

        // 获取页面中的所有记录
        QVector<QVector<QVariant>> records;
        if (TablePage::getAllRecords(page, tableDef, records)) {
            for (const auto& record : records) {
                totalCount++;

                // 采样策略：每N条记录采样1条
                size_t samplingRate = qMax(1ULL, totalCount / sampleSize);
                if (totalCount % samplingRate == 0) {
                    if (colIndex < record.size()) {
                        samples.append(record[colIndex]);
                        sampledCount++;

                        if (sampledCount >= sampleSize) {
                            break;
                        }
                    }
                }
            }
        }

        PageHeader* header = page->getHeader();
        currentPageId = header->nextPageId;

        bufferPool_->unpinPage(page->getPageId(), false);
    }

    return samples;
}

size_t StatisticsCollector::estimateCardinality(const QVector<QVariant>& samples) {
    if (samples.isEmpty()) {
        return 0;
    }

    // 使用 HyperLogLog 的简化版本：统计不同值的数量
    QSet<QString> distinctValues;

    for (const QVariant& val : samples) {
        // 将 QVariant 转换为字符串用于去重
        distinctValues.insert(val.toString());
    }

    return distinctValues.size();
}

void StatisticsCollector::buildHistogram(ColumnStats& stats, const QVector<QVariant>& allValues) {
    // 简化实现：存储采样值
    // 完整实现需要构建等深或等宽直方图
    stats.sampleValues = allValues;
}

} // namespace qindb
