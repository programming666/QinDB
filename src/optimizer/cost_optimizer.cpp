#include "qindb/cost_optimizer.h"
#include "qindb/catalog.h"
#include "qindb/logger.h"
#include <algorithm>
#include <limits>

namespace qindb {

CostOptimizer::CostOptimizer(Catalog* catalog,
                            StatisticsCollector* statsCollector,
                            const CostModel& costModel)
    : catalog_(catalog)
    , statsCollector_(statsCollector)
    , costModel_(costModel) {
}

// ========== 主要接口 ==========

std::unique_ptr<PlanNode> CostOptimizer::optimizeSelect(const ast::SelectStatement* selectStmt) {
    if (!selectStmt) {
        LOG_ERROR("SelectStatement is null");
        return nullptr;
    }

    // 单表查询
    if (selectStmt->from && selectStmt->joins.empty()) {
        QString tableName = selectStmt->from->tableName;

        // 生成访问路径
        auto plan = generateAccessPath(tableName, selectStmt->where.get());

        // 如果有 LIMIT，应用 LIMIT 优化
        if (selectStmt->limit > 0) {
            auto limitPlan = std::make_unique<PlanNode>(PlanNodeType::LIMIT);
            limitPlan->cost = costModel_.estimateLimitCost(plan->cost, selectStmt->limit);
            limitPlan->addChild(std::move(plan));
            return limitPlan;
        }

        return plan;
    }

    // 多表连接查询
    QVector<QString> tables;
    if (selectStmt->from) {
        tables.append(selectStmt->from->tableName);
    }
    for (const auto& join : selectStmt->joins) {
        tables.append(join->right->tableName);
    }

    // TODO: 提取连接条件
    QVector<ast::Expression*> joinConditions;

    return optimizeJoin(tables, joinConditions);
}

std::unique_ptr<PlanNode> CostOptimizer::optimizeJoin(const QVector<QString>& tables,
                                                      const QVector<ast::Expression*>& joinConditions) {
    if (tables.isEmpty()) {
        return nullptr;
    }

    if (tables.size() == 1) {
        return generateAccessPath(tables[0], nullptr);
    }

    // 使用动态规划优化连接顺序（对于小于8个表）
    if (tables.size() <= 7) {
        return optimizeJoinOrderDP(tables);
    }

    // 对于大量表，使用贪心算法
    return optimizeJoinOrderGreedy(tables);
}

// ========== 执行计划生成 ==========

std::unique_ptr<PlanNode> CostOptimizer::generateAccessPath(const QString& tableName,
                                                            ast::Expression* filter) {
    const TableStats* stats = getTableStats(tableName);
    if (!stats) {
        LOG_WARN(QString("No statistics for table '%1', using SeqScan with default estimates").arg(tableName));
        auto plan = std::make_unique<PlanNode>(PlanNodeType::SEQ_SCAN);
        plan->tableName = tableName;
        // 设置默认估算值（没有统计信息时的后备值）
        plan->cost.totalCost = 100.0;
        plan->cost.estimatedRows = 100;
        plan->cost.estimatedWidth = 100;
        return plan;
    }

    // 估算选择率
    double selectivity = filter ? estimateSelectivity(filter, tableName) : 1.0;

    // 检查是否可以使用索引
    QString indexName;
    if (filter && canUseIndex(filter, tableName, indexName)) {
        // 比较索引扫描和全表扫描的成本
        CostEstimate indexCost = costModel_.estimateIndexScanCost(*stats, indexName, selectivity);
        CostEstimate seqCost = costModel_.estimateSeqScanCost(*stats, selectivity);

        if (indexCost.isCheaperThan(seqCost)) {
            LOG_INFO(QString("Choosing IndexScan on '%1' (cost: %2 vs %3)")
                        .arg(indexName).arg(indexCost.totalCost).arg(seqCost.totalCost));

            auto plan = std::make_unique<PlanNode>(PlanNodeType::INDEX_SCAN);
            plan->tableName = tableName;
            plan->indexName = indexName;
            plan->cost = indexCost;
            // TODO: Clone filter expression (需要实现 Expression copy)
            // if (filter) plan->filter = ...;
            return plan;
        }
    }

    // 使用全表扫描
    LOG_INFO(QString("Choosing SeqScan on '%1'").arg(tableName));
    auto plan = std::make_unique<PlanNode>(PlanNodeType::SEQ_SCAN);
    plan->tableName = tableName;
    plan->cost = costModel_.estimateSeqScanCost(*stats, selectivity);
    // TODO: Clone filter expression (需要实现 Expression copy)
    // if (filter) plan->filter = ...;
    return plan;
}

std::unique_ptr<PlanNode> CostOptimizer::generateJoinPlan(std::unique_ptr<PlanNode> leftPlan,
                                                          std::unique_ptr<PlanNode> rightPlan,
                                                          PlanNodeType joinType) {
    auto joinPlan = std::make_unique<PlanNode>(joinType);

    // 估算连接成本
    const TableStats* leftStats = getTableStats(leftPlan->tableName);
    const TableStats* rightStats = getTableStats(rightPlan->tableName);

    if (leftStats && rightStats) {
        if (joinType == PlanNodeType::NESTED_LOOP_JOIN) {
            joinPlan->cost = costModel_.estimateNestedLoopJoinCost(*leftStats, *rightStats, 1.0, 1.0);
        } else if (joinType == PlanNodeType::HASH_JOIN) {
            joinPlan->cost = costModel_.estimateHashJoinCost(*leftStats, *rightStats, 1.0, 1.0);
        } else if (joinType == PlanNodeType::SORT_MERGE_JOIN) {
            joinPlan->cost = costModel_.estimateSortMergeJoinCost(*leftStats, *rightStats, 1.0, 1.0);
        }
    }

    joinPlan->addChild(std::move(leftPlan));
    joinPlan->addChild(std::move(rightPlan));

    return joinPlan;
}

// ========== 连接顺序优化 ==========

std::unique_ptr<PlanNode> CostOptimizer::optimizeJoinOrderDP(const QVector<QString>& tables) {
    if (tables.isEmpty()) {
        return nullptr;
    }

    if (tables.size() == 1) {
        return generateAccessPath(tables[0], nullptr);
    }

    // 动态规划：dp[subset] = 最优计划
    // 使用位图表示子集
    size_t n = tables.size();
    size_t maxSubset = (1 << n);  // 2^n

    std::vector<std::unique_ptr<PlanNode>> dp(maxSubset);
    std::vector<double> costs(maxSubset, std::numeric_limits<double>::infinity());

    // 初始化：单表
    for (size_t i = 0; i < n; i++) {
        size_t subset = (1 << i);
        dp[subset] = generateAccessPath(tables[i], nullptr);
        costs[subset] = dp[subset]->cost.totalCost;
    }

    // 动态规划：枚举所有子集
    for (size_t subset = 1; subset < maxSubset; subset++) {
        // 跳过单表
        if (__builtin_popcount(subset) <= 1) {
            continue;
        }

        // 枚举分割点
        for (size_t left = (subset - 1) & subset; left > 0; left = (left - 1) & subset) {
            size_t right = subset ^ left;

            if (right == 0 || !dp[left] || !dp[right]) {
                continue;
            }

            // 选择连接算法
            PlanNodeType joinType = chooseJoinAlgorithm(
                *getTableStats(dp[left]->tableName),
                *getTableStats(dp[right]->tableName)
            );

            // 简单克隆计划节点（只复制必要字段，不复制 filter）
            auto leftCopy = std::make_unique<PlanNode>(dp[left]->nodeType);
            leftCopy->tableName = dp[left]->tableName;
            leftCopy->indexName = dp[left]->indexName;
            leftCopy->cost = dp[left]->cost;
            // TODO: 深拷贝 children 和 filter

            auto rightCopy = std::make_unique<PlanNode>(dp[right]->nodeType);
            rightCopy->tableName = dp[right]->tableName;
            rightCopy->indexName = dp[right]->indexName;
            rightCopy->cost = dp[right]->cost;
            // TODO: 深拷贝 children 和 filter

            auto joinPlan = generateJoinPlan(std::move(leftCopy), std::move(rightCopy), joinType);

            if (joinPlan->cost.totalCost < costs[subset]) {
                costs[subset] = joinPlan->cost.totalCost;
                dp[subset] = std::move(joinPlan);
            }
        }
    }

    // 返回完整连接的最优计划
    return std::move(dp[maxSubset - 1]);
}

std::unique_ptr<PlanNode> CostOptimizer::optimizeJoinOrderGreedy(const QVector<QString>& tables) {
    if (tables.isEmpty()) {
        return nullptr;
    }

    if (tables.size() == 1) {
        return generateAccessPath(tables[0], nullptr);
    }

    // 贪心算法：每次选择成本最小的连接
    std::vector<std::unique_ptr<PlanNode>> plans;

    // 初始化：为每个表创建访问计划
    for (const QString& table : tables) {
        plans.push_back(generateAccessPath(table, nullptr));
    }

    // 贪心连接
    while (plans.size() > 1) {
        double minCost = std::numeric_limits<double>::infinity();
        int bestI = -1, bestJ = -1;
        PlanNodeType bestJoinType = PlanNodeType::NESTED_LOOP_JOIN;

        // 找到成本最小的连接对
        for (int i = 0; i < plans.size(); i++) {
            for (int j = i + 1; j < plans.size(); j++) {
                const TableStats* leftStats = getTableStats(plans[i]->tableName);
                const TableStats* rightStats = getTableStats(plans[j]->tableName);

                if (!leftStats || !rightStats) continue;

                // 尝试不同的连接算法
                PlanNodeType joinType = chooseJoinAlgorithm(*leftStats, *rightStats);

                double cost = 0.0;
                if (joinType == PlanNodeType::NESTED_LOOP_JOIN) {
                    cost = costModel_.estimateNestedLoopJoinCost(*leftStats, *rightStats, 1.0, 1.0).totalCost;
                } else if (joinType == PlanNodeType::HASH_JOIN) {
                    cost = costModel_.estimateHashJoinCost(*leftStats, *rightStats, 1.0, 1.0).totalCost;
                }

                if (cost < minCost) {
                    minCost = cost;
                    bestI = i;
                    bestJ = j;
                    bestJoinType = joinType;
                }
            }
        }

        if (bestI == -1 || bestJ == -1) {
            break;  // 无法找到更多连接
        }

        // 执行连接
        auto leftPlan = std::move(plans[bestI]);
        auto rightPlan = std::move(plans[bestJ]);

        auto joinPlan = generateJoinPlan(std::move(leftPlan), std::move(rightPlan), bestJoinType);

        // 重建 plans 列表，排除已使用的索引
        std::vector<std::unique_ptr<PlanNode>> newPlans;
        for (size_t i = 0; i < plans.size(); i++) {
            if (i != static_cast<size_t>(bestI) && i != static_cast<size_t>(bestJ) && plans[i]) {
                newPlans.push_back(std::move(plans[i]));
            }
        }
        newPlans.push_back(std::move(joinPlan));
        plans = std::move(newPlans);
    }

    return plans.empty() ? nullptr : std::move(plans[0]);
}

// ========== 辅助方法 ==========

double CostOptimizer::estimateSelectivity(ast::Expression* expr, const QString& tableName) {
    if (!expr) {
        return 1.0;
    }

    const TableStats* stats = getTableStats(tableName);
    if (!stats) {
        return 0.1;  // 默认选择率
    }

    // 二元操作
    if (auto* binExpr = dynamic_cast<ast::BinaryExpression*>(expr)) {
        return estimateBinaryOpSelectivity(binExpr, tableName);
    }

    return 0.1;
}

bool CostOptimizer::canUseIndex(ast::Expression* expr,
                               const QString& tableName,
                               QString& indexName) {
    if (!expr) {
        return false;
    }

    // 检查是否是等值条件: column = value
    QString column;
    QVariant value;

    if (!extractEquality(expr, column, value)) {
        return false;
    }

    // 检查该列是否有索引
    QVector<IndexDef> indexes = catalog_->getTableIndexes(tableName);

    for (const IndexDef& index : indexes) {
        if (index.columns.size() == 1 && index.columns[0] == column) {
            indexName = index.name;
            return true;
        }

        // 复合索引的第一列也可以使用
        if (!index.columns.isEmpty() && index.columns[0] == column) {
            indexName = index.name;
            return true;
        }
    }

    return false;
}

PlanNodeType CostOptimizer::chooseJoinAlgorithm(const TableStats& leftStats,
                                                const TableStats& rightStats) {
    // 简单启发式规则：

    // 1. 如果一个表很小，使用嵌套循环
    if (leftStats.numRows < 100 || rightStats.numRows < 100) {
        return PlanNodeType::NESTED_LOOP_JOIN;
    }

    // 2. 如果两个表都较大，使用哈希连接
    if (leftStats.numRows > 1000 && rightStats.numRows > 1000) {
        return PlanNodeType::HASH_JOIN;
    }

    // 3. 默认使用嵌套循环
    return PlanNodeType::NESTED_LOOP_JOIN;
}

const TableStats* CostOptimizer::getTableStats(const QString& tableName) const {
    // 先查缓存
    auto it = statsCache_.find(tableName);
    if (it != statsCache_.end()) {
        return it.value();
    }

    // 从统计收集器获取
    const TableStats* stats = statsCollector_->getTableStats(tableName);
    if (stats) {
        statsCache_.insert(tableName, stats);
    }

    return stats;
}

// ========== 私有辅助方法 ==========

double CostOptimizer::estimateBinaryOpSelectivity(ast::BinaryExpression* binExpr, const QString& tableName) {
    const TableStats* stats = getTableStats(tableName);
    if (!stats) {
        return 0.1;
    }

    // 等值条件: column = value
    if (binExpr->op == ast::BinaryOp::EQ) {
        QString column;
        QVariant value;

        if (extractEquality(binExpr, column, value)) {
            return stats->estimateSelectivity(column, value);
        }
    }

    // 范围条件: column > value, column < value, column BETWEEN a AND b
    if (binExpr->op == ast::BinaryOp::GT || binExpr->op == ast::BinaryOp::LT ||
        binExpr->op == ast::BinaryOp::GE || binExpr->op == ast::BinaryOp::LE) {
        // 简化：范围查询选择率估算为 1/3
        return 0.33;
    }

    // AND/OR 逻辑操作
    if (binExpr->op == ast::BinaryOp::AND) {
        double leftSel = estimateSelectivity(binExpr->left.get(), tableName);
        double rightSel = estimateSelectivity(binExpr->right.get(), tableName);
        // 独立性假设：P(A AND B) = P(A) * P(B)
        return leftSel * rightSel;
    } else if (binExpr->op == ast::BinaryOp::OR) {
        double leftSel = estimateSelectivity(binExpr->left.get(), tableName);
        double rightSel = estimateSelectivity(binExpr->right.get(), tableName);
        // P(A OR B) = P(A) + P(B) - P(A AND B)
        return leftSel + rightSel - (leftSel * rightSel);
    }

    return 0.1;
}

bool CostOptimizer::extractEquality(ast::Expression* expr, QString& column, QVariant& value) {
    auto* binExpr = dynamic_cast<ast::BinaryExpression*>(expr);
    if (!binExpr || binExpr->op != ast::BinaryOp::EQ) {
        return false;
    }

    // 检查左边是否是列引用
    auto* leftCol = dynamic_cast<ast::ColumnExpression*>(binExpr->left.get());
    auto* rightLit = dynamic_cast<ast::LiteralExpression*>(binExpr->right.get());

    if (leftCol && rightLit) {
        column = leftCol->column;
        value = rightLit->value;
        return true;
    }

    // 检查右边是否是列引用
    auto* rightCol = dynamic_cast<ast::ColumnExpression*>(binExpr->right.get());
    auto* leftLit = dynamic_cast<ast::LiteralExpression*>(binExpr->left.get());

    if (rightCol && leftLit) {
        column = rightCol->column;
        value = leftLit->value;
        return true;
    }

    return false;
}

bool CostOptimizer::referencesColumn(ast::Expression* expr, const QString& columnName) {
    if (!expr) {
        return false;
    }

    if (auto* colExpr = dynamic_cast<ast::ColumnExpression*>(expr)) {
        return colExpr->column == columnName;
    }

    if (auto* binExpr = dynamic_cast<ast::BinaryExpression*>(expr)) {
        return referencesColumn(binExpr->left.get(), columnName) ||
               referencesColumn(binExpr->right.get(), columnName);
    }

    return false;
}

} // namespace qindb
