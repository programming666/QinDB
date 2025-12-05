#include "qindb/ast.h"

namespace qindb {
namespace ast {

// Expression implementations
QString LiteralExpression::toString() const {
    return value.toString();
}

QString ColumnExpression::toString() const {
    if (table.isEmpty()) {
        return column;
    }
    return table + "." + column;
}

QString binaryOpToString(BinaryOp op) {
    switch (op) {
        case BinaryOp::ADD: return "+";
        case BinaryOp::SUB: return "-";
        case BinaryOp::MUL: return "*";
        case BinaryOp::DIV: return "/";
        case BinaryOp::MOD: return "%";
        case BinaryOp::EQ: return "=";
        case BinaryOp::NE: return "!=";
        case BinaryOp::LT: return "<";
        case BinaryOp::LE: return "<=";
        case BinaryOp::GT: return ">";
        case BinaryOp::GE: return ">=";
        case BinaryOp::AND: return "AND";
        case BinaryOp::OR: return "OR";
        case BinaryOp::LIKE: return "LIKE";
        case BinaryOp::IN: return "IN";
        default: return "?";
    }
}

QString BinaryExpression::toString() const {
    return "(" + left->toString() + " " + binaryOpToString(op) + " " + right->toString() + ")";
}

QString unaryOpToString(UnaryOp op) {
    switch (op) {
        case UnaryOp::NOT: return "NOT";
        case UnaryOp::MINUS: return "-";
        case UnaryOp::PLUS: return "+";
        case UnaryOp::IS_NULL: return "IS NULL";
        case UnaryOp::IS_NOT_NULL: return "IS NOT NULL";
        default: return "?";
    }
}

QString UnaryExpression::toString() const {
    if (op == UnaryOp::IS_NULL || op == UnaryOp::IS_NOT_NULL) {
        return expr->toString() + " " + unaryOpToString(op);
    }
    return unaryOpToString(op) + " " + expr->toString();
}

QString aggFuncToString(AggFunc func) {
    switch (func) {
        case AggFunc::COUNT: return "COUNT";
        case AggFunc::SUM: return "SUM";
        case AggFunc::AVG: return "AVG";
        case AggFunc::MIN: return "MIN";
        case AggFunc::MAX: return "MAX";
        default: return "?";
    }
}

QString AggregateExpression::toString() const {
    QString result = aggFuncToString(func) + "(";
    if (distinct) result += "DISTINCT ";
    result += argument->toString() + ")";
    return result;
}

QString FunctionCallExpression::toString() const {
    QString result = name + "(";
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0) result += ", ";
        result += arguments[i]->toString();
    }
    result += ")";
    return result;
}

QString CaseExpression::toString() const {
    QString result = "CASE";
    for (const auto& when : whenClauses) {
        result += " WHEN " + when.condition->toString();
        result += " THEN " + when.result->toString();
    }
    if (elseExpression) {
        result += " ELSE " + elseExpression->toString();
    }
    result += " END";
    return result;
}

SubqueryExpression::SubqueryExpression(std::unique_ptr<SelectStatement> query)
    : subquery(std::move(query)) {}

QString SubqueryExpression::toString() const {
    return "(" + subquery->toString() + ")";
}

QString MatchExpression::toString() const {
    QString result = "MATCH(";
    for (int i = 0; i < columns.size(); ++i) {
        if (i > 0) result += ", ";
        result += columns[i];
    }
    result += ") AGAINST('" + query + "'";
    if (mode == MatchMode::BOOLEAN) {
        result += " IN BOOLEAN MODE";
    }
    result += ")";
    return result;
}

// TableReference
QString TableReference::toString() const {
    if (alias.isEmpty()) {
        return tableName;
    }
    return tableName + " AS " + alias;
}

// JoinClause
QString joinTypeToString(JoinType type) {
    switch (type) {
        case JoinType::INNER: return "INNER JOIN";
        case JoinType::LEFT: return "LEFT JOIN";
        case JoinType::RIGHT: return "RIGHT JOIN";
        case JoinType::FULL: return "FULL JOIN";
        case JoinType::CROSS: return "CROSS JOIN";
        default: return "JOIN";
    }
}

QString JoinClause::toString() const {
    QString result = joinTypeToString(type) + " " + right->toString();
    if (condition) {
        result += " ON " + condition->toString();
    }
    return result;
}

// SelectStatement
QString SelectStatement::toString() const {
    QString result = "SELECT ";
    if (distinct) result += "DISTINCT ";

    for (size_t i = 0; i < selectList.size(); ++i) {
        if (i > 0) result += ", ";
        result += selectList[i]->toString();
        if (i < static_cast<size_t>(selectAliases.size()) && !selectAliases[static_cast<qsizetype>(i)].isEmpty()) {
            result += " AS " + selectAliases[static_cast<qsizetype>(i)];
        }
    }

    if (from) {
        result += " FROM " + from->toString();
    }

    for (const auto& join : joins) {
        result += " " + join->toString();
    }

    if (where) {
        result += " WHERE " + where->toString();
    }

    if (groupBy) {
        result += " GROUP BY ";
        for (size_t i = 0; i < groupBy->expressions.size(); ++i) {
            if (i > 0) result += ", ";
            result += groupBy->expressions[i]->toString();
        }
        if (groupBy->having) {
            result += " HAVING " + groupBy->having->toString();
        }
    }

    if (!orderBy.empty()) {
        result += " ORDER BY ";
        for (size_t i = 0; i < orderBy.size(); ++i) {
            if (i > 0) result += ", ";
            result += orderBy[i].expression->toString();
            result += orderBy[i].ascending ? " ASC" : " DESC";
        }
    }

    if (limit >= 0) {
        result += " LIMIT " + QString::number(limit);
    }

    if (offset >= 0) {
        result += " OFFSET " + QString::number(offset);
    }

    return result;
}

// InsertStatement
QString InsertStatement::toString() const {
    QString result = "INSERT INTO " + tableName;

    if (!columns.isEmpty()) {
        result += " (";
        for (qsizetype i = 0; i < columns.size(); ++i) {
            if (i > 0) result += ", ";
            result += columns[i];
        }
        result += ")";
    }

    if (selectQuery) {
        result += " " + selectQuery->toString();
    } else {
        result += " VALUES ";
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) result += ", ";
            result += "(";
            for (size_t j = 0; j < values[i].size(); ++j) {
                if (j > 0) result += ", ";
                result += values[i][j]->toString();
            }
            result += ")";
        }
    }

    return result;
}

// UpdateStatement
QString UpdateStatement::toString() const {
    QString result = "UPDATE " + tableName + " SET ";

    for (qsizetype i = 0; i < static_cast<qsizetype>(assignments.size()); ++i) {
        if (i > 0) result += ", ";
        result += assignments[i].first + " = " + assignments[i].second->toString();
    }

    if (where) {
        result += " WHERE " + where->toString();
    }

    return result;
}

// DeleteStatement
QString DeleteStatement::toString() const {
    QString result = "DELETE FROM " + tableName;
    if (where) {
        result += " WHERE " + where->toString();
    }
    return result;
}

// CreateTableStatement
QString CreateTableStatement::toString() const {
    QString result = "CREATE TABLE ";
    if (ifNotExists) result += "IF NOT EXISTS ";
    result += tableName + " (";

    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) result += ", ";
        const auto& col = columns[i];
        result += col.name + " ";
        // Add type info (simplified)
        result += "[TYPE]";
    }

    result += ")";
    return result;
}

// DropTableStatement
QString DropTableStatement::toString() const {
    QString result = "DROP TABLE ";
    if (ifExists) result += "IF EXISTS ";
    result += tableName;
    return result;
}

// AlterTableStatement
QString AlterTableStatement::toString() const {
    return "ALTER TABLE " + tableName + " [operation]";
}

// CreateIndexStatement
QString CreateIndexStatement::toString() const {
    QString result = "CREATE ";
    if (unique) result += "UNIQUE ";
    result += "INDEX ";
    result += "INDEX " + indexName + " ON " + tableName;
    result += indexName + " ON " + tableName + " (";
    for (qsizetype i = 0; i < columns.size(); ++i) {
        if (i > 0) result += ", ";
        result += columns[i];
    }
    result += ")";
    return result;
}

// DropIndexStatement
QString DropIndexStatement::toString() const {
    QString result = "DROP INDEX ";
    if (ifExists) result += "IF EXISTS ";
    result += indexName + " ON " + tableName;
    return result;
}

// ShowTablesStatement
QString ShowTablesStatement::toString() const {
    return "SHOW TABLES";
}

// ShowIndexesStatement
QString ShowIndexesStatement::toString() const {
    return "SHOW INDEXES FROM " + tableName;
}

// BeginTransactionStatement
QString BeginTransactionStatement::toString() const {
    return "BEGIN TRANSACTION";
}

// CommitStatement
QString CommitStatement::toString() const {
    return "COMMIT";
}

// RollbackStatement
QString RollbackStatement::toString() const {
    return "ROLLBACK";
}

// CreateDatabaseStatement
QString CreateDatabaseStatement::toString() const {
    QString result = "CREATE DATABASE ";
    if (ifNotExists) result += "IF NOT EXISTS ";
    result += databaseName;
    return result;
}

// DropDatabaseStatement
QString DropDatabaseStatement::toString() const {
    QString result = "DROP DATABASE ";
    if (ifExists) result += "IF EXISTS ";
    result += databaseName;
    return result;
}

// UseDatabaseStatement
QString UseDatabaseStatement::toString() const {
    return "USE " + databaseName;
}

// ShowDatabasesStatement
QString ShowDatabasesStatement::toString() const {
    return "SHOW DATABASES";
}

// SaveStatement
QString SaveStatement::toString() const {
    return "SAVE";
}

// VacuumStatement
QString VacuumStatement::toString() const {
    if (tableName.isEmpty()) {
        return "VACUUM";
    }
    return "VACUUM " + tableName;
}

// AnalyzeStatement
QString AnalyzeStatement::toString() const {
    if (tableName.isEmpty()) {
        return "ANALYZE";
    }
    return "ANALYZE TABLE " + tableName;
}

// ExplainStatement
QString ExplainStatement::toString() const {
    if (query) {
        return "EXPLAIN " + query->toString();
    }
    return "EXPLAIN";
}

// CreateUserStatement
QString CreateUserStatement::toString() const {
    QString result = "CREATE USER " + username + " IDENTIFIED BY '" + password + "'";
    if (isAdmin) {
        result += " WITH ADMIN";
    }
    return result;
}

// DropUserStatement
QString DropUserStatement::toString() const {
    return "DROP USER " + username;
}

// AlterUserStatement
QString AlterUserStatement::toString() const {
    return "ALTER USER " + username + " IDENTIFIED BY '" + newPassword + "'";
}

// Helper function to convert PrivilegeType to string
static QString privilegeTypeToString(PrivilegeType type) {
    switch (type) {
        case PrivilegeType::SELECT: return "SELECT";
        case PrivilegeType::INSERT: return "INSERT";
        case PrivilegeType::UPDATE: return "UPDATE";
        case PrivilegeType::DELETE_PRIV: return "DELETE";
        case PrivilegeType::ALL: return "ALL";
        default: return "UNKNOWN";
    }
}

// GrantStatement
QString GrantStatement::toString() const {
    QString result = "GRANT " + privilegeTypeToString(privilegeType) + " ON ";

    if (!databaseName.isEmpty()) {
        result += databaseName;
        if (!tableName.isEmpty()) {
            result += "." + tableName;
        } else {
            result += ".*";
        }
    } else {
        result += "*.*";
    }

    result += " TO " + username;

    if (withGrantOption) {
        result += " WITH GRANT OPTION";
    }

    return result;
}

// RevokeStatement
QString RevokeStatement::toString() const {
    QString result = "REVOKE " + privilegeTypeToString(privilegeType) + " ON ";

    if (!databaseName.isEmpty()) {
        result += databaseName;
        if (!tableName.isEmpty()) {
            result += "." + tableName;
        } else {
            result += ".*";
        }
    } else {
        result += "*.*";
    }

    result += " FROM " + username;

    return result;
}

} // namespace ast
} // namespace qindb

