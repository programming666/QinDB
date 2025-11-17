#ifndef QINDB_COST_MODEL_H  // 防止重复包含的头文件保护宏
#define QINDB_COST_MODEL_H

#include "qindb/common.h"    // 引入公共定义
#include "qindb/statistics.h" // 引入统计信息相关定义
#include <QString>           // 引入Qt字符串类

namespace qindb {  // 定义命名空间 qindb

/**
 * @brief 成本估算参数
 *
 * 定义各种操作的成本系数，用于查询优化器估算执行计划成本
 */
struct CostParams {
    // I/O 成本相关参数
    double seqPageReadCost = 1.0;      // 顺序页面读取成本
    double randomPageReadCost = 4.0;   // 随机页面读取成本
    double pageWriteCost = 2.0;        // 页面写入成本

    // CPU 成本相关参数
    double tupleProcessCost = 0.01;    // 处理一条记录的成本
    double operatorCost = 0.005;       // 运算符计算成本
    double indexSearchCost = 0.02;     // 索引查找成本

    // 内存成本相关参数
    double memoryUseCost = 0.0001;     // 内存使用成本（每字节）

    // 网络成本（未来使用）
    double networkTransferCost = 0.1;  // 网络传输成本（每字节）

    static CostParams defaults() {
        return CostParams();
    }
};

/**
 * @brief 执行计划节点类型
 */
enum class PlanNodeType {
    SEQ_SCAN,           // 全表扫描
    INDEX_SCAN,         // 索引扫描
    NESTED_LOOP_JOIN,   // 嵌套循环连接
    HASH_JOIN,          // 哈希连接
    SORT_MERGE_JOIN,    // 排序归并连接
    SORT,               // 排序
    AGGREGATE,          // 聚合
    LIMIT               // 限制
};

/**
 * @brief 成本估算结果
 */
struct CostEstimate {
    double totalCost = 0.0;       // 总成本
    double startupCost = 0.0;     // 启动成本
    double ioCost = 0.0;          // I/O 成本
    double cpuCost = 0.0;         // CPU 成本
    size_t estimatedRows = 0;     // 估算的结果行数
    size_t estimatedWidth = 0;    // 估算的行宽度（字节）

    CostEstimate() = default;

    CostEstimate(double total, double startup, double io, double cpu, size_t rows, size_t width)
        : totalCost(total), startupCost(startup), ioCost(io), cpuCost(cpu),
          estimatedRows(rows), estimatedWidth(width) {}

    // 比较成本（用于选择最优计划）
    bool isCheaperThan(const CostEstimate& other) const {
        return totalCost < other.totalCost;
    }
};

/**
 * @brief 成本模型
 *
 * 负责估算各种查询操作的成本
 */
class CostModel {
public:
    explicit CostModel(const CostParams& params = CostParams::defaults())
        : params_(params) {}

    ~CostModel() = default;

    // ========== 扫描成本估算 ==========

    /**
     * @brief 估算全表扫描成本
     * @param stats 表统计信息
     * @param selectivity 选择率（WHERE 条件过滤后的比例）
     */
    CostEstimate estimateSeqScanCost(const TableStats& stats, double selectivity = 1.0) const;

    /**
     * @brief 估算索引扫描成本
     * @param stats 表统计信息
     * @param indexName 索引名称
     * @param selectivity 选择率
     */
    CostEstimate estimateIndexScanCost(const TableStats& stats,
                                      const QString& indexName,
                                      double selectivity) const;

    // ========== 连接成本估算 ==========

    /**
     * @brief 估算嵌套循环连接成本
     * @param outerStats 外表统计信息
     * @param innerStats 内表统计信息
     * @param outerSelectivity 外表选择率
     * @param innerSelectivity 内表选择率
     */
    CostEstimate estimateNestedLoopJoinCost(const TableStats& outerStats,
                                           const TableStats& innerStats,
                                           double outerSelectivity,
                                           double innerSelectivity) const;

    /**
     * @brief 估算哈希连接成本
     */
    CostEstimate estimateHashJoinCost(const TableStats& buildStats,
                                     const TableStats& probeStats,
                                     double buildSelectivity,
                                     double probeSelectivity) const;

    /**
     * @brief 估算排序归并连接成本
     */
    CostEstimate estimateSortMergeJoinCost(const TableStats& leftStats,
                                          const TableStats& rightStats,
                                          double leftSelectivity,
                                          double rightSelectivity) const;

    // ========== 其他操作成本估算 ==========

    /**
     * @brief 估算排序成本
     * @param numRows 行数
     * @param rowWidth 行宽度
     */
    CostEstimate estimateSortCost(size_t numRows, size_t rowWidth) const;

    /**
     * @brief 估算聚合成本
     * @param numRows 输入行数
     * @param numGroups 输出组数
     */
    CostEstimate estimateAggregateCost(size_t numRows, size_t numGroups) const;

    /**
     * @brief 估算 LIMIT 成本
     */
    CostEstimate estimateLimitCost(const CostEstimate& inputCost, size_t limit) const;

    // ========== 辅助方法 ==========

    // 获取成本参数
    const CostParams& getParams() const { return params_; }

    // 设置成本参数
    void setParams(const CostParams& params) { params_ = params; }

private:
    CostParams params_;

    // 辅助计算方法
    double estimateIOCost(size_t numPages, bool sequential) const;
    double estimateCPUCost(size_t numTuples) const;
    double estimateSortCPUCost(size_t numRows) const;  // 排序 CPU 成本: O(n log n)
};

} // namespace qindb

#endif // QINDB_COST_MODEL_H
