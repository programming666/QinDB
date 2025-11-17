#ifndef QINDB_COST_OPTIMIZER_H
#define QINDB_COST_OPTIMIZER_H

#include "qindb/common.h"
#include "qindb/ast.h"
#include "qindb/statistics.h"
#include "qindb/cost_model.h"
#include <QString>
#include <QVector>
#include <memory>

namespace qindb {

// 前向声明 Catalog 类
class Catalog;

/**
 * @brief 执行计划节点
 *
 * 表示查询执行计划的一个节点，可以是表扫描、索引扫描、连接等操作
 */
struct PlanNode {
    PlanNodeType nodeType;                    // 节点类型（表扫描、索引扫描、连接等）
    CostEstimate cost;                        // 成本估算
    QString tableName;                        // 表名（用于扫描节点）
    QString indexName;                        // 索引名（用于索引扫描）
    std::vector<std::unique_ptr<PlanNode>> children;  // 子节点列表

    // 连接相关属性
    QString joinColumn;                       // 连接列名

    // 过滤条件
    std::unique_ptr<ast::Expression> filter;  // WHERE 条件表达式

    // 构造函数
    PlanNode(PlanNodeType type) : nodeType(type) {}

    // 添加子节点
    void addChild(std::unique_ptr<PlanNode> child) {
        children.push_back(std::move(child));
    }
};

/**
 * @brief 基于成本的优化器
 *
 * 使用统计信息和成本模型选择最优执行计划
 */
class CostOptimizer {
public:
    CostOptimizer(Catalog* catalog,
                 StatisticsCollector* statsCollector,
                 const CostModel& costModel = CostModel());

    ~CostOptimizer() = default;

    // ========== 主要接口 ==========

    /**
     * @brief 优化 SELECT 语句
     * @param selectStmt SELECT AST 节点
     * @return 优化后的执行计划
     */
    std::unique_ptr<PlanNode> optimizeSelect(const ast::SelectStatement* selectStmt);

    /**
     * @brief 优化 JOIN 查询
     * @param tables 表列表
     * @param joinConditions 连接条件
     * @return 最优的 JOIN 执行计划
     */
    std::unique_ptr<PlanNode> optimizeJoin(const QVector<QString>& tables,
                                          const QVector<ast::Expression*>& joinConditions);

    // ========== 执行计划生成 ==========

    /**
     * @brief 为表生成最优访问路径
     * @param tableName 表名
     * @param filter WHERE 条件
     * @return 访问计划（SeqScan 或 IndexScan）
     */
    std::unique_ptr<PlanNode> generateAccessPath(const QString& tableName,
                                                 ast::Expression* filter);

    /**
     * @brief 生成连接计划
     * @param leftPlan 左表计划
     * @param rightPlan 右表计划
     * @param joinType 连接类型
     * @return 连接计划
     */
    std::unique_ptr<PlanNode> generateJoinPlan(std::unique_ptr<PlanNode> leftPlan,
                                               std::unique_ptr<PlanNode> rightPlan,
                                               PlanNodeType joinType);

    // ========== 连接顺序优化 ==========

    /**
     * @brief 使用动态规划优化连接顺序
     * @param tables 表列表
     * @return 最优连接顺序和计划
     */
    std::unique_ptr<PlanNode> optimizeJoinOrderDP(const QVector<QString>& tables);

    /**
     * @brief 使用贪心算法优化连接顺序（用于大量表）
     */
    std::unique_ptr<PlanNode> optimizeJoinOrderGreedy(const QVector<QString>& tables);

    // ========== 辅助方法 ==========

    /**
     * @brief 估算表达式的选择率
     * @param expr WHERE 表达式
     * @param tableName 表名
     * @return 选择率 (0.0 - 1.0)
     */
    double estimateSelectivity(ast::Expression* expr, const QString& tableName);

    /**
     * @brief 检查是否可以使用索引
     * @param expr 表达式
     * @param tableName 表名
     * @param indexName 输出：可用的索引名
     * @return 是否可以使用索引
     */
    bool canUseIndex(ast::Expression* expr,
                    const QString& tableName,
                    QString& indexName);

    /**
     * @brief 选择最优的连接算法
     * @param leftStats 左表统计
     * @param rightStats 右表统计
     * @return 最优的连接类型
     */
    PlanNodeType chooseJoinAlgorithm(const TableStats& leftStats,
                                    const TableStats& rightStats);

    // 获取统计信息
    const TableStats* getTableStats(const QString& tableName) const;

private:
    Catalog* catalog_;
    StatisticsCollector* statsCollector_;
    CostModel costModel_;

    // 缓存
    mutable QMap<QString, const TableStats*> statsCache_;

    // 辅助方法
    double estimateBinaryOpSelectivity(ast::BinaryExpression* binOp, const QString& tableName);

    // 提取等值条件中的列和值
    bool extractEquality(ast::Expression* expr, QString& column, QVariant& value);

    // 检查表达式是否引用特定列
    bool referencesColumn(ast::Expression* expr, const QString& columnName);
};

} // namespace qindb

#endif // QINDB_COST_OPTIMIZER_H
