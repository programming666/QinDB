#include "qindb/cost_model.h"
#include <cmath>
#include <algorithm>

namespace qindb {

// ========== 扫描成本估算 ==========

CostEstimate CostModel::estimateSeqScanCost(const TableStats& stats, double selectivity) const {
    CostEstimate cost;

    // 估算返回的行数（向上取整，避免小表估算为 0）
    cost.estimatedRows = static_cast<size_t>(std::ceil(stats.numRows * selectivity));
    cost.estimatedWidth = stats.avgRowSize;

    // I/O 成本：需要读取所有页面
    cost.ioCost = estimateIOCost(stats.numPages, true);  // 顺序读取

    // CPU 成本：处理所有元组
    cost.cpuCost = estimateCPUCost(stats.numRows);

    // 启动成本：打开表
    cost.startupCost = params_.seqPageReadCost;

    // 总成本
    cost.totalCost = cost.startupCost + cost.ioCost + cost.cpuCost;

    return cost;
}

CostEstimate CostModel::estimateIndexScanCost(const TableStats& stats,
                                              const QString& indexName,
                                              double selectivity) const {
    Q_UNUSED(indexName);
    CostEstimate cost;

    // 估算返回的行数（向上取整，避免小表估算为 0）
    cost.estimatedRows = static_cast<size_t>(std::ceil(stats.numRows * selectivity));
    cost.estimatedWidth = stats.avgRowSize;

    // 索引大小估算（假设索引占表的20%）
    size_t indexPages = stats.numPages / 5;

    // I/O 成本：
    // 1. 索引查找成本 (B+树遍历，log(N))
    double indexHeight = std::log2(indexPages + 1);
    cost.ioCost = indexHeight * params_.randomPageReadCost;

    // 2. 表数据随机读取成本
    size_t dataPages = std::min(cost.estimatedRows, stats.numPages);
    cost.ioCost += dataPages * params_.randomPageReadCost;

    // CPU 成本：
    // 1. 索引搜索
    cost.cpuCost = indexHeight * params_.indexSearchCost;

    // 2. 处理返回的元组
    cost.cpuCost += estimateCPUCost(cost.estimatedRows);

    // 启动成本：打开索引
    cost.startupCost = params_.indexSearchCost;

    // 总成本
    cost.totalCost = cost.startupCost + cost.ioCost + cost.cpuCost;

    return cost;
}

// ========== 连接成本估算 ==========

CostEstimate CostModel::estimateNestedLoopJoinCost(const TableStats& outerStats,
                                                   const TableStats& innerStats,
                                                   double outerSelectivity,
                                                   double innerSelectivity) const {
    CostEstimate cost;

    // 估算行数
    size_t outerRows = static_cast<size_t>(outerStats.numRows * outerSelectivity);
    size_t innerRows = static_cast<size_t>(innerStats.numRows * innerSelectivity);

    // 结果行数（假设连接选择率为 1/N，N为内表基数）
    double joinSelectivity = (innerStats.numRows > 0) ? (1.0 / innerStats.numRows) : 0.1;
    cost.estimatedRows = static_cast<size_t>(outerRows * innerRows * joinSelectivity);
    cost.estimatedWidth = outerStats.avgRowSize + innerStats.avgRowSize;

    // I/O 成本：
    // 1. 扫描外表一次
    cost.ioCost = estimateIOCost(outerStats.numPages, true);

    // 2. 对外表的每一行，扫描内表一次
    cost.ioCost += outerRows * estimateIOCost(innerStats.numPages, true);

    // CPU 成本：
    // 1. 处理外表
    cost.cpuCost = estimateCPUCost(outerRows);

    // 2. 对每个外表元组，处理内表
    cost.cpuCost += outerRows * estimateCPUCost(innerRows);

    // 3. 连接条件比较
    cost.cpuCost += outerRows * innerRows * params_.operatorCost;

    // 启动成本
    cost.startupCost = params_.seqPageReadCost * 2;

    // 总成本
    cost.totalCost = cost.startupCost + cost.ioCost + cost.cpuCost;

    return cost;
}

CostEstimate CostModel::estimateHashJoinCost(const TableStats& buildStats,
                                             const TableStats& probeStats,
                                             double buildSelectivity,
                                             double probeSelectivity) const {
    CostEstimate cost;

    // 估算行数
    size_t buildRows = static_cast<size_t>(buildStats.numRows * buildSelectivity);
    size_t probeRows = static_cast<size_t>(probeStats.numRows * probeSelectivity);

    // 结果行数
    double joinSelectivity = (buildRows > 0) ? (1.0 / buildRows) : 0.1;
    cost.estimatedRows = static_cast<size_t>(buildRows * probeRows * joinSelectivity);
    cost.estimatedWidth = buildStats.avgRowSize + probeStats.avgRowSize;

    // I/O 成本：
    // 1. 读取构建表（build phase）
    cost.ioCost = estimateIOCost(buildStats.numPages, true);

    // 2. 读取探测表（probe phase）
    cost.ioCost += estimateIOCost(probeStats.numPages, true);

    // CPU 成本：
    // 1. 构建哈希表
    cost.cpuCost = estimateCPUCost(buildRows) * 2;  // 哈希计算额外成本

    // 2. 探测哈希表
    cost.cpuCost += estimateCPUCost(probeRows) * 2;

    // 3. 连接匹配
    cost.cpuCost += cost.estimatedRows * params_.operatorCost;

    // 内存成本：哈希表
    size_t hashTableSize = buildRows * buildStats.avgRowSize;
    cost.cpuCost += hashTableSize * params_.memoryUseCost;

    // 启动成本：构建哈希表
    cost.startupCost = estimateCPUCost(buildRows);

    // 总成本
    cost.totalCost = cost.startupCost + cost.ioCost + cost.cpuCost;

    return cost;
}

CostEstimate CostModel::estimateSortMergeJoinCost(const TableStats& leftStats,
                                                  const TableStats& rightStats,
                                                  double leftSelectivity,
                                                  double rightSelectivity) const {
    CostEstimate cost;

    // 估算行数
    size_t leftRows = static_cast<size_t>(leftStats.numRows * leftSelectivity);
    size_t rightRows = static_cast<size_t>(rightStats.numRows * rightSelectivity);

    // 结果行数
    double joinSelectivity = (rightRows > 0) ? (1.0 / rightRows) : 0.1;
    cost.estimatedRows = static_cast<size_t>(leftRows * rightRows * joinSelectivity);
    cost.estimatedWidth = leftStats.avgRowSize + rightStats.avgRowSize;

    // I/O 成本：读取两个表
    cost.ioCost = estimateIOCost(leftStats.numPages, true);
    cost.ioCost += estimateIOCost(rightStats.numPages, true);

    // 如果需要外部排序，增加写入成本
    if (leftRows * leftStats.avgRowSize > 1024 * 1024) {  // > 1MB
        cost.ioCost += estimateIOCost(leftStats.numPages, true) * params_.pageWriteCost;
    }
    if (rightRows * rightStats.avgRowSize > 1024 * 1024) {
        cost.ioCost += estimateIOCost(rightStats.numPages, true) * params_.pageWriteCost;
    }

    // CPU 成本：
    // 1. 排序两个表
    cost.cpuCost = estimateSortCPUCost(leftRows);
    cost.cpuCost += estimateSortCPUCost(rightRows);

    // 2. 归并扫描
    cost.cpuCost += estimateCPUCost(leftRows + rightRows);

    // 3. 连接匹配
    cost.cpuCost += cost.estimatedRows * params_.operatorCost;

    // 启动成本：排序
    cost.startupCost = estimateSortCPUCost(leftRows) + estimateSortCPUCost(rightRows);

    // 总成本
    cost.totalCost = cost.startupCost + cost.ioCost + cost.cpuCost;

    return cost;
}

// ========== 其他操作成本估算 ==========

CostEstimate CostModel::estimateSortCost(size_t numRows, size_t rowWidth) const {
    CostEstimate cost;

    cost.estimatedRows = numRows;
    cost.estimatedWidth = rowWidth;

    // CPU 成本：O(n log n)
    cost.cpuCost = estimateSortCPUCost(numRows);

    // I/O 成本：如果需要外部排序
    size_t dataSize = numRows * rowWidth;
    size_t numPages = (dataSize + PAGE_SIZE - 1) / PAGE_SIZE;

    if (dataSize > 1024 * 1024) {  // > 1MB，需要外部排序
        // 读取 + 写入
        cost.ioCost = estimateIOCost(numPages, true);
        cost.ioCost += numPages * params_.pageWriteCost;

        // 多路归并的额外成本
        size_t numPasses = static_cast<size_t>(std::log2(numPages / 100 + 1)) + 1;
        cost.ioCost *= numPasses;
    }

    // 启动成本
    cost.startupCost = params_.seqPageReadCost;

    // 总成本
    cost.totalCost = cost.startupCost + cost.ioCost + cost.cpuCost;

    return cost;
}

CostEstimate CostModel::estimateAggregateCost(size_t numRows, size_t numGroups) const {
    CostEstimate cost;

    cost.estimatedRows = numGroups;
    cost.estimatedWidth = 100;  // 估算聚合结果的平均宽度

    // CPU 成本：
    // 1. 处理所有输入行
    cost.cpuCost = estimateCPUCost(numRows);

    // 2. 哈希表操作
    cost.cpuCost += numRows * params_.operatorCost;

    // 3. 聚合计算
    cost.cpuCost += numRows * params_.operatorCost;

    // 启动成本：创建哈希表
    cost.startupCost = params_.operatorCost;

    // I/O 成本：通常在内存中完成
    cost.ioCost = 0;

    // 总成本
    cost.totalCost = cost.startupCost + cost.ioCost + cost.cpuCost;

    return cost;
}

CostEstimate CostModel::estimateLimitCost(const CostEstimate& inputCost, size_t limit) const {
    CostEstimate cost = inputCost;

    // LIMIT 减少输出行数
    cost.estimatedRows = std::min(cost.estimatedRows, limit);

    // 如果是顺序扫描，可以提前停止，减少成本
    if (limit < inputCost.estimatedRows) {
        double ratio = static_cast<double>(limit) / inputCost.estimatedRows;
        cost.ioCost *= ratio;
        cost.cpuCost *= ratio;
        cost.totalCost = cost.startupCost + cost.ioCost + cost.cpuCost;
    }

    return cost;
}

// ========== 辅助方法 ==========

double CostModel::estimateIOCost(size_t numPages, bool sequential) const {
    if (sequential) {
        return numPages * params_.seqPageReadCost;
    } else {
        return numPages * params_.randomPageReadCost;
    }
}

double CostModel::estimateCPUCost(size_t numTuples) const {
    return numTuples * params_.tupleProcessCost;
}

double CostModel::estimateSortCPUCost(size_t numRows) const {
    if (numRows == 0) return 0.0;

    // O(n log n)
    return numRows * std::log2(numRows) * params_.operatorCost;
}

} // namespace qindb
