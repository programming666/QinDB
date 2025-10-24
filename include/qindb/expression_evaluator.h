#ifndef QINDB_EXPRESSION_EVALUATOR_H
#define QINDB_EXPRESSION_EVALUATOR_H

#include "qindb/ast.h"
#include "qindb/catalog.h"
#include <QVariant>
#include <QVector>
#include <memory>

namespace qindb {

// Using declarations for AST types
using ast::BinaryOp;
using ast::UnaryOp;

/**
 * @brief Expression Evaluator - converts AST Expression trees to QVariant values
 *
 * This class evaluates expressions from the AST, converting them to concrete QVariant
 * values that can be stored in the database or used in WHERE clause evaluation.
 *
 * Supported expression types:
 * - LiteralExpression: Direct values (integers, strings, floats, etc.)
 * - BinaryExpression: Arithmetic and comparison operations
 * - UnaryExpression: Negation, NOT, NULL checks
 * - ColumnExpression: Column references (requires row context)
 */
class ExpressionEvaluator {
public:
    ExpressionEvaluator(const Catalog* catalog);
    ~ExpressionEvaluator();

    /**
     * @brief Evaluate an expression to a QVariant value (no row context)
     *
     * This is used for INSERT VALUES where expressions are constants.
     *
     * @param expr The expression to evaluate
     * @return QVariant The evaluated value, or null QVariant on error
     */
    QVariant evaluate(const ast::Expression* expr);

    /**
     * @brief Evaluate an expression with row context (for WHERE clauses)
     *
     * @param expr The expression to evaluate
     * @param table The table definition for column references
     * @param row The current row values
     * @return QVariant The evaluated value, or null QVariant on error
     */
    QVariant evaluateWithRow(const ast::Expression* expr,
                            const TableDef* table,
                            const QVector<QVariant>& row);

    /**
     * @brief Evaluate a list of expressions (for INSERT VALUES)
     *
     * @param exprs List of expressions to evaluate
     * @return QVector<QVariant> The evaluated values
     */
    QVector<QVariant> evaluateList(const std::vector<std::unique_ptr<ast::Expression>>& exprs);

    /**
     * @brief Get the last error message
     */
    QString getLastError() const { return lastError_; }

    /**
     * @brief Check if evaluator has an error
     */
    bool hasError() const { return !lastError_.isEmpty(); }

private:
    // Evaluate specific expression types
    QVariant evaluateLiteral(const ast::LiteralExpression* expr);
    QVariant evaluateBinary(const ast::BinaryExpression* expr,
                           const TableDef* table,
                           const QVector<QVariant>& row);
    QVariant evaluateUnary(const ast::UnaryExpression* expr,
                          const TableDef* table,
                          const QVector<QVariant>& row);
    QVariant evaluateColumn(const ast::ColumnExpression* expr,
                           const TableDef* table,
                           const QVector<QVariant>& row);

    // Helper functions for binary operations
    QVariant evaluateArithmetic(const QVariant& left, const QVariant& right,
                               BinaryOp op);
    QVariant evaluateComparison(const QVariant& left, const QVariant& right,
                               BinaryOp op);
    QVariant evaluateLogical(const QVariant& left, const QVariant& right,
                            BinaryOp op);

    // Helper to find column index in table
    int findColumnIndex(const TableDef* table, const QString& columnName) const;

    // Error handling
    void setError(const QString& error);
    void clearError();

    const Catalog* catalog_;
    QString lastError_;
};

} // namespace qindb

#endif // QINDB_EXPRESSION_EVALUATOR_H
