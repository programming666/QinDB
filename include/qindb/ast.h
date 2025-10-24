#ifndef QINDB_AST_H
#define QINDB_AST_H

#include "common.h"
#include <QList>
#include <memory>
#include <vector>

namespace qindb {
namespace ast {

// AST 节点基类
class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual QString toString() const = 0;
};

// 表达式基类
class Expression : public ASTNode {
public:
    virtual ~Expression() = default;
};

// 字面值表达式
class LiteralExpression : public Expression {
public:
    explicit LiteralExpression(const Value& val) : value(val) {}
    QString toString() const override;
    Value value;
};

// 列引用表达式
class ColumnExpression : public Expression {
public:
    ColumnExpression(const QString& tbl, const QString& col)
        : table(tbl), column(col) {}
    QString toString() const override;
    QString table;
    QString column;
};

// 二元操作符
enum class BinaryOp {
    ADD, SUB, MUL, DIV, MOD,
    EQ, NE, LT, LE, GT, GE,
    AND, OR,
    LIKE, IN
};

// 二元表达式
class BinaryExpression : public Expression {
public:
    BinaryExpression(std::unique_ptr<Expression> l,
                     BinaryOp o,
                     std::unique_ptr<Expression> r)
        : left(std::move(l)), op(o), right(std::move(r)) {}
    QString toString() const override;
    std::unique_ptr<Expression> left;
    BinaryOp op;
    std::unique_ptr<Expression> right;
};

// 一元操作符
enum class UnaryOp {
    NOT, MINUS, PLUS, IS_NULL, IS_NOT_NULL
};

// 一元表达式
class UnaryExpression : public Expression {
public:
    UnaryExpression(UnaryOp o, std::unique_ptr<Expression> e)
        : op(o), expr(std::move(e)) {}
    QString toString() const override;
    UnaryOp op;
    std::unique_ptr<Expression> expr;
};

// 聚合函数
enum class AggFunc {
    COUNT, SUM, AVG, MIN, MAX
};

// 聚合表达式
class AggregateExpression : public Expression {
public:
    AggregateExpression(AggFunc f, std::unique_ptr<Expression> arg, bool dist = false)
        : func(f), argument(std::move(arg)), distinct(dist) {}
    QString toString() const override;
    AggFunc func;
    std::unique_ptr<Expression> argument;
    bool distinct;
};

// 函数调用表达式
class FunctionCallExpression : public Expression {
public:
    FunctionCallExpression(const QString& n, std::vector<std::unique_ptr<Expression>> args)
        : name(n), arguments(std::move(args)) {}
    QString toString() const override;
    QString name;
    std::vector<std::unique_ptr<Expression>> arguments;
};

// CASE 表达式
class CaseExpression : public Expression {
public:
    struct WhenClause {
        std::unique_ptr<Expression> condition;
        std::unique_ptr<Expression> result;
    };

    CaseExpression(std::vector<WhenClause> whens, std::unique_ptr<Expression> elseExpr)
        : whenClauses(std::move(whens)), elseExpression(std::move(elseExpr)) {}
    QString toString() const override;
    std::vector<WhenClause> whenClauses;
    std::unique_ptr<Expression> elseExpression;
};

// 子查询表达式
class SubqueryExpression : public Expression {
public:
    explicit SubqueryExpression(std::unique_ptr<class SelectStatement> query);
    QString toString() const override;
    std::unique_ptr<class SelectStatement> subquery;
};

// 全文搜索模式
enum class MatchMode {
    NATURAL_LANGUAGE,  // 自然语言模式（默认）
    BOOLEAN            // 布尔模式（AND/OR）
};

// MATCH...AGAINST 表达式
class MatchExpression : public Expression {
public:
    MatchExpression(const QStringList& cols, const QString& q, MatchMode m = MatchMode::NATURAL_LANGUAGE)
        : columns(cols), query(q), mode(m) {}
    QString toString() const override;
    QStringList columns;  // 列名列表
    QString query;        // 查询字符串
    MatchMode mode;       // 搜索模式
};

// 列定义
struct ColumnDefinition {
    QString name;
    DataType type;
    int length = 0;  // 用于 CHAR, VARCHAR, DECIMAL
    int precision = 0;  // 用于 DECIMAL
    bool notNull = false;
    bool primaryKey = false;
    bool unique = false;
    bool autoIncrement = false;
    std::unique_ptr<Expression> defaultValue;
    QString checkConstraint;
};

// 索引类型
enum class IndexType {
    BTREE,
    HASH,
    FULLTEXT
};

// 索引定义
struct IndexDefinition {
    QString name;
    IndexType type = IndexType::BTREE;
    QStringList columns;
    bool unique = false;
};

// JOIN 类型
enum class JoinType {
    INNER,
    LEFT,
    RIGHT,
    FULL,
    CROSS
};

// 表引用
class TableReference : public ASTNode {
public:
    explicit TableReference(const QString& tbl, const QString& als = "")
        : tableName(tbl), alias(als) {}
    QString toString() const override;
    QString tableName;
    QString alias;
};

// JOIN 子句
class JoinClause : public ASTNode {
public:
    JoinClause(JoinType t,
               std::unique_ptr<TableReference> r,
               std::unique_ptr<Expression> cond)
        : type(t), right(std::move(r)), condition(std::move(cond)) {}
    QString toString() const override;
    JoinType type;
    std::unique_ptr<TableReference> right;
    std::unique_ptr<Expression> condition;
};

// ORDER BY 项
struct OrderByItem {
    std::unique_ptr<Expression> expression;
    bool ascending = true;
};

// GROUP BY 子句
struct GroupByClause {
    std::vector<std::unique_ptr<Expression>> expressions;
    std::unique_ptr<Expression> having;
};

// SELECT 语句
class SelectStatement : public ASTNode {
public:
    QString toString() const override;

    bool distinct = false;
    std::vector<std::unique_ptr<Expression>> selectList;
    QStringList selectAliases;
    std::unique_ptr<TableReference> from;
    std::vector<std::unique_ptr<JoinClause>> joins;
    std::unique_ptr<Expression> where;
    std::unique_ptr<GroupByClause> groupBy;
    std::vector<OrderByItem> orderBy;
    int limit = -1;
    int offset = -1;
};

// INSERT 语句
class InsertStatement : public ASTNode {
public:
    QString toString() const override;

    QString tableName;
    QStringList columns;
    std::vector<std::vector<std::unique_ptr<Expression>>> values;
    std::unique_ptr<SelectStatement> selectQuery;
};

// UPDATE 语句
class UpdateStatement : public ASTNode {
public:
    QString toString() const override;

    QString tableName;
    std::vector<std::pair<QString, std::unique_ptr<Expression>>> assignments;
    std::unique_ptr<Expression> where;
};

// DELETE 语句
class DeleteStatement : public ASTNode {
public:
    QString toString() const override;

    QString tableName;
    std::unique_ptr<Expression> where;
};

// CREATE TABLE 语句
class CreateTableStatement : public ASTNode {
public:
    QString toString() const override;

    QString tableName;
    std::vector<ColumnDefinition> columns;
    std::vector<IndexDefinition> indexes;
    bool ifNotExists = false;
};

// DROP TABLE 语句
class DropTableStatement : public ASTNode {
public:
    QString toString() const override;

    QString tableName;
    bool ifExists = false;
};

// ALTER TABLE 操作类型
enum class AlterOperation {
    ADD_COLUMN,
    DROP_COLUMN,
    MODIFY_COLUMN,
    RENAME_COLUMN,
    ADD_INDEX,
    DROP_INDEX
};

// ALTER TABLE 语句
class AlterTableStatement : public ASTNode {
public:
    QString toString() const override;

    QString tableName;
    AlterOperation operation;
    QString columnName;
    QString newColumnName;
    ColumnDefinition columnDef;
    IndexDefinition indexDef;
};

// CREATE INDEX 语句
class CreateIndexStatement : public ASTNode {
public:
    QString toString() const override;

    QString indexName;
    QString tableName;
    IndexType type = IndexType::BTREE;
    QStringList columns;
    bool unique = false;
    bool ifNotExists = false;
};

// DROP INDEX 语句
class DropIndexStatement : public ASTNode {
public:
    QString toString() const override;

    QString indexName;
    QString tableName;
    bool ifExists = false;
};

// SHOW TABLES 语句
class ShowTablesStatement : public ASTNode {
public:
    QString toString() const override;
    QString format = "table";  // table, json, csv
};

// SHOW INDEXES 语句
class ShowIndexesStatement : public ASTNode {
public:
    QString toString() const override;
    QString tableName;
    QString format = "table";  // table, json, csv
};

// BEGIN TRANSACTION 语句
class BeginTransactionStatement : public ASTNode {
public:
    QString toString() const override;
};

// COMMIT 语句
class CommitStatement : public ASTNode {
public:
    QString toString() const override;
};

// ROLLBACK 语句
class RollbackStatement : public ASTNode {
public:
    QString toString() const override;
};

// CREATE DATABASE 语句
class CreateDatabaseStatement : public ASTNode {
public:
    QString toString() const override;

    QString databaseName;
    bool ifNotExists = false;
};

// DROP DATABASE 语句
class DropDatabaseStatement : public ASTNode {
public:
    QString toString() const override;

    QString databaseName;
    bool ifExists = false;
};

// USE DATABASE 语句
class UseDatabaseStatement : public ASTNode {
public:
    QString toString() const override;

    QString databaseName;
};

// SHOW DATABASES 语句
class ShowDatabasesStatement : public ASTNode {
public:
    QString toString() const override;
    QString format = "table";  // table, json, csv
};

// SAVE 语句（保存数据库到磁盘）
class SaveStatement : public ASTNode {
public:
    QString toString() const override;
};

// VACUUM 语句（垃圾回收）
class VacuumStatement : public ASTNode {
public:
    QString toString() const override;

    QString tableName;  // 表名（如果为空则清理所有表）
};

// ANALYZE 语句（收集统计信息）
class AnalyzeStatement : public ASTNode {
public:
    QString toString() const override;

    QString tableName;  // 表名（如果为空则收集所有表的统计信息）
};

// EXPLAIN 语句（显示执行计划）
class ExplainStatement : public ASTNode {
public:
    QString toString() const override;

    std::unique_ptr<SelectStatement> query;  // 要解释的查询
};

// CREATE USER 语句
class CreateUserStatement : public ASTNode {
public:
    QString toString() const override;

    QString username;      // 用户名
    QString password;      // 密码
    bool isAdmin = false;  // 是否为管理员
};

// DROP USER 语句
class DropUserStatement : public ASTNode {
public:
    QString toString() const override;

    QString username;  // 用户名
};

// ALTER USER 语句
class AlterUserStatement : public ASTNode {
public:
    QString toString() const override;

    QString username;      // 用户名
    QString newPassword;   // 新密码
};

// 权限类型
enum class PrivilegeType {
    SELECT,
    INSERT,
    UPDATE,
    DELETE_PRIV,  // DELETE是关键字，使用DELETE_PRIV避免冲突
    ALL
};

// GRANT 语句
// GRANT privilege_type ON database.table TO username [WITH GRANT OPTION]
class GrantStatement : public ASTNode {
public:
    QString toString() const override;

    PrivilegeType privilegeType;  // 权限类型
    QString databaseName;         // 数据库名
    QString tableName;            // 表名（空表示数据库级权限）
    QString username;             // 用户名
    bool withGrantOption = false; // 是否可以授予他人
};

// REVOKE 语句
// REVOKE privilege_type ON database.table FROM username
class RevokeStatement : public ASTNode {
public:
    QString toString() const override;

    PrivilegeType privilegeType;  // 权限类型
    QString databaseName;         // 数据库名
    QString tableName;            // 表名（空表示数据库级权限）
    QString username;             // 用户名
};

} // namespace ast
} // namespace qindb

#endif // QINDB_AST_H
