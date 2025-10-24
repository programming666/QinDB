#include "qindb/query_rewriter.h"
#include "qindb/logger.h"
#include "qindb/expression_evaluator.h"
#include <QStringList>

namespace qindb {

QueryRewriter::QueryRewriter() {
    LOG_DEBUG("QueryRewriter initialized");
}

QueryRewriter::~QueryRewriter() {
}

std::unique_ptr<ast::SelectStatement> QueryRewriter::rewrite(const ast::SelectStatement* stmt) {
    if (!stmt) {
        LOG_ERROR("QueryRewriter::rewrite: null statement");
        return nullptr;
    }

    // 重置统计信息
    stats_ = RewriteStats();
    logRewrite("=== Query Rewrite Started ===");

    // 克隆原始语句
    auto rewritten = cloneSelectStatement(stmt);

    // 应用各种优化
    if (constantFoldingEnabled_) {
        applyConstantFolding(rewritten.get());
    }

    if (predicatePushdownEnabled_) {
        applyPredicatePushdown(rewritten.get());
    }

    if (columnPruningEnabled_) {
        applyColumnPruning(rewritten.get());
    }

    if (subqueryUnnesitingEnabled_) {
        applySubqueryUnnesting(rewritten.get());
    }

    logRewrite("=== Query Rewrite Completed ===");
    LOG_DEBUG(QString("Query rewrite stats: predicates=%1, constants=%2, columns=%3, subqueries=%4")
              .arg(stats_.predicatesPushedDown)
              .arg(stats_.constantsFolded)
              .arg(stats_.columnsPruned)
              .arg(stats_.subqueriesUnnested));

    return rewritten;
}

// ========== 谓词下推 ==========

void QueryRewriter::applyPredicatePushdown(ast::SelectStatement* stmt) {
    if (!stmt || !stmt->where) {
        return;
    }

    logRewrite("Applying predicate pushdown...");

    // 目前只处理简单的情况：单表 + JOIN
    if (!stmt->from || stmt->joins.empty()) {
        return;
    }

    // 分离 WHERE 子句中的 AND 连接的谓词
    std::vector<ast::Expression*> predicates = splitConjuncts(stmt->where.get());

    // 获取主表名称
    QString mainTable = getEffectiveTableName(stmt->from.get());

    // 尝试将谓词下推到主表
    std::vector<std::unique_ptr<ast::Expression>> pushedPredicates;
    std::vector<ast::Expression*> remainingPredicates;

    for (auto* pred : predicates) {
        if (onlyReferencesTable(pred, mainTable)) {
            // 可以下推到主表
            pushedPredicates.push_back(cloneExpression(pred));
            stats_.predicatesPushedDown++;
            logRewrite(QString("  Pushed predicate to table '%1': %2")
                      .arg(mainTable, pred->toString()));
        } else {
            // 无法下推，保留在 WHERE 子句
            remainingPredicates.push_back(pred);
        }
    }

    // 如果有下推的谓词，更新 WHERE 子句
    if (!pushedPredicates.empty()) {
        if (remainingPredicates.empty()) {
            // 所有谓词都下推了，WHERE 子句可以移除
            stmt->where.reset();
        } else {
            // 重建 WHERE 子句（只包含无法下推的谓词）
            if (remainingPredicates.size() == 1) {
                stmt->where = cloneExpression(remainingPredicates[0]);
            } else {
                // 合并多个谓词为 AND 表达式
                auto combined = cloneExpression(remainingPredicates[0]);
                for (size_t i = 1; i < remainingPredicates.size(); ++i) {
                    combined = std::make_unique<ast::BinaryExpression>(
                        std::move(combined),
                        ast::BinaryOp::AND,
                        cloneExpression(remainingPredicates[i])
                    );
                }
                stmt->where = std::move(combined);
            }
        }

        // 注意：实际的下推逻辑需要在执行计划生成时应用
        // 这里只是标记哪些谓词可以下推
        // 在真实实现中，应该修改表引用或创建过滤算子
    }

    // 检查 JOIN 条件中是否有可以下推的谓词
    for (auto& join : stmt->joins) {
        if (!join->condition) {
            continue;
        }

        QString joinTable = getEffectiveTableName(join->right.get());
        std::vector<ast::Expression*> joinPredicates = splitConjuncts(join->condition.get());

        for (auto* pred : joinPredicates) {
            if (onlyReferencesTable(pred, joinTable)) {
                stats_.predicatesPushedDown++;
                logRewrite(QString("  Identified pushable predicate in JOIN to '%1': %2")
                          .arg(joinTable, pred->toString()));
            }
        }
    }
}

std::vector<std::unique_ptr<ast::Expression>> QueryRewriter::extractPushablePredicates(
    ast::Expression* expr,
    const QString& tableName)
{
    std::vector<std::unique_ptr<ast::Expression>> result;

    if (!expr) {
        return result;
    }

    // 如果是 AND 表达式，递归处理
    if (auto* binExpr = dynamic_cast<ast::BinaryExpression*>(expr)) {
        if (binExpr->op == ast::BinaryOp::AND) {
            auto leftResults = extractPushablePredicates(binExpr->left.get(), tableName);
            auto rightResults = extractPushablePredicates(binExpr->right.get(), tableName);
            result.insert(result.end(),
                         std::make_move_iterator(leftResults.begin()),
                         std::make_move_iterator(leftResults.end()));
            result.insert(result.end(),
                         std::make_move_iterator(rightResults.begin()),
                         std::make_move_iterator(rightResults.end()));
            return result;
        }
    }

    // 检查表达式是否只引用指定表
    if (onlyReferencesTable(expr, tableName)) {
        result.push_back(cloneExpression(expr));
    }

    return result;
}

bool QueryRewriter::onlyReferencesTable(const ast::Expression* expr, const QString& tableName) {
    if (!expr) {
        return true;  // null 表达式不引用任何表
    }

    // 检查列引用
    if (auto* colExpr = dynamic_cast<const ast::ColumnExpression*>(expr)) {
        // 如果有表限定符，检查是否匹配
        if (!colExpr->table.isEmpty()) {
            return colExpr->table == tableName;
        }
        // 如果没有表限定符，假设引用的是指定表（简化处理）
        return true;
    }

    // 字面值不引用任何表
    if (dynamic_cast<const ast::LiteralExpression*>(expr)) {
        return true;
    }

    // 二元表达式：检查左右子表达式
    if (auto* binExpr = dynamic_cast<const ast::BinaryExpression*>(expr)) {
        return onlyReferencesTable(binExpr->left.get(), tableName) &&
               onlyReferencesTable(binExpr->right.get(), tableName);
    }

    // 一元表达式：检查子表达式
    if (auto* unaryExpr = dynamic_cast<const ast::UnaryExpression*>(expr)) {
        return onlyReferencesTable(unaryExpr->expr.get(), tableName);
    }

    // 聚合表达式：检查参数
    if (auto* aggExpr = dynamic_cast<const ast::AggregateExpression*>(expr)) {
        return onlyReferencesTable(aggExpr->argument.get(), tableName);
    }

    // 函数调用：检查所有参数
    if (auto* funcExpr = dynamic_cast<const ast::FunctionCallExpression*>(expr)) {
        for (const auto& arg : funcExpr->arguments) {
            if (!onlyReferencesTable(arg.get(), tableName)) {
                return false;
            }
        }
        return true;
    }

    // CASE 表达式
    if (auto* caseExpr = dynamic_cast<const ast::CaseExpression*>(expr)) {
        for (const auto& when : caseExpr->whenClauses) {
            if (!onlyReferencesTable(when.condition.get(), tableName) ||
                !onlyReferencesTable(when.result.get(), tableName)) {
                return false;
            }
        }
        if (caseExpr->elseExpression) {
            return onlyReferencesTable(caseExpr->elseExpression.get(), tableName);
        }
        return true;
    }

    // 子查询：保守处理，认为引用了其他表
    if (dynamic_cast<const ast::SubqueryExpression*>(expr)) {
        return false;
    }

    // 未知类型，保守处理
    return false;
}

std::vector<ast::Expression*> QueryRewriter::splitConjuncts(ast::Expression* expr) {
    std::vector<ast::Expression*> result;

    if (!expr) {
        return result;
    }

    // 如果是 AND 表达式，递归分离
    if (auto* binExpr = dynamic_cast<ast::BinaryExpression*>(expr)) {
        if (binExpr->op == ast::BinaryOp::AND) {
            auto left = splitConjuncts(binExpr->left.get());
            auto right = splitConjuncts(binExpr->right.get());
            result.insert(result.end(), left.begin(), left.end());
            result.insert(result.end(), right.begin(), right.end());
            return result;
        }
    }

    // 不是 AND 表达式，作为单个谓词
    result.push_back(expr);
    return result;
}

// ========== 常量折叠 ==========

void QueryRewriter::applyConstantFolding(ast::SelectStatement* stmt) {
    if (!stmt) {
        return;
    }

    logRewrite("Applying constant folding...");

    // 折叠 WHERE 子句
    if (stmt->where) {
        stmt->where = foldConstants(std::move(stmt->where));
    }

    // 折叠 SELECT 列表
    for (auto& expr : stmt->selectList) {
        expr = foldConstants(std::move(expr));
    }

    // 折叠 JOIN 条件
    for (auto& join : stmt->joins) {
        if (join->condition) {
            join->condition = foldConstants(std::move(join->condition));
        }
    }

    // 折叠 HAVING 子句
    if (stmt->groupBy && stmt->groupBy->having) {
        stmt->groupBy->having = foldConstants(std::move(stmt->groupBy->having));
    }

    // 折叠 ORDER BY
    for (auto& orderItem : stmt->orderBy) {
        orderItem.expression = foldConstants(std::move(orderItem.expression));
    }
}

std::unique_ptr<ast::Expression> QueryRewriter::foldConstants(std::unique_ptr<ast::Expression> expr) {
    if (!expr) {
        return expr;
    }

    // 如果已经是字面值，无需折叠
    if (dynamic_cast<ast::LiteralExpression*>(expr.get())) {
        return expr;
    }

    // 递归折叠子表达式
    if (auto* binExpr = dynamic_cast<ast::BinaryExpression*>(expr.get())) {
        binExpr->left = foldConstants(std::move(binExpr->left));
        binExpr->right = foldConstants(std::move(binExpr->right));

        // 如果两边都是常量，计算结果
        if (isConstant(binExpr->left.get()) && isConstant(binExpr->right.get())) {
            try {
                Value result = evaluateConstant(binExpr);
                stats_.constantsFolded++;
                logRewrite(QString("  Folded constant: %1 -> %2")
                          .arg(expr->toString(), result.toString()));
                return std::make_unique<ast::LiteralExpression>(result);
            } catch (...) {
                // 计算失败，保留原表达式
                LOG_WARN(QString("Failed to fold constant expression: %1").arg(expr->toString()));
            }
        }
        return expr;
    }

    if (auto* unaryExpr = dynamic_cast<ast::UnaryExpression*>(expr.get())) {
        unaryExpr->expr = foldConstants(std::move(unaryExpr->expr));

        // 如果操作数是常量，计算结果
        if (isConstant(unaryExpr->expr.get())) {
            try {
                Value result = evaluateConstant(unaryExpr);
                stats_.constantsFolded++;
                logRewrite(QString("  Folded constant: %1 -> %2")
                          .arg(expr->toString(), result.toString()));
                return std::make_unique<ast::LiteralExpression>(result);
            } catch (...) {
                LOG_WARN(QString("Failed to fold constant expression: %1").arg(expr->toString()));
            }
        }
        return expr;
    }

    // 函数调用
    if (auto* funcExpr = dynamic_cast<ast::FunctionCallExpression*>(expr.get())) {
        for (auto& arg : funcExpr->arguments) {
            arg = foldConstants(std::move(arg));
        }

        // 检查所有参数是否都是常量
        bool allConst = true;
        for (const auto& arg : funcExpr->arguments) {
            if (!isConstant(arg.get())) {
                allConst = false;
                break;
            }
        }

        if (allConst) {
            try {
                Value result = evaluateConstant(funcExpr);
                stats_.constantsFolded++;
                logRewrite(QString("  Folded constant function: %1 -> %2")
                          .arg(expr->toString(), result.toString()));
                return std::make_unique<ast::LiteralExpression>(result);
            } catch (...) {
                LOG_WARN(QString("Failed to fold constant function: %1").arg(expr->toString()));
            }
        }
        return expr;
    }

    // CASE 表达式
    if (auto* caseExpr = dynamic_cast<ast::CaseExpression*>(expr.get())) {
        for (auto& when : caseExpr->whenClauses) {
            when.condition = foldConstants(std::move(when.condition));
            when.result = foldConstants(std::move(when.result));
        }
        if (caseExpr->elseExpression) {
            caseExpr->elseExpression = foldConstants(std::move(caseExpr->elseExpression));
        }
        return expr;
    }

    // 其他类型保持不变
    return expr;
}

bool QueryRewriter::isConstant(const ast::Expression* expr) {
    if (!expr) {
        return false;
    }

    // 字面值是常量
    if (dynamic_cast<const ast::LiteralExpression*>(expr)) {
        return true;
    }

    // 列引用不是常量
    if (dynamic_cast<const ast::ColumnExpression*>(expr)) {
        return false;
    }

    // 聚合函数不是常量
    if (dynamic_cast<const ast::AggregateExpression*>(expr)) {
        return false;
    }

    // 子查询不是常量
    if (dynamic_cast<const ast::SubqueryExpression*>(expr)) {
        return false;
    }

    // 二元表达式：两边都是常量才是常量
    if (auto* binExpr = dynamic_cast<const ast::BinaryExpression*>(expr)) {
        return isConstant(binExpr->left.get()) && isConstant(binExpr->right.get());
    }

    // 一元表达式：操作数是常量才是常量
    if (auto* unaryExpr = dynamic_cast<const ast::UnaryExpression*>(expr)) {
        return isConstant(unaryExpr->expr.get());
    }

    // 函数调用：所有参数都是常量才是常量
    if (auto* funcExpr = dynamic_cast<const ast::FunctionCallExpression*>(expr)) {
        for (const auto& arg : funcExpr->arguments) {
            if (!isConstant(arg.get())) {
                return false;
            }
        }
        return true;
    }

    return false;
}

Value QueryRewriter::evaluateConstant(const ast::Expression* expr) {
    if (!expr) {
        return Value();
    }

    // 字面值直接返回
    if (auto* litExpr = dynamic_cast<const ast::LiteralExpression*>(expr)) {
        return litExpr->value;
    }

    // 使用表达式求值器计算
    // 注意：这里传入 nullptr 作为 Catalog，因为我们只计算常量表达式
    ExpressionEvaluator evaluator(nullptr);
    return evaluator.evaluate(expr);
}

// ========== 列裁剪 ==========

void QueryRewriter::applyColumnPruning(ast::SelectStatement* stmt) {
    if (!stmt) {
        return;
    }

    logRewrite("Applying column pruning...");

    // 如果是 SELECT *，无法裁剪
    if (stmt->selectList.size() == 1) {
        if (auto* colExpr = dynamic_cast<ast::ColumnExpression*>(stmt->selectList[0].get())) {
            if (colExpr->column == "*") {
                logRewrite("  Skipping column pruning for SELECT *");
                return;
            }
        }
    }

    // 收集实际引用的列
    QSet<QString> referencedColumns = collectReferencedColumns(stmt);

    // 注意：SELECT 列表中的列总是被"使用"的，因为它们是查询结果的一部分
    // 列裁剪主要用于子查询或视图的优化
    // 这里我们记录引用的列数量作为统计信息
    stats_.columnsPruned = 0;  // 实际裁剪数量取决于具体实现

    logRewrite(QString("  Total referenced columns: %1").arg(referencedColumns.size()));

    // TODO: 在子查询优化时，可以根据外层查询只选择必要的列
}

QSet<QString> QueryRewriter::collectReferencedColumns(const ast::SelectStatement* stmt) {
    QSet<QString> columns;

    if (!stmt) {
        return columns;
    }

    // 收集 SELECT 列表中的列
    for (const auto& expr : stmt->selectList) {
        collectColumnsInExpression(expr.get(), columns);
    }

    // 收集 WHERE 子句中的列
    if (stmt->where) {
        collectColumnsInExpression(stmt->where.get(), columns);
    }

    // 收集 JOIN 条件中的列
    for (const auto& join : stmt->joins) {
        if (join->condition) {
            collectColumnsInExpression(join->condition.get(), columns);
        }
    }

    // 收集 GROUP BY 中的列
    if (stmt->groupBy) {
        for (const auto& expr : stmt->groupBy->expressions) {
            collectColumnsInExpression(expr.get(), columns);
        }
        if (stmt->groupBy->having) {
            collectColumnsInExpression(stmt->groupBy->having.get(), columns);
        }
    }

    // 收集 ORDER BY 中的列
    for (const auto& orderItem : stmt->orderBy) {
        collectColumnsInExpression(orderItem.expression.get(), columns);
    }

    return columns;
}

void QueryRewriter::collectColumnsInExpression(const ast::Expression* expr, QSet<QString>& columns) {
    if (!expr) {
        return;
    }

    // 列引用
    if (auto* colExpr = dynamic_cast<const ast::ColumnExpression*>(expr)) {
        QString fullName = colExpr->table.isEmpty()
            ? colExpr->column
            : colExpr->table + "." + colExpr->column;
        columns.insert(fullName);
        return;
    }

    // 二元表达式
    if (auto* binExpr = dynamic_cast<const ast::BinaryExpression*>(expr)) {
        collectColumnsInExpression(binExpr->left.get(), columns);
        collectColumnsInExpression(binExpr->right.get(), columns);
        return;
    }

    // 一元表达式
    if (auto* unaryExpr = dynamic_cast<const ast::UnaryExpression*>(expr)) {
        collectColumnsInExpression(unaryExpr->expr.get(), columns);
        return;
    }

    // 聚合表达式
    if (auto* aggExpr = dynamic_cast<const ast::AggregateExpression*>(expr)) {
        collectColumnsInExpression(aggExpr->argument.get(), columns);
        return;
    }

    // 函数调用
    if (auto* funcExpr = dynamic_cast<const ast::FunctionCallExpression*>(expr)) {
        for (const auto& arg : funcExpr->arguments) {
            collectColumnsInExpression(arg.get(), columns);
        }
        return;
    }

    // CASE 表达式
    if (auto* caseExpr = dynamic_cast<const ast::CaseExpression*>(expr)) {
        for (const auto& when : caseExpr->whenClauses) {
            collectColumnsInExpression(when.condition.get(), columns);
            collectColumnsInExpression(when.result.get(), columns);
        }
        if (caseExpr->elseExpression) {
            collectColumnsInExpression(caseExpr->elseExpression.get(), columns);
        }
        return;
    }

    // 子查询
    if (auto* subqueryExpr = dynamic_cast<const ast::SubqueryExpression*>(expr)) {
        // 递归收集子查询中的列
        QSet<QString> subqueryColumns = collectReferencedColumns(subqueryExpr->subquery.get());
        columns.unite(subqueryColumns);
        return;
    }
}

// ========== 子查询展开 ==========

void QueryRewriter::applySubqueryUnnesting(ast::SelectStatement* stmt) {
    if (!stmt) {
        return;
    }

    logRewrite("Applying subquery unnesting...");

    // 目前只处理 WHERE 子句中的简单子查询
    // TODO: 实现完整的子查询展开逻辑

    logRewrite("  Subquery unnesting not yet fully implemented");
}

bool QueryRewriter::canUnnesitSubquery(const ast::SubqueryExpression* subquery) {
    if (!subquery || !subquery->subquery) {
        return false;
    }

    auto* stmt = subquery->subquery.get();

    // 简单子查询的特征：
    // 1. 没有聚合函数
    // 2. 没有 GROUP BY
    // 3. 没有 LIMIT/OFFSET
    // 4. 没有 DISTINCT
    // 5. 只有一个表引用

    if (stmt->distinct) {
        return false;
    }

    if (stmt->groupBy) {
        return false;
    }

    if (stmt->limit >= 0 || stmt->offset >= 0) {
        return false;
    }

    if (!stmt->joins.empty()) {
        return false;  // 简化处理，不展开有 JOIN 的子查询
    }

    // TODO: 检查是否有聚合函数

    return true;
}

// ========== 辅助方法 ==========

std::unique_ptr<ast::Expression> QueryRewriter::cloneExpression(const ast::Expression* expr) {
    if (!expr) {
        return nullptr;
    }

    // 字面值
    if (auto* litExpr = dynamic_cast<const ast::LiteralExpression*>(expr)) {
        return std::make_unique<ast::LiteralExpression>(litExpr->value);
    }

    // 列引用
    if (auto* colExpr = dynamic_cast<const ast::ColumnExpression*>(expr)) {
        return std::make_unique<ast::ColumnExpression>(colExpr->table, colExpr->column);
    }

    // 二元表达式
    if (auto* binExpr = dynamic_cast<const ast::BinaryExpression*>(expr)) {
        return std::make_unique<ast::BinaryExpression>(
            cloneExpression(binExpr->left.get()),
            binExpr->op,
            cloneExpression(binExpr->right.get())
        );
    }

    // 一元表达式
    if (auto* unaryExpr = dynamic_cast<const ast::UnaryExpression*>(expr)) {
        return std::make_unique<ast::UnaryExpression>(
            unaryExpr->op,
            cloneExpression(unaryExpr->expr.get())
        );
    }

    // 聚合表达式
    if (auto* aggExpr = dynamic_cast<const ast::AggregateExpression*>(expr)) {
        return std::make_unique<ast::AggregateExpression>(
            aggExpr->func,
            cloneExpression(aggExpr->argument.get()),
            aggExpr->distinct
        );
    }

    // 函数调用
    if (auto* funcExpr = dynamic_cast<const ast::FunctionCallExpression*>(expr)) {
        std::vector<std::unique_ptr<ast::Expression>> clonedArgs;
        for (const auto& arg : funcExpr->arguments) {
            clonedArgs.push_back(cloneExpression(arg.get()));
        }
        return std::make_unique<ast::FunctionCallExpression>(
            funcExpr->name,
            std::move(clonedArgs)
        );
    }

    // CASE 表达式
    if (auto* caseExpr = dynamic_cast<const ast::CaseExpression*>(expr)) {
        std::vector<ast::CaseExpression::WhenClause> clonedWhens;
        for (const auto& when : caseExpr->whenClauses) {
            ast::CaseExpression::WhenClause clonedWhen;
            clonedWhen.condition = cloneExpression(when.condition.get());
            clonedWhen.result = cloneExpression(when.result.get());
            clonedWhens.push_back(std::move(clonedWhen));
        }
        return std::make_unique<ast::CaseExpression>(
            std::move(clonedWhens),
            cloneExpression(caseExpr->elseExpression.get())
        );
    }

    // 子查询
    if (auto* subqueryExpr = dynamic_cast<const ast::SubqueryExpression*>(expr)) {
        return std::make_unique<ast::SubqueryExpression>(
            cloneSelectStatement(subqueryExpr->subquery.get())
        );
    }

    LOG_WARN(QString("Unknown expression type in cloneExpression: %1").arg(expr->toString()));
    return nullptr;
}

std::unique_ptr<ast::SelectStatement> QueryRewriter::cloneSelectStatement(const ast::SelectStatement* stmt) {
    if (!stmt) {
        return nullptr;
    }

    auto cloned = std::make_unique<ast::SelectStatement>();

    cloned->distinct = stmt->distinct;

    // 克隆 SELECT 列表
    for (const auto& expr : stmt->selectList) {
        cloned->selectList.push_back(cloneExpression(expr.get()));
    }
    cloned->selectAliases = stmt->selectAliases;

    // 克隆 FROM 子句
    if (stmt->from) {
        cloned->from = std::make_unique<ast::TableReference>(
            stmt->from->tableName,
            stmt->from->alias
        );
    }

    // 克隆 JOIN 子句
    for (const auto& join : stmt->joins) {
        cloned->joins.push_back(std::make_unique<ast::JoinClause>(
            join->type,
            std::make_unique<ast::TableReference>(
                join->right->tableName,
                join->right->alias
            ),
            cloneExpression(join->condition.get())
        ));
    }

    // 克隆 WHERE 子句
    cloned->where = cloneExpression(stmt->where.get());

    // 克隆 GROUP BY 子句
    if (stmt->groupBy) {
        cloned->groupBy = std::make_unique<ast::GroupByClause>();
        for (const auto& expr : stmt->groupBy->expressions) {
            cloned->groupBy->expressions.push_back(cloneExpression(expr.get()));
        }
        cloned->groupBy->having = cloneExpression(stmt->groupBy->having.get());
    }

    // 克隆 ORDER BY 子句
    for (const auto& orderItem : stmt->orderBy) {
        ast::OrderByItem clonedItem;
        clonedItem.expression = cloneExpression(orderItem.expression.get());
        clonedItem.ascending = orderItem.ascending;
        cloned->orderBy.push_back(std::move(clonedItem));
    }

    cloned->limit = stmt->limit;
    cloned->offset = stmt->offset;

    return cloned;
}

std::unique_ptr<ast::Expression> QueryRewriter::combinePredicates(
    std::vector<std::unique_ptr<ast::Expression>> predicates)
{
    if (predicates.empty()) {
        return nullptr;
    }

    if (predicates.size() == 1) {
        return std::move(predicates[0]);
    }

    auto combined = std::move(predicates[0]);
    for (size_t i = 1; i < predicates.size(); ++i) {
        combined = std::make_unique<ast::BinaryExpression>(
            std::move(combined),
            ast::BinaryOp::AND,
            std::move(predicates[i])
        );
    }

    return combined;
}

QString QueryRewriter::getEffectiveTableName(const ast::TableReference* tableRef) {
    if (!tableRef) {
        return QString();
    }

    // 别名优先
    if (!tableRef->alias.isEmpty()) {
        return tableRef->alias;
    }

    return tableRef->tableName;
}

void QueryRewriter::logRewrite(const QString& message) {
    stats_.rewriteLog += message + "\n";
    LOG_DEBUG(QString("QueryRewriter: %1").arg(message));
}

} // namespace qindb
