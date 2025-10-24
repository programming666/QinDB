#ifndef QINDB_PARSER_H
#define QINDB_PARSER_H

#include "common.h"
#include "lexer.h"
#include "ast.h"
#include <memory>
#include <optional>

namespace qindb {

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
    explicit Parser(const QString& sql);

    /**
     * @brief 解析 SQL 语句，返回 AST 根节点
     * @return 成功返回 AST 节点，失败返回 nullptr
     */
    std::unique_ptr<ast::ASTNode> parse();

    /**
     * @brief 获取最后一次错误信息
     */
    const Error& lastError() const { return m_error; }

private:
    // ========== 核心辅助函数 ==========

    /**
     * @brief 获取当前 Token
     */
    const Token& current() const;

    /**
     * @brief 查看下一个 Token（不消耗）
     */
    const Token& peek() const;

    /**
     * @brief 前进到下一个 Token
     */
    void advance();

    /**
     * @brief 检查当前 Token 是否为指定类型
     */
    bool check(TokenType type) const;

    /**
     * @brief 如果当前 Token 匹配，则前进
     * @return 是否匹配成功
     */
    bool match(TokenType type);

    /**
     * @brief 消耗指定类型的 Token，否则报错
     * @return 是否成功
     */
    bool consume(TokenType type, const QString& errMsg);

    /**
     * @brief 设置错误信息
     */
    void setError(ErrorCode code, const QString& msg, const QString& detail = "");

    // ========== 语句解析 ==========

    std::unique_ptr<ast::ASTNode> parseStatement();
    std::unique_ptr<ast::SelectStatement> parseSelect();
    std::unique_ptr<ast::InsertStatement> parseInsert();
    std::unique_ptr<ast::UpdateStatement> parseUpdate();
    std::unique_ptr<ast::DeleteStatement> parseDelete();
    std::unique_ptr<ast::CreateTableStatement> parseCreateTable();
    std::unique_ptr<ast::DropTableStatement> parseDropTable();
    std::unique_ptr<ast::AlterTableStatement> parseAlterTable();
    std::unique_ptr<ast::CreateIndexStatement> parseCreateIndex();
    std::unique_ptr<ast::DropIndexStatement> parseDropIndex();
    std::unique_ptr<ast::ShowTablesStatement> parseShowTables();
    std::unique_ptr<ast::ShowIndexesStatement> parseShowIndexes();
    std::unique_ptr<ast::BeginTransactionStatement> parseBeginTransaction();
    std::unique_ptr<ast::CommitStatement> parseCommit();
    std::unique_ptr<ast::RollbackStatement> parseRollback();
    std::unique_ptr<ast::CreateDatabaseStatement> parseCreateDatabase();
    std::unique_ptr<ast::DropDatabaseStatement> parseDropDatabase();
    std::unique_ptr<ast::UseDatabaseStatement> parseUseDatabase();
    std::unique_ptr<ast::ShowDatabasesStatement> parseShowDatabases();
    std::unique_ptr<ast::SaveStatement> parseSave();
    std::unique_ptr<ast::VacuumStatement> parseVacuum();
    std::unique_ptr<ast::AnalyzeStatement> parseAnalyze();
    std::unique_ptr<ast::ExplainStatement> parseExplain();
    std::unique_ptr<ast::CreateUserStatement> parseCreateUser();
    std::unique_ptr<ast::DropUserStatement> parseDropUser();
    std::unique_ptr<ast::AlterUserStatement> parseAlterUser();
    std::unique_ptr<ast::GrantStatement> parseGrant();
    std::unique_ptr<ast::RevokeStatement> parseRevoke();

    // ========== SELECT 子句解析 ==========

    /**
     * @brief 解析 SELECT 列表
     */
    bool parseSelectList(ast::SelectStatement* stmt);

    /**
     * @brief 解析 FROM 子句
     */
    std::unique_ptr<ast::TableReference> parseTableReference();

    /**
     * @brief 解析 JOIN 子句
     */
    std::unique_ptr<ast::JoinClause> parseJoin();

    /**
     * @brief 解析 WHERE 子句
     */
    std::unique_ptr<ast::Expression> parseWhereClause();

    /**
     * @brief 解析 GROUP BY 子句
     */
    std::unique_ptr<ast::GroupByClause> parseGroupBy();

    /**
     * @brief 解析 ORDER BY 子句
     */
    std::vector<ast::OrderByItem> parseOrderBy();

    // ========== 表达式解析 (运算符优先级) ==========

    std::unique_ptr<ast::Expression> parseExpression();
    std::unique_ptr<ast::Expression> parseOrExpression();
    std::unique_ptr<ast::Expression> parseAndExpression();
    std::unique_ptr<ast::Expression> parseNotExpression();
    std::unique_ptr<ast::Expression> parseComparisonExpression();
    std::unique_ptr<ast::Expression> parseAdditiveExpression();
    std::unique_ptr<ast::Expression> parseMultiplicativeExpression();
    std::unique_ptr<ast::Expression> parseUnaryExpression();
    std::unique_ptr<ast::Expression> parsePrimaryExpression();

    // ========== 特殊表达式 ==========

    std::unique_ptr<ast::Expression> parseFunctionCall(const QString& name);
    std::unique_ptr<ast::Expression> parseAggregateFunction(const QString& name);
    std::unique_ptr<ast::Expression> parseCaseExpression();
    std::unique_ptr<ast::Expression> parseSubquery();

    // ========== DDL 辅助函数 ==========

    ast::ColumnDefinition parseColumnDefinition();
    ast::IndexDefinition parseIndexDefinition();
    DataType parseDataType();

    // ========== 成员变量 ==========

    Lexer m_lexer;              // 词法分析器
    Token m_currentToken;       // 当前 Token
    Token m_peekToken;          // 预读 Token
    Error m_error;              // 错误信息
};

} // namespace qindb

#endif // QINDB_PARSER_H
