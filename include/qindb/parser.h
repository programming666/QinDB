#ifndef QINDB_PARSER_H  // 防止重复包含
#define QINDB_PARSER_H

#include "common.h"    // 包含公共定义和类型
#include "lexer.h"     // 包含词法分析器相关定义
#include "ast.h"       // 包含抽象语法树相关定义
#include <memory>      // 智能指针相关头文件
#include <optional>    // 可选值相关头文件

namespace qindb {    // 定义命名空间 qindb

/**
 * @brief SQL 语法分析器 (递归下降解析器)
 *
 * 将词法分析器产生的 Token 流转换为 AST (抽象语法树)
 * 支持完整的 SQL-92 语法，包括：
 * - DML: SELECT, INSERT, UPDATE, DELETE
 * - DDL: CREATE TABLE, DROP TABLE, ALTER TABLE, CREATE INDEX, DROP INDEX
 * - 事务: BEGIN, COMMIT, ROLLBACK
 * - 扩展: SHOW TABLES, SHOW INDEXES
 */
class Parser {
public:
    explicit Parser(const QString& sql);  // 构造函数，接收 SQL 字符串

    /**
     * @brief 解析 SQL 语句，返回 AST 根节点
     * @return 成功返回 AST 节点，失败返回 nullptr
     */
    std::unique_ptr<ast::ASTNode> parse();  // 主解析函数

    /**
     * @brief 获取最后一次错误信息
     */
    const Error& lastError() const { return m_error; }  // 获取最后一次错误信息

private:
    // ========== 核心辅助函数 ==========

    /**
     * @brief 获取当前 Token
     */
    const Token& current() const;  // 获取当前 Token 的引用

    /**
     * @brief 查看下一个 Token（不消耗）
     */
    const Token& peek() const;  // 查看下一个 Token 但不移动指针

    /**
     * @brief 前进到下一个 Token
     */
    void advance();  // 移动到下一个 Token

    /**
     * @brief 检查当前 Token 是否为指定类型
     */
    bool check(TokenType type) const;  // 检查当前 Token 类型

    /**
     * @brief 如果当前 Token 匹配，则前进
     * @return 是否匹配成功
     */
    bool match(TokenType type);  // 尝试匹配 Token 类型

    /**
     * @brief 消耗指定类型的 Token，否则报错
     * @return 是否成功
     */
    bool consume(TokenType type, const QString& errMsg);  // 消耗指定类型 Token

    /**
     * @brief 设置错误信息
     */
    void setError(ErrorCode code, const QString& msg, const QString& detail = "");  // 设置错误信息

    // ========== 语句解析 ==========

    std::unique_ptr<ast::ASTNode> parseStatement();  // 解析语句
    std::unique_ptr<ast::SelectStatement> parseSelect();  // 解析 SELECT 语句
    std::unique_ptr<ast::InsertStatement> parseInsert();  // 解析 INSERT 语句
    std::unique_ptr<ast::UpdateStatement> parseUpdate();  // 解析 UPDATE 语句
    std::unique_ptr<ast::DeleteStatement> parseDelete();  // 解析 DELETE 语句
    std::unique_ptr<ast::CreateTableStatement> parseCreateTable();  // 解析 CREATE TABLE 语句
    std::unique_ptr<ast::DropTableStatement> parseDropTable();  // 解析 DROP TABLE 语句
    std::unique_ptr<ast::AlterTableStatement> parseAlterTable();  // 解析 ALTER TABLE 语句
    std::unique_ptr<ast::CreateIndexStatement> parseCreateIndex();
    std::unique_ptr<ast::DropIndexStatement> parseDropIndex();  // 解析 DROP INDEX 语句
    std::unique_ptr<ast::ShowTablesStatement> parseShowTables();  // 解析 SHOW TABLES 语句
    std::unique_ptr<ast::ShowIndexesStatement> parseShowIndexes();  // 解析 SHOW INDEXES 语句
    std::unique_ptr<ast::BeginTransactionStatement> parseBeginTransaction();  // 解析 BEGIN TRANSACTION 语句
    std::unique_ptr<ast::CommitStatement> parseCommit();  // 解析 COMMIT 语句
    std::unique_ptr<ast::RollbackStatement> parseRollback();  // 解析 ROLLBACK 语句
    std::unique_ptr<ast::CreateDatabaseStatement> parseCreateDatabase();  // 解析 CREATE DATABASE 语句
    std::unique_ptr<ast::DropDatabaseStatement> parseDropDatabase();  // 解析 DROP DATABASE 语句
    std::unique_ptr<ast::UseDatabaseStatement> parseUseDatabase();  // 解析 USE DATABASE 语句
    std::unique_ptr<ast::ShowDatabasesStatement> parseShowDatabases();  // 解析 SHOW DATABASES 语句
    std::unique_ptr<ast::SaveStatement> parseSave();  // 解析 SAVE 语句
    std::unique_ptr<ast::VacuumStatement> parseVacuum();  // 解析 VACUUM 语句
    std::unique_ptr<ast::AnalyzeStatement> parseAnalyze();  // 解析 ANALYZE 语句
    std::unique_ptr<ast::ExplainStatement> parseExplain();  // 解析 EXPLAIN 语句
    std::unique_ptr<ast::CreateUserStatement> parseCreateUser();  // 解析 CREATE USER 语句
    std::unique_ptr<ast::DropUserStatement> parseDropUser();  // 解析 DROP USER 语句
    std::unique_ptr<ast::AlterUserStatement> parseAlterUser();  // 解析 ALTER USER 语句
    std::unique_ptr<ast::GrantStatement> parseGrant();
    std::unique_ptr<ast::RevokeStatement> parseRevoke();

    // ========== SELECT 子句解析 ==========

    /**
     * @brief 解析 SELECT 列表
     */
    bool parseSelectList(ast::SelectStatement* stmt);  // 解析 SELECT 列表

    /**
     * @brief 解析 FROM 子句
     */
    std::unique_ptr<ast::TableReference> parseTableReference();

    /**
     * @brief 解析 JOIN 子句
     */
    std::unique_ptr<ast::JoinClause> parseJoin();  // 解析 JOIN 子句

    /**
     * @brief 解析 WHERE 子句
     */
    std::unique_ptr<ast::Expression> parseWhereClause();  // 解析 WHERE 子句

    /**
     * @brief 解析 GROUP BY 子句
     */
    std::unique_ptr<ast::GroupByClause> parseGroupBy();  // 解析 GROUP BY 子句

    /**
     * @brief 解析 ORDER BY 子句
     */
    std::vector<ast::OrderByItem> parseOrderBy();  // 解析 ORDER BY 子句

    // ========== 表达式解析 (运算符优先级) ==========

    std::unique_ptr<ast::Expression> parseExpression();  // 解析表达式
    std::unique_ptr<ast::Expression> parseOrExpression();
    std::unique_ptr<ast::Expression> parseAndExpression();  // 解析 AND 表达式
    std::unique_ptr<ast::Expression> parseNotExpression();  // 解析 NOT 表达式
    std::unique_ptr<ast::Expression> parseComparisonExpression();  // 解析比较表达式
    std::unique_ptr<ast::Expression> parseAdditiveExpression();  // 解析加法表达式
    std::unique_ptr<ast::Expression> parseMultiplicativeExpression();  // 解析乘法表达式
    std::unique_ptr<ast::Expression> parseUnaryExpression();  // 解析一元表达式
    std::unique_ptr<ast::Expression> parsePrimaryExpression();

    // ========== 特殊表达式 ==========

    std::unique_ptr<ast::Expression> parseFunctionCall(const QString& name);  // 解析函数调用
    std::unique_ptr<ast::Expression> parseAggregateFunction(const QString& name);  // 解析聚合函数
    std::unique_ptr<ast::Expression> parseCaseExpression();  // 解析 CASE 表达式
    std::unique_ptr<ast::Expression> parseSubquery();  // 解析子查询

    // ========== DDL 辅助函数 ==========

    ast::ColumnDefinition parseColumnDefinition();  // 解析列定义
    ast::IndexDefinition parseIndexDefinition();  // 解析数据类型
    DataType parseDataType();

    // ========== 成员变量 ==========

    Lexer m_lexer;              // 词法分析器
    Token m_currentToken;       // 当前 Token
    Token m_peekToken;          // 预读 Token
    Error m_error;              // 错误信息
};

} // namespace qindb

#endif // QINDB_PARSER_H  // 结束条件编译
