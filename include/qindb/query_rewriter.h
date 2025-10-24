#ifndef QINDB_QUERY_REWRITER_H
#define QINDB_QUERY_REWRITER_H

#include "ast.h"
#include "common.h"
#include <memory>
#include <vector>
#include <QSet>
#include <QString>

namespace qindb {

/**
 * @brief 查询重写引擎
 *
 * 实现多种查询优化技术：
 * 1. 谓词下推（Predicate Pushdown）
 * 2. 常量折叠（Constant Folding）
 * 3. 列裁剪（Column Pruning）
 * 4. 子查询展开（Subquery Unnesting）
 */
class QueryRewriter {
public:
    QueryRewriter();
    ~QueryRewriter();

    /**
     * @brief 重写 SELECT 语句
     * @param stmt 原始 SELECT 语句
     * @return 优化后的 SELECT 语句（新对象）
     */
    std::unique_ptr<ast::SelectStatement> rewrite(const ast::SelectStatement* stmt);

    /**
     * @brief 启用/禁用特定优化
     */
    void setPredicatePushdownEnabled(bool enabled) { predicatePushdownEnabled_ = enabled; }
    void setConstantFoldingEnabled(bool enabled) { constantFoldingEnabled_ = enabled; }
    void setColumnPruningEnabled(bool enabled) { columnPruningEnabled_ = enabled; }
    void setSubqueryUnnesitingEnabled(bool enabled) { subqueryUnnesitingEnabled_ = enabled; }

    /**
     * @brief 获取最后一次重写的统计信息
     */
    struct RewriteStats {
        int predicatesPushedDown = 0;      // 下推的谓词数量
        int constantsFolded = 0;            // 折叠的常量表达式数量
        int columnsPruned = 0;              // 裁剪的列数量
        int subqueriesUnnested = 0;         // 展开的子查询数量
        QString rewriteLog;                 // 重写日志
    };
    const RewriteStats& getStats() const { return stats_; }

private:
    // ========== 谓词下推 ==========
    /**
     * @brief 应用谓词下推优化
     * 将 WHERE 条件中可以下推的谓词移动到 JOIN 之前执行
     */
    void applyPredicatePushdown(ast::SelectStatement* stmt);

    /**
     * @brief 提取可以下推的谓词
     * @param expr WHERE 表达式
     * @param tableName 表名（或别名）
     * @return 可以下推到该表的谓词列表
     */
    std::vector<std::unique_ptr<ast::Expression>> extractPushablePredicates(
        ast::Expression* expr,
        const QString& tableName
    );

    /**
     * @brief 检查表达式是否只引用指定表
     */
    bool onlyReferencesTable(const ast::Expression* expr, const QString& tableName);

    /**
     * @brief 从 AND 连接的表达式中分离出独立的谓词
     */
    std::vector<ast::Expression*> splitConjuncts(ast::Expression* expr);

    // ========== 常量折叠 ==========
    /**
     * @brief 应用常量折叠优化
     * 在编译时计算常量表达式
     */
    void applyConstantFolding(ast::SelectStatement* stmt);

    /**
     * @brief 折叠表达式中的常量
     * @return 折叠后的表达式（可能是原表达式或新的字面值表达式）
     */
    std::unique_ptr<ast::Expression> foldConstants(std::unique_ptr<ast::Expression> expr);

    /**
     * @brief 检查表达式是否为常量
     */
    bool isConstant(const ast::Expression* expr);

    /**
     * @brief 计算常量表达式的值
     */
    Value evaluateConstant(const ast::Expression* expr);

    // ========== 列裁剪 ==========
    /**
     * @brief 应用列裁剪优化
     * 只选择实际需要的列
     */
    void applyColumnPruning(ast::SelectStatement* stmt);

    /**
     * @brief 收集语句中引用的所有列
     */
    QSet<QString> collectReferencedColumns(const ast::SelectStatement* stmt);

    /**
     * @brief 收集表达式中引用的列
     */
    void collectColumnsInExpression(const ast::Expression* expr, QSet<QString>& columns);

    // ========== 子查询展开 ==========
    /**
     * @brief 应用子查询展开优化
     * 将简单的子查询转换为 JOIN
     */
    void applySubqueryUnnesting(ast::SelectStatement* stmt);

    /**
     * @brief 检查子查询是否可以展开
     */
    bool canUnnesitSubquery(const ast::SubqueryExpression* subquery);

    // ========== 辅助方法 ==========
    /**
     * @brief 克隆表达式（深拷贝）
     */
    std::unique_ptr<ast::Expression> cloneExpression(const ast::Expression* expr);

    /**
     * @brief 克隆 SELECT 语句（深拷贝）
     */
    std::unique_ptr<ast::SelectStatement> cloneSelectStatement(const ast::SelectStatement* stmt);

    /**
     * @brief 从表达式列表中移除指定的表达式
     */
    void removeExpression(
        std::vector<std::unique_ptr<ast::Expression>>& list,
        const ast::Expression* toRemove
    );

    /**
     * @brief 合并多个谓词为 AND 表达式
     */
    std::unique_ptr<ast::Expression> combinePredicates(
        std::vector<std::unique_ptr<ast::Expression>> predicates
    );

    /**
     * @brief 获取表的有效名称（别名优先，否则表名）
     */
    QString getEffectiveTableName(const ast::TableReference* tableRef);

    /**
     * @brief 记录重写日志
     */
    void logRewrite(const QString& message);

private:
    // 优化开关
    bool predicatePushdownEnabled_ = true;
    bool constantFoldingEnabled_ = true;
    bool columnPruningEnabled_ = true;
    bool subqueryUnnesitingEnabled_ = true;

    // 统计信息
    RewriteStats stats_;
};

} // namespace qindb

#endif // QINDB_QUERY_REWRITER_H
