#include "qindb/expression_evaluator.h"
#include "qindb/logger.h"

namespace qindb {

ExpressionEvaluator::ExpressionEvaluator(const Catalog* catalog)
    : catalog_(catalog)
{
}

ExpressionEvaluator::~ExpressionEvaluator() = default;

QVariant ExpressionEvaluator::evaluate(const ast::Expression* expr) {
    return evaluateWithRow(expr, nullptr, QVector<QVariant>());
}

QVector<QVariant> ExpressionEvaluator::evaluateList(
    const std::vector<std::unique_ptr<ast::Expression>>& exprs) {
    QVector<QVariant> results;
    results.reserve(exprs.size());

    clearError();

    for (const auto& expr : exprs) {
        QVariant value = evaluate(expr.get());
        if (hasError()) {
            LOG_ERROR(QString("Failed to evaluate expression: %1").arg(lastError_));
            return QVector<QVariant>();
        }
        results.append(value);
    }

    return results;
}

QVariant ExpressionEvaluator::evaluateWithRow(const ast::Expression* expr,
                                              const TableDef* table,
                                              const QVector<QVariant>& row) {
    if (!expr) {
        setError("Null expression");
        return QVariant();
    }

    clearError();

    // Dispatch based on expression type
    if (auto* literal = dynamic_cast<const ast::LiteralExpression*>(expr)) {
        return evaluateLiteral(literal);
    }

    if (auto* binary = dynamic_cast<const ast::BinaryExpression*>(expr)) {
        return evaluateBinary(binary, table, row);
    }

    if (auto* unary = dynamic_cast<const ast::UnaryExpression*>(expr)) {
        return evaluateUnary(unary, table, row);
    }

    if (auto* column = dynamic_cast<const ast::ColumnExpression*>(expr)) {
        return evaluateColumn(column, table, row);
    }

    setError("Unsupported expression type");
    return QVariant();
}

QVariant ExpressionEvaluator::evaluateLiteral(const ast::LiteralExpression* expr) {
    if (!expr) {
        setError("Null literal expression");
        return QVariant();
    }

    // LiteralExpression::value is already a QVariant
    return expr->value;
}

QVariant ExpressionEvaluator::evaluateBinary(const ast::BinaryExpression* expr,
                                             const TableDef* table,
                                             const QVector<QVariant>& row) {
    if (!expr) {
        setError("Null binary expression");
        return QVariant();
    }

    // Evaluate left and right operands
    QVariant left = evaluateWithRow(expr->left.get(), table, row);
    if (hasError()) {
        return QVariant();
    }

    QVariant right = evaluateWithRow(expr->right.get(), table, row);
    if (hasError()) {
        return QVariant();
    }

    // Dispatch based on operator type
    using Op = ast::BinaryOp;

    switch (expr->op) {
        // Arithmetic operators
        case Op::ADD:
        case Op::SUB:
        case Op::MUL:
        case Op::DIV:
        case Op::MOD:
            return evaluateArithmetic(left, right, expr->op);

        // Comparison operators
        case Op::EQ:
        case Op::NE:
        case Op::LT:
        case Op::LE:
        case Op::GT:
        case Op::GE:
            return evaluateComparison(left, right, expr->op);

        // Logical operators
        case Op::AND:
        case Op::OR:
            return evaluateLogical(left, right, expr->op);

        default:
            setError("Unsupported binary operator");
            return QVariant();
    }
}

QVariant ExpressionEvaluator::evaluateUnary(const ast::UnaryExpression* expr,
                                            const TableDef* table,
                                            const QVector<QVariant>& row) {
    if (!expr) {
        setError("Null unary expression");
        return QVariant();
    }

    QVariant operand = evaluateWithRow(expr->expr.get(), table, row);
    if (hasError()) {
        return QVariant();
    }

    using Op = ast::UnaryOp;

    switch (expr->op) {
        case Op::MINUS:
            if (operand.isNull()) {
                return QVariant();
            }
            if (operand.canConvert<qint64>()) {
                return QVariant(-operand.toLongLong());
            }
            if (operand.canConvert<double>()) {
                return QVariant(-operand.toDouble());
            }
            setError("Cannot negate non-numeric value");
            return QVariant();

        case Op::NOT:
            if (operand.isNull()) {
                return QVariant();
            }
            return QVariant(!operand.toBool());

        case Op::IS_NULL:
            return QVariant(operand.isNull());

        case Op::IS_NOT_NULL:
            return QVariant(!operand.isNull());

        default:
            setError("Unsupported unary operator");
            return QVariant();
    }
}

QVariant ExpressionEvaluator::evaluateColumn(const ast::ColumnExpression* expr,
                                             const TableDef* table,
                                             const QVector<QVariant>& row) {
    if (!expr) {
        setError("Null column expression");
        return QVariant();
    }

    if (!table) {
        setError("Cannot evaluate column reference without table context");
        return QVariant();
    }

    // Find column index
    int colIndex = findColumnIndex(table, expr->column);
    if (colIndex < 0) {
        setError(QString("Column '%1' not found in table '%2'")
                    .arg(expr->column)
                    .arg(table->name));
        return QVariant();
    }

    // Check if row has this column
    if (colIndex >= row.size()) {
        setError(QString("Row does not have column at index %1").arg(colIndex));
        return QVariant();
    }

    return row[colIndex];
}

QVariant ExpressionEvaluator::evaluateArithmetic(const QVariant& left,
                                                 const QVariant& right,
                                                 BinaryOp op) {
    // NULL propagation
    if (left.isNull() || right.isNull()) {
        return QVariant();
    }

    using Op = ast::BinaryOp;

    // Try integer arithmetic first
    if (left.canConvert<qint64>() && right.canConvert<qint64>()) {
        qint64 l = left.toLongLong();
        qint64 r = right.toLongLong();

        switch (op) {
            case Op::ADD: return QVariant(l + r);
            case Op::SUB: return QVariant(l - r);
            case Op::MUL: return QVariant(l * r);
            case Op::DIV:
                if (r == 0) {
                    setError("Division by zero");
                    return QVariant();
                }
                return QVariant(l / r);
            case Op::MOD:
                if (r == 0) {
                    setError("Modulo by zero");
                    return QVariant();
                }
                return QVariant(l % r);
            default:
                break;
        }
    }

    // Try floating-point arithmetic
    if (left.canConvert<double>() && right.canConvert<double>()) {
        double l = left.toDouble();
        double r = right.toDouble();

        switch (op) {
            case Op::ADD: return QVariant(l + r);
            case Op::SUB: return QVariant(l - r);
            case Op::MUL: return QVariant(l * r);
            case Op::DIV:
                if (r == 0.0) {
                    setError("Division by zero");
                    return QVariant();
                }
                return QVariant(l / r);
            case Op::MOD:
                setError("Modulo not supported for floating-point numbers");
                return QVariant();
            default:
                break;
        }
    }

    setError("Arithmetic operation requires numeric operands");
    return QVariant();
}

QVariant ExpressionEvaluator::evaluateComparison(const QVariant& left,
                                                 const QVariant& right,
                                                 BinaryOp op) {
    using Op = ast::BinaryOp;

    // NULL handling for equality
    if (op == Op::EQ) {
        if (left.isNull() && right.isNull()) return QVariant(true);
        if (left.isNull() || right.isNull()) return QVariant(false);
    }
    if (op == Op::NE) {
        if (left.isNull() && right.isNull()) return QVariant(false);
        if (left.isNull() || right.isNull()) return QVariant(true);
    }

    // NULL propagation for other comparisons
    if (left.isNull() || right.isNull()) {
        return QVariant();
    }

    // 检查实际类型，优先进行字符串比较
    // 如果两个值都是字符串类型，直接进行字符串比较
    if (left.userType() == QMetaType::QString || right.userType() == QMetaType::QString) {
        QString l = left.toString();
        QString r = right.toString();

        switch (op) {
            case Op::EQ: return QVariant(l == r);
            case Op::NE: return QVariant(l != r);
            case Op::LT: return QVariant(l < r);
            case Op::LE: return QVariant(l <= r);
            case Op::GT: return QVariant(l > r);
            case Op::GE: return QVariant(l >= r);
            default:
                break;
        }
    }

    // Numeric comparison - 只对真正的数值类型进行数值比较
    if ((left.userType() == QMetaType::Int || left.userType() == QMetaType::LongLong ||
         left.userType() == QMetaType::Double || left.userType() == QMetaType::Float) &&
        (right.userType() == QMetaType::Int || right.userType() == QMetaType::LongLong ||
         right.userType() == QMetaType::Double || right.userType() == QMetaType::Float)) {
        double l = left.toDouble();
        double r = right.toDouble();

        switch (op) {
            case Op::EQ: return QVariant(l == r);
            case Op::NE: return QVariant(l != r);
            case Op::LT: return QVariant(l < r);
            case Op::LE: return QVariant(l <= r);
            case Op::GT: return QVariant(l > r);
            case Op::GE: return QVariant(l >= r);
            default:
                break;
        }
    }

    setError("Cannot compare incompatible types");
    return QVariant();
}

QVariant ExpressionEvaluator::evaluateLogical(const QVariant& left,
                                              const QVariant& right,
                                              BinaryOp op) {
    using Op = ast::BinaryOp;

    // SQL three-valued logic (TRUE, FALSE, NULL)
    switch (op) {
        case Op::AND:
            // FALSE AND anything = FALSE
            if (!left.isNull() && !left.toBool()) return QVariant(false);
            if (!right.isNull() && !right.toBool()) return QVariant(false);
            // TRUE AND TRUE = TRUE
            if (!left.isNull() && !right.isNull()) {
                return QVariant(left.toBool() && right.toBool());
            }
            // NULL AND TRUE/NULL = NULL
            return QVariant();

        case Op::OR:
            // TRUE OR anything = TRUE
            if (!left.isNull() && left.toBool()) return QVariant(true);
            if (!right.isNull() && right.toBool()) return QVariant(true);
            // FALSE OR FALSE = FALSE
            if (!left.isNull() && !right.isNull()) {
                return QVariant(left.toBool() || right.toBool());
            }
            // NULL OR FALSE/NULL = NULL
            return QVariant();

        default:
            setError("Unsupported logical operator");
            return QVariant();
    }
}

int ExpressionEvaluator::findColumnIndex(const TableDef* table,
                                         const QString& columnName) const {
    if (!table) {
        return -1;
    }

    for (int i = 0; i < table->columns.size(); ++i) {
        if (table->columns[i].name.compare(columnName, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }

    return -1;
}

void ExpressionEvaluator::setError(const QString& error) {
    lastError_ = error;
}

void ExpressionEvaluator::clearError() {
    lastError_.clear();
}

} // namespace qindb
