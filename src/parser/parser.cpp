#include "qindb/parser.h"
#include "qindb/logger.h"

namespace qindb {

Parser::Parser(const QString& sql)
    : m_lexer(sql)
    , m_error{ErrorCode::SUCCESS, "", ""}
{
    // 预读两个 Token
    m_currentToken = m_lexer.nextToken();
    m_peekToken = m_lexer.nextToken();
}

// ========== 核心辅助函数 ==========

const Token& Parser::current() const {
    return m_currentToken;
}

const Token& Parser::peek() const {
    return m_peekToken;
}

void Parser::advance() {
    m_currentToken = m_peekToken;
    m_peekToken = m_lexer.nextToken();
}

bool Parser::check(TokenType type) const {
    return m_currentToken.type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::consume(TokenType type, const QString& errMsg) {
    if (check(type)) {
        advance();
        return true;
    }
    setError(ErrorCode::SYNTAX_ERROR, errMsg,
             QString("Expected token type, but got '%1'").arg(m_currentToken.lexeme));
    return false;
}

void Parser::setError(ErrorCode code, const QString& msg, const QString& detail) {
    m_error.code = code;
    m_error.message = msg;
    m_error.detail = detail;
    LOG_ERROR(QString("Parser error: %1 - %2").arg(msg, detail));
}

// ========== 主解析入口 ==========

std::unique_ptr<ast::ASTNode> Parser::parse() {
    if (check(TokenType::EOF_TOKEN)) {
        setError(ErrorCode::SYNTAX_ERROR, "Empty SQL statement", "");
        return nullptr;
    }

    auto stmt = parseStatement();

    if (!stmt) {
        return nullptr;
    }

    // 消费可选的分号
    if (check(TokenType::SEMICOLON)) {
        advance();
    }

    // 检查是否还有多余的 Token
    if (!check(TokenType::EOF_TOKEN)) {
        setError(ErrorCode::SYNTAX_ERROR, "Unexpected tokens after statement",
                 QString("Extra token: '%1'").arg(m_currentToken.lexeme));
        return nullptr;
    }

    return stmt;
}

std::unique_ptr<ast::ASTNode> Parser::parseStatement() {
    switch (m_currentToken.type) {
        case TokenType::SELECT:
            return parseSelect();
        case TokenType::INSERT:
            return parseInsert();
        case TokenType::UPDATE:
            return parseUpdate();
        case TokenType::DELETE:
            return parseDelete();
        case TokenType::CREATE:
            // 需要查看下一个 Token 来区分 CREATE TABLE、CREATE INDEX、CREATE DATABASE、CREATE USER
            if (peek().type == TokenType::TABLE) {
                return parseCreateTable();
            } else if (peek().type == TokenType::INDEX || peek().type == TokenType::UNIQUE) {
                return parseCreateIndex();
            } else if (peek().type == TokenType::DATABASE || peek().type == TokenType::DATABASES) {
                return parseCreateDatabase();
            } else if (peek().type == TokenType::USER) {
                return parseCreateUser();
            } else {
                setError(ErrorCode::SYNTAX_ERROR, "Invalid CREATE statement",
                         QString("Expected TABLE, INDEX, DATABASE or USER, got '%1'").arg(peek().lexeme));
                return nullptr;
            }
        case TokenType::DROP:
            if (peek().type == TokenType::TABLE) {
                return parseDropTable();
            } else if (peek().type == TokenType::INDEX) {
                return parseDropIndex();
            } else if (peek().type == TokenType::DATABASE || peek().type == TokenType::DATABASES) {
                return parseDropDatabase();
            } else if (peek().type == TokenType::USER) {
                return parseDropUser();
            } else {
                setError(ErrorCode::SYNTAX_ERROR, "Invalid DROP statement",
                         QString("Expected TABLE, INDEX, DATABASE or USER, got '%1'").arg(peek().lexeme));
                return nullptr;
            }
        case TokenType::ALTER:
            // 区分 ALTER TABLE 和 ALTER USER
            if (peek().type == TokenType::TABLE) {
                return parseAlterTable();
            } else if (peek().type == TokenType::USER) {
                return parseAlterUser();
            } else {
                setError(ErrorCode::SYNTAX_ERROR, "Invalid ALTER statement",
                         QString("Expected TABLE or USER, got '%1'").arg(peek().lexeme));
                return nullptr;
            }
        case TokenType::SHOW:
            if (peek().type == TokenType::TABLES) {
                return parseShowTables();
            } else if (peek().type == TokenType::INDEXES) {
                return parseShowIndexes();
            } else if (peek().type == TokenType::DATABASES) {
                return parseShowDatabases();
            } else {
                setError(ErrorCode::SYNTAX_ERROR, "Invalid SHOW statement",
                         QString("Expected TABLES, INDEXES or DATABASES, got '%1'").arg(peek().lexeme));
                return nullptr;
            }
        case TokenType::BEGIN:
            return parseBeginTransaction();
        case TokenType::COMMIT:
            return parseCommit();
        case TokenType::ROLLBACK:
            return parseRollback();
        case TokenType::USE:
            return parseUseDatabase();
        case TokenType::SAVE:
            return parseSave();
        case TokenType::VACUUM:
            return parseVacuum();
        case TokenType::ANALYZE:
            return parseAnalyze();
        case TokenType::EXPLAIN:
            return parseExplain();
        case TokenType::GRANT:
            return parseGrant();
        case TokenType::REVOKE:
            return parseRevoke();
        default:
            setError(ErrorCode::SYNTAX_ERROR, "Unknown statement type",
                     QString("Unexpected token: '%1'").arg(m_currentToken.lexeme));
            return nullptr;
    }
}

// ========== SELECT 语句解析 ==========

std::unique_ptr<ast::SelectStatement> Parser::parseSelect() {
    auto stmt = std::make_unique<ast::SelectStatement>();

    consume(TokenType::SELECT, "Expected SELECT");

    // DISTINCT
    if (match(TokenType::DISTINCT)) {
        stmt->distinct = true;
    }

    // SELECT 列表
    if (!parseSelectList(stmt.get())) {
        return nullptr;
    }

    // FROM 子句（可选）
    if (match(TokenType::FROM)) {
        stmt->from = parseTableReference();
        if (!stmt->from) {
            return nullptr;
        }

        // JOIN 子句（可选，可多个）
        while (check(TokenType::INNER) || check(TokenType::LEFT) ||
               check(TokenType::RIGHT) || check(TokenType::FULL) ||
               check(TokenType::CROSS) || check(TokenType::JOIN)) {
            auto join = parseJoin();
            if (!join) {
                return nullptr;
            }
            stmt->joins.push_back(std::move(join));
        }
    }

    // WHERE 子句（可选）
    if (match(TokenType::WHERE)) {
        stmt->where = parseExpression();
        if (!stmt->where) {
            return nullptr;
        }
    }

    // GROUP BY 子句（可选）
    if (check(TokenType::GROUP)) {
        stmt->groupBy = parseGroupBy();
        if (!stmt->groupBy) {
            return nullptr;
        }
    }

    // ORDER BY 子句（可选）
    if (check(TokenType::ORDER)) {
        stmt->orderBy = parseOrderBy();
        if (stmt->orderBy.empty() && !m_error.isSuccess()) {
            return nullptr;
        }
    }

    // LIMIT 子句（可选）
    if (match(TokenType::LIMIT)) {
        if (!check(TokenType::INTEGER)) {
            setError(ErrorCode::SYNTAX_ERROR, "LIMIT requires integer value", "");
            return nullptr;
        }
        stmt->limit = m_currentToken.value.toInt();
        advance();
    }

    // OFFSET 子句（可选）
    if (match(TokenType::OFFSET)) {
        if (!check(TokenType::INTEGER)) {
            setError(ErrorCode::SYNTAX_ERROR, "OFFSET requires integer value", "");
            return nullptr;
        }
        stmt->offset = m_currentToken.value.toInt();
        advance();
    }

    // INTO OUTFILE 子句（可选）
    if (match(TokenType::INTO)) {
        if (!consume(TokenType::OUTFILE, "Expected OUTFILE after INTO")) {
            return nullptr;
        }

        // 获取文件路径
        if (!check(TokenType::STRING)) {
            setError(ErrorCode::SYNTAX_ERROR, "OUTFILE requires string path", "");
            return nullptr;
        }
        stmt->exportFilePath = m_currentToken.value.toString();
        advance();

        // FORMAT 子句（可选，默认 CSV）
        if (match(TokenType::FORMAT)) {
            if (!check(TokenType::IDENTIFIER)) {
                setError(ErrorCode::SYNTAX_ERROR, "FORMAT requires format name (JSON/CSV/XML)", "");
                return nullptr;
            }
            stmt->exportFormat = m_currentToken.lexeme.toUpper();
            advance();

            // 验证格式
            if (stmt->exportFormat != "JSON" &&
                stmt->exportFormat != "CSV" &&
                stmt->exportFormat != "XML") {
                setError(ErrorCode::SYNTAX_ERROR,
                        "Invalid export format. Must be JSON, CSV, or XML", "");
                return nullptr;
            }
        } else {
            // 默认格式为 CSV
            stmt->exportFormat = "CSV";
        }
    }

    return stmt;
}

bool Parser::parseSelectList(ast::SelectStatement* stmt) {
    do {
        // 处理 * 通配符
        if (match(TokenType::STAR)) {
            auto expr = std::make_unique<ast::ColumnExpression>("", "*");
            stmt->selectList.push_back(std::move(expr));
            stmt->selectAliases.append("");
            continue;
        }

        // 解析表达式
        auto expr = parseExpression();
        if (!expr) {
            return false;
        }

        // 检查是否有别名
        QString alias;
        if (match(TokenType::AS)) {
            if (!check(TokenType::IDENTIFIER)) {
                setError(ErrorCode::SYNTAX_ERROR, "Expected alias after AS", "");
                return false;
            }
            alias = m_currentToken.lexeme;
            advance();
        } else if (check(TokenType::IDENTIFIER) && !check(TokenType::COMMA) && !check(TokenType::FROM)) {
            // 隐式别名（没有 AS 关键字）
            alias = m_currentToken.lexeme;
            advance();
        }

        stmt->selectList.push_back(std::move(expr));
        stmt->selectAliases.append(alias);

    } while (match(TokenType::COMMA));

    return true;
}

std::unique_ptr<ast::TableReference> Parser::parseTableReference() {
    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected table name", "");
        return nullptr;
    }

    QString tableName = m_currentToken.lexeme;
    advance();

    QString alias;
    if (match(TokenType::AS)) {
        if (!check(TokenType::IDENTIFIER)) {
            setError(ErrorCode::SYNTAX_ERROR, "Expected alias after AS", "");
            return nullptr;
        }
        alias = m_currentToken.lexeme;
        advance();
    } else if (check(TokenType::IDENTIFIER)) {
        // 隐式别名
        alias = m_currentToken.lexeme;
        advance();
    }

    return std::make_unique<ast::TableReference>(tableName, alias);
}

std::unique_ptr<ast::JoinClause> Parser::parseJoin() {
    ast::JoinType joinType = ast::JoinType::INNER;

    // 解析 JOIN 类型
    if (match(TokenType::INNER)) {
        joinType = ast::JoinType::INNER;
        consume(TokenType::JOIN, "Expected JOIN after INNER");
    } else if (match(TokenType::LEFT)) {
        joinType = ast::JoinType::LEFT;
        consume(TokenType::JOIN, "Expected JOIN after LEFT");
    } else if (match(TokenType::RIGHT)) {
        joinType = ast::JoinType::RIGHT;
        consume(TokenType::JOIN, "Expected JOIN after RIGHT");
    } else if (match(TokenType::FULL)) {
        joinType = ast::JoinType::FULL;
        consume(TokenType::JOIN, "Expected JOIN after FULL");
    } else if (match(TokenType::CROSS)) {
        joinType = ast::JoinType::CROSS;
        consume(TokenType::JOIN, "Expected JOIN after CROSS");
    } else if (match(TokenType::JOIN)) {
        joinType = ast::JoinType::INNER;
    } else {
        setError(ErrorCode::SYNTAX_ERROR, "Expected JOIN keyword", "");
        return nullptr;
    }

    // 解析右表
    auto rightTable = parseTableReference();
    if (!rightTable) {
        return nullptr;
    }

    // 解析 ON 条件（CROSS JOIN 不需要）
    std::unique_ptr<ast::Expression> condition;
    if (joinType != ast::JoinType::CROSS) {
        if (!consume(TokenType::ON, "Expected ON after JOIN")) {
            return nullptr;
        }
        condition = parseExpression();
        if (!condition) {
            return nullptr;
        }
    }

    return std::make_unique<ast::JoinClause>(joinType, std::move(rightTable), std::move(condition));
}

std::unique_ptr<ast::GroupByClause> Parser::parseGroupBy() {
    consume(TokenType::GROUP, "Expected GROUP");
    consume(TokenType::BY, "Expected BY after GROUP");

    auto groupBy = std::make_unique<ast::GroupByClause>();

    // 解析分组表达式列表
    do {
        auto expr = parseExpression();
        if (!expr) {
            return nullptr;
        }
        groupBy->expressions.push_back(std::move(expr));
    } while (match(TokenType::COMMA));

    // HAVING 子句（可选）
    if (match(TokenType::HAVING)) {
        groupBy->having = parseExpression();
        if (!groupBy->having) {
            return nullptr;
        }
    }

    return groupBy;
}

std::vector<ast::OrderByItem> Parser::parseOrderBy() {
    consume(TokenType::ORDER, "Expected ORDER");
    consume(TokenType::BY, "Expected BY after ORDER");

    std::vector<ast::OrderByItem> items;

    do {
        ast::OrderByItem item;
        item.expression = parseExpression();
        if (!item.expression) {
            return {};
        }

        // ASC/DESC（可选，默认 ASC）
        if (match(TokenType::ASC)) {
            item.ascending = true;
        } else if (match(TokenType::DESC)) {
            item.ascending = false;
        } else {
            item.ascending = true;
        }

        items.push_back(std::move(item));
    } while (match(TokenType::COMMA));

    return items;
}

// ========== INSERT 语句解析 ==========

std::unique_ptr<ast::InsertStatement> Parser::parseInsert() {
    auto stmt = std::make_unique<ast::InsertStatement>();

    consume(TokenType::INSERT, "Expected INSERT");
    consume(TokenType::INTO, "Expected INTO after INSERT");

    // 表名
    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected table name", "");
        return nullptr;
    }
    stmt->tableName = m_currentToken.lexeme;
    advance();

    // 列名列表（可选）
    if (match(TokenType::LPAREN)) {
        do {
            if (!check(TokenType::IDENTIFIER)) {
                setError(ErrorCode::SYNTAX_ERROR, "Expected column name", "");
                return nullptr;
            }
            stmt->columns.append(m_currentToken.lexeme);
            advance();
        } while (match(TokenType::COMMA));

        if (!consume(TokenType::RPAREN, "Expected ')' after column list")) {
            return nullptr;
        }
    }

    // VALUES 或 SELECT
    if (check(TokenType::SELECT)) {
        stmt->selectQuery = parseSelect();
        if (!stmt->selectQuery) {
            return nullptr;
        }
    } else if (match(TokenType::VALUES)) {
        // 解析值列表
        do {
            if (!consume(TokenType::LPAREN, "Expected '(' before values")) {
                return nullptr;
            }

            std::vector<std::unique_ptr<ast::Expression>> row;
            do {
                auto expr = parseExpression();
                if (!expr) {
                    return nullptr;
                }
                row.push_back(std::move(expr));
            } while (match(TokenType::COMMA));

            if (!consume(TokenType::RPAREN, "Expected ')' after values")) {
                return nullptr;
            }

            stmt->values.push_back(std::move(row));
        } while (match(TokenType::COMMA));
    } else {
        setError(ErrorCode::SYNTAX_ERROR, "Expected VALUES or SELECT after column list", "");
        return nullptr;
    }

    return stmt;
}

// ========== UPDATE 语句解析 ==========

std::unique_ptr<ast::UpdateStatement> Parser::parseUpdate() {
    auto stmt = std::make_unique<ast::UpdateStatement>();

    consume(TokenType::UPDATE, "Expected UPDATE");

    // 表名
    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected table name", "");
        return nullptr;
    }
    stmt->tableName = m_currentToken.lexeme;
    advance();

    // SET 子句
    consume(TokenType::SET, "Expected SET after table name");

    do {
        if (!check(TokenType::IDENTIFIER)) {
            setError(ErrorCode::SYNTAX_ERROR, "Expected column name", "");
            return nullptr;
        }
        QString column = m_currentToken.lexeme;
        advance();

        if (!consume(TokenType::EQ, "Expected '=' after column name")) {
            return nullptr;
        }

        auto expr = parseExpression();
        if (!expr) {
            return nullptr;
        }

        stmt->assignments.push_back({column, std::move(expr)});
    } while (match(TokenType::COMMA));

    // WHERE 子句（可选）
    if (match(TokenType::WHERE)) {
        stmt->where = parseExpression();
        if (!stmt->where) {
            return nullptr;
        }
    }

    return stmt;
}

// ========== DELETE 语句解析 ==========

std::unique_ptr<ast::DeleteStatement> Parser::parseDelete() {
    auto stmt = std::make_unique<ast::DeleteStatement>();

    consume(TokenType::DELETE, "Expected DELETE");
    consume(TokenType::FROM, "Expected FROM after DELETE");

    // 表名
    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected table name", "");
        return nullptr;
    }
    stmt->tableName = m_currentToken.lexeme;
    advance();

    // WHERE 子句（可选）
    if (match(TokenType::WHERE)) {
        stmt->where = parseExpression();
        if (!stmt->where) {
            return nullptr;
        }
    }

    return stmt;
}

// ========== 表达式解析（运算符优先级） ==========

std::unique_ptr<ast::Expression> Parser::parseExpression() {
    return parseOrExpression();
}

std::unique_ptr<ast::Expression> Parser::parseOrExpression() {
    auto left = parseAndExpression();
    if (!left) return nullptr;

    while (match(TokenType::OR)) {
        auto right = parseAndExpression();
        if (!right) return nullptr;

        left = std::make_unique<ast::BinaryExpression>(
            std::move(left), ast::BinaryOp::OR, std::move(right));
    }

    return left;
}

std::unique_ptr<ast::Expression> Parser::parseAndExpression() {
    auto left = parseNotExpression();
    if (!left) return nullptr;

    while (match(TokenType::AND)) {
        auto right = parseNotExpression();
        if (!right) return nullptr;

        left = std::make_unique<ast::BinaryExpression>(
            std::move(left), ast::BinaryOp::AND, std::move(right));
    }

    return left;
}

std::unique_ptr<ast::Expression> Parser::parseNotExpression() {
    if (match(TokenType::NOT)) {
        auto expr = parseNotExpression();
        if (!expr) return nullptr;
        return std::make_unique<ast::UnaryExpression>(ast::UnaryOp::NOT, std::move(expr));
    }

    return parseComparisonExpression();
}

std::unique_ptr<ast::Expression> Parser::parseComparisonExpression() {
    auto left = parseAdditiveExpression();
    if (!left) return nullptr;

    // 比较运算符
    if (match(TokenType::EQ)) {
        auto right = parseAdditiveExpression();
        if (!right) return nullptr;
        return std::make_unique<ast::BinaryExpression>(
            std::move(left), ast::BinaryOp::EQ, std::move(right));
    } else if (match(TokenType::NE)) {
        auto right = parseAdditiveExpression();
        if (!right) return nullptr;
        return std::make_unique<ast::BinaryExpression>(
            std::move(left), ast::BinaryOp::NE, std::move(right));
    } else if (match(TokenType::LT)) {
        auto right = parseAdditiveExpression();
        if (!right) return nullptr;
        return std::make_unique<ast::BinaryExpression>(
            std::move(left), ast::BinaryOp::LT, std::move(right));
    } else if (match(TokenType::LE)) {
        auto right = parseAdditiveExpression();
        if (!right) return nullptr;
        return std::make_unique<ast::BinaryExpression>(
            std::move(left), ast::BinaryOp::LE, std::move(right));
    } else if (match(TokenType::GT)) {
        auto right = parseAdditiveExpression();
        if (!right) return nullptr;
        return std::make_unique<ast::BinaryExpression>(
            std::move(left), ast::BinaryOp::GT, std::move(right));
    } else if (match(TokenType::GE)) {
        auto right = parseAdditiveExpression();
        if (!right) return nullptr;
        return std::make_unique<ast::BinaryExpression>(
            std::move(left), ast::BinaryOp::GE, std::move(right));
    } else if (match(TokenType::LIKE)) {
        auto right = parseAdditiveExpression();
        if (!right) return nullptr;
        return std::make_unique<ast::BinaryExpression>(
            std::move(left), ast::BinaryOp::LIKE, std::move(right));
    } else if (match(TokenType::IN)) {
        auto right = parseAdditiveExpression();
        if (!right) return nullptr;
        return std::make_unique<ast::BinaryExpression>(
            std::move(left), ast::BinaryOp::IN, std::move(right));
    } else if (match(TokenType::IS)) {
        if (match(TokenType::NOT)) {
            consume(TokenType::NULL_KW, "Expected NULL after IS NOT");
            return std::make_unique<ast::UnaryExpression>(ast::UnaryOp::IS_NOT_NULL, std::move(left));
        } else {
            consume(TokenType::NULL_KW, "Expected NULL after IS");
            return std::make_unique<ast::UnaryExpression>(ast::UnaryOp::IS_NULL, std::move(left));
        }
    }

    return left;
}

std::unique_ptr<ast::Expression> Parser::parseAdditiveExpression() {
    auto left = parseMultiplicativeExpression();
    if (!left) return nullptr;

    while (true) {
        if (match(TokenType::PLUS)) {
            auto right = parseMultiplicativeExpression();
            if (!right) return nullptr;
            left = std::make_unique<ast::BinaryExpression>(
                std::move(left), ast::BinaryOp::ADD, std::move(right));
        } else if (match(TokenType::MINUS)) {
            auto right = parseMultiplicativeExpression();
            if (!right) return nullptr;
            left = std::make_unique<ast::BinaryExpression>(
                std::move(left), ast::BinaryOp::SUB, std::move(right));
        } else {
            break;
        }
    }

    return left;
}

std::unique_ptr<ast::Expression> Parser::parseMultiplicativeExpression() {
    auto left = parseUnaryExpression();
    if (!left) return nullptr;

    while (true) {
        if (match(TokenType::STAR)) {
            auto right = parseUnaryExpression();
            if (!right) return nullptr;
            left = std::make_unique<ast::BinaryExpression>(
                std::move(left), ast::BinaryOp::MUL, std::move(right));
        } else if (match(TokenType::SLASH)) {
            auto right = parseUnaryExpression();
            if (!right) return nullptr;
            left = std::make_unique<ast::BinaryExpression>(
                std::move(left), ast::BinaryOp::DIV, std::move(right));
        } else if (match(TokenType::PERCENT)) {
            auto right = parseUnaryExpression();
            if (!right) return nullptr;
            left = std::make_unique<ast::BinaryExpression>(
                std::move(left), ast::BinaryOp::MOD, std::move(right));
        } else {
            break;
        }
    }

    return left;
}

std::unique_ptr<ast::Expression> Parser::parseUnaryExpression() {
    if (match(TokenType::MINUS)) {
        auto expr = parseUnaryExpression();
        if (!expr) return nullptr;
        return std::make_unique<ast::UnaryExpression>(ast::UnaryOp::MINUS, std::move(expr));
    } else if (match(TokenType::PLUS)) {
        auto expr = parseUnaryExpression();
        if (!expr) return nullptr;
        return std::make_unique<ast::UnaryExpression>(ast::UnaryOp::PLUS, std::move(expr));
    }

    return parsePrimaryExpression();
}

std::unique_ptr<ast::Expression> Parser::parsePrimaryExpression() {
    // 字面值
    if (check(TokenType::INTEGER) || check(TokenType::FLOAT) || check(TokenType::STRING)) {
        auto expr = std::make_unique<ast::LiteralExpression>(m_currentToken.value);
        advance();
        return expr;
    }

    // NULL
    if (match(TokenType::NULL_KW)) {
        return std::make_unique<ast::LiteralExpression>(Value());
    }

    // TRUE/FALSE
    if (match(TokenType::TRUE_KW)) {
        return std::make_unique<ast::LiteralExpression>(Value(true));
    }
    if (match(TokenType::FALSE_KW)) {
        return std::make_unique<ast::LiteralExpression>(Value(false));
    }

    // 括号表达式或子查询
    if (match(TokenType::LPAREN)) {
        // 检查是否是子查询
        if (check(TokenType::SELECT)) {
            auto subquery = parseSelect();
            if (!subquery) return nullptr;
            if (!consume(TokenType::RPAREN, "Expected ')' after subquery")) {
                return nullptr;
            }
            return std::make_unique<ast::SubqueryExpression>(std::move(subquery));
        } else {
            // 普通括号表达式
            auto expr = parseExpression();
            if (!expr) return nullptr;
            if (!consume(TokenType::RPAREN, "Expected ')' after expression")) {
                return nullptr;
            }
            return expr;
        }
    }

    // CASE 表达式
    if (check(TokenType::CASE)) {
        return parseCaseExpression();
    }

    // MATCH...AGAINST 表达式
    if (match(TokenType::MATCH)) {
        if (!consume(TokenType::LPAREN, "Expected '(' after MATCH")) {
            return nullptr;
        }

        // 解析列名列表
        QStringList columns;
        do {
            if (!check(TokenType::IDENTIFIER)) {
                setError(ErrorCode::SYNTAX_ERROR, "Expected column name in MATCH", "");
                return nullptr;
            }
            columns.append(m_currentToken.lexeme);
            advance();
        } while (match(TokenType::COMMA));

        if (!consume(TokenType::RPAREN, "Expected ')' after column list")) {
            return nullptr;
        }

        if (!consume(TokenType::AGAINST, "Expected AGAINST after MATCH(...)")) {
            return nullptr;
        }

        if (!consume(TokenType::LPAREN, "Expected '(' after AGAINST")) {
            return nullptr;
        }

        if (!check(TokenType::STRING)) {
            setError(ErrorCode::SYNTAX_ERROR, "Expected search query string in AGAINST", "");
            return nullptr;
        }
        QString query = m_currentToken.value.toString();
        advance();

        // 检查是否有 IN BOOLEAN MODE
        ast::MatchMode mode = ast::MatchMode::NATURAL_LANGUAGE;
        if (match(TokenType::IN)) {
            if (!check(TokenType::IDENTIFIER)) {
                setError(ErrorCode::SYNTAX_ERROR, "Expected BOOLEAN after IN", "");
                return nullptr;
            }
            QString keyword = m_currentToken.lexeme.toUpper();
            if (keyword == "BOOLEAN") {
                advance();
                if (!check(TokenType::IDENTIFIER) || m_currentToken.lexeme.toUpper() != "MODE") {
                    setError(ErrorCode::SYNTAX_ERROR, "Expected MODE after BOOLEAN", "");
                    return nullptr;
                }
                advance();
                mode = ast::MatchMode::BOOLEAN;
            } else {
                setError(ErrorCode::SYNTAX_ERROR, "Expected BOOLEAN after IN", "");
                return nullptr;
            }
        }

        if (!consume(TokenType::RPAREN, "Expected ')' after AGAINST query")) {
            return nullptr;
        }

        return std::make_unique<ast::MatchExpression>(columns, query, mode);
    }

    // 函数调用或列引用
    if (check(TokenType::IDENTIFIER)) {
        QString name = m_currentToken.lexeme;
        advance();

        // 检查是否是聚合函数
        QString upperName = name.toUpper();
        if (upperName == "COUNT" || upperName == "SUM" || upperName == "AVG" ||
            upperName == "MIN" || upperName == "MAX") {
            return parseAggregateFunction(name);
        }

        // 检查是否是函数调用
        if (check(TokenType::LPAREN)) {
            return parseFunctionCall(name);
        }

        // 列引用（可能带表名前缀）
        if (match(TokenType::DOT)) {
            if (!check(TokenType::IDENTIFIER) && !check(TokenType::STAR)) {
                setError(ErrorCode::SYNTAX_ERROR, "Expected column name after '.'", "");
                return nullptr;
            }
            QString column = m_currentToken.lexeme;
            advance();
            return std::make_unique<ast::ColumnExpression>(name, column);
        }

        // 单独的列名
        return std::make_unique<ast::ColumnExpression>("", name);
    }

    setError(ErrorCode::SYNTAX_ERROR, "Unexpected token in expression",
             QString("Got: '%1'").arg(m_currentToken.lexeme));
    return nullptr;
}

std::unique_ptr<ast::Expression> Parser::parseFunctionCall(const QString& name) {
    consume(TokenType::LPAREN, "Expected '(' after function name");

    std::vector<std::unique_ptr<ast::Expression>> args;

    if (!check(TokenType::RPAREN)) {
        do {
            auto arg = parseExpression();
            if (!arg) return nullptr;
            args.push_back(std::move(arg));
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RPAREN, "Expected ')' after function arguments");

    return std::make_unique<ast::FunctionCallExpression>(name, std::move(args));
}

std::unique_ptr<ast::Expression> Parser::parseAggregateFunction(const QString& name) {
    consume(TokenType::LPAREN, "Expected '(' after aggregate function");

    bool distinct = match(TokenType::DISTINCT);

    auto arg = parseExpression();
    if (!arg) return nullptr;

    consume(TokenType::RPAREN, "Expected ')' after aggregate argument");

    ast::AggFunc func;
    QString upperName = name.toUpper();
    if (upperName == "COUNT") func = ast::AggFunc::COUNT;
    else if (upperName == "SUM") func = ast::AggFunc::SUM;
    else if (upperName == "AVG") func = ast::AggFunc::AVG;
    else if (upperName == "MIN") func = ast::AggFunc::MIN;
    else if (upperName == "MAX") func = ast::AggFunc::MAX;
    else {
        setError(ErrorCode::SYNTAX_ERROR, "Unknown aggregate function", name);
        return nullptr;
    }

    return std::make_unique<ast::AggregateExpression>(func, std::move(arg), distinct);
}

std::unique_ptr<ast::Expression> Parser::parseCaseExpression() {
    consume(TokenType::CASE, "Expected CASE");

    std::vector<ast::CaseExpression::WhenClause> whenClauses;

    while (match(TokenType::WHEN)) {
        auto condition = parseExpression();
        if (!condition) return nullptr;

        consume(TokenType::THEN, "Expected THEN after WHEN condition");

        auto result = parseExpression();
        if (!result) return nullptr;

        whenClauses.push_back({std::move(condition), std::move(result)});
    }

    std::unique_ptr<ast::Expression> elseExpr;
    if (match(TokenType::ELSE)) {
        elseExpr = parseExpression();
        if (!elseExpr) return nullptr;
    }

    consume(TokenType::END, "Expected END to close CASE expression");

    return std::make_unique<ast::CaseExpression>(std::move(whenClauses), std::move(elseExpr));
}

// ========== DDL 语句解析 ==========

std::unique_ptr<ast::CreateTableStatement> Parser::parseCreateTable() {
    auto stmt = std::make_unique<ast::CreateTableStatement>();

    consume(TokenType::CREATE, "Expected CREATE");
    consume(TokenType::TABLE, "Expected TABLE");

    if (match(TokenType::IF)) {
        consume(TokenType::NOT, "Expected NOT after IF");
        consume(TokenType::EXISTS, "Expected EXISTS after IF NOT");
        stmt->ifNotExists = true;
    }

    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected table name", "");
        return nullptr;
    }
    stmt->tableName = m_currentToken.lexeme;
    advance();

    consume(TokenType::LPAREN, "Expected '(' after table name");

    // 解析列定义
    do {
        auto colDef = parseColumnDefinition();
        stmt->columns.push_back(std::move(colDef));
    } while (match(TokenType::COMMA));

    consume(TokenType::RPAREN, "Expected ')' after column definitions");

    return stmt;
}

ast::ColumnDefinition Parser::parseColumnDefinition() {
    ast::ColumnDefinition colDef;

    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected column name", "");
        return colDef;
    }
    colDef.name = m_currentToken.lexeme;
    advance();

    // 数据类型
    colDef.type = parseDataType();

    // 约束
    while (true) {
        if (match(TokenType::NOT)) {
            consume(TokenType::NULL_KW, "Expected NULL after NOT");
            colDef.notNull = true;
        } else if (match(TokenType::PRIMARY)) {
            consume(TokenType::KEY, "Expected KEY after PRIMARY");
            colDef.primaryKey = true;
        } else if (match(TokenType::UNIQUE)) {
            colDef.unique = true;
        } else if (match(TokenType::AUTO_INCREMENT)) {
            colDef.autoIncrement = true;
        } else if (match(TokenType::DEFAULT)) {
            colDef.defaultValue = parseExpression();
        } else {
            break;
        }
    }

    return colDef;
}

DataType Parser::parseDataType() {
    DataType type = DataType::INT;

    if (match(TokenType::INT_KW) || match(TokenType::INTEGER)) {
        type = DataType::INT;
    } else if (match(TokenType::BIGINT)) {
        type = DataType::BIGINT;
    } else if (match(TokenType::FLOAT_KW)) {
        type = DataType::FLOAT;
    } else if (match(TokenType::DOUBLE_KW)) {
        type = DataType::DOUBLE;
    } else if (match(TokenType::DECIMAL)) {
        type = DataType::DECIMAL;
    } else if (match(TokenType::CHAR)) {
        type = DataType::CHAR;
    } else if (match(TokenType::VARCHAR)) {
        type = DataType::VARCHAR;
    } else if (match(TokenType::TEXT)) {
        type = DataType::TEXT;
    } else if (match(TokenType::DATE)) {
        type = DataType::DATE;
    } else if (match(TokenType::TIME)) {
        type = DataType::TIME;
    } else if (match(TokenType::DATETIME)) {
        type = DataType::DATETIME;
    } else if (match(TokenType::BOOLEAN)) {
        type = DataType::BOOLEAN;
    } else if (match(TokenType::BLOB)) {
        type = DataType::BLOB;
    } else {
        setError(ErrorCode::SYNTAX_ERROR, "Unknown data type", m_currentToken.lexeme);
    }

    // 处理长度参数（如 VARCHAR(255)）
    if (match(TokenType::LPAREN)) {
        if (check(TokenType::INTEGER)) {
            // length or precision
            advance();
            if (match(TokenType::COMMA)) {
                if (check(TokenType::INTEGER)) {
                    // scale for DECIMAL
                    advance();
                }
            }
        }
        consume(TokenType::RPAREN, "Expected ')' after type parameters");
    }

    return type;
}

std::unique_ptr<ast::DropTableStatement> Parser::parseDropTable() {
    auto stmt = std::make_unique<ast::DropTableStatement>();

    consume(TokenType::DROP, "Expected DROP");
    consume(TokenType::TABLE, "Expected TABLE");

    if (match(TokenType::IF)) {
        consume(TokenType::EXISTS, "Expected EXISTS after IF");
        stmt->ifExists = true;
    }

    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected table name", "");
        return nullptr;
    }
    stmt->tableName = m_currentToken.lexeme;
    advance();

    return stmt;
}

std::unique_ptr<ast::AlterTableStatement> Parser::parseAlterTable() {
    auto stmt = std::make_unique<ast::AlterTableStatement>();

    consume(TokenType::ALTER, "Expected ALTER");
    consume(TokenType::TABLE, "Expected TABLE");

    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected table name", "");
        return nullptr;
    }
    stmt->tableName = m_currentToken.lexeme;
    advance();

    // TODO: 实现 ALTER TABLE 的各种操作
    setError(ErrorCode::NOT_IMPLEMENTED, "ALTER TABLE not fully implemented", "");
    return nullptr;
}

std::unique_ptr<ast::CreateIndexStatement> Parser::parseCreateIndex() {
    auto stmt = std::make_unique<ast::CreateIndexStatement>();

    consume(TokenType::CREATE, "Expected CREATE");

    if (match(TokenType::UNIQUE)) {
        stmt->unique = true;
    }

    consume(TokenType::INDEX, "Expected INDEX");

    if (match(TokenType::IF)) {
        consume(TokenType::NOT, "Expected NOT after IF");
        consume(TokenType::EXISTS, "Expected EXISTS after IF NOT");
        stmt->ifNotExists = true;
    }

    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected index name", "");
        return nullptr;
    }
    stmt->indexName = m_currentToken.lexeme;
    advance();

    consume(TokenType::ON, "Expected ON after index name");

    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected table name", "");
        return nullptr;
    }
    stmt->tableName = m_currentToken.lexeme;
    advance();

    consume(TokenType::LPAREN, "Expected '(' after table name");

    do {
        if (!check(TokenType::IDENTIFIER)) {
            setError(ErrorCode::SYNTAX_ERROR, "Expected column name", "");
            return nullptr;
        }
        stmt->columns.append(m_currentToken.lexeme);
        advance();
    } while (match(TokenType::COMMA));

    consume(TokenType::RPAREN, "Expected ')' after column list");

    // Parse optional USING clause (e.g., USING HASH, USING BTREE)
    if (match(TokenType::USING)) {
        if (check(TokenType::IDENTIFIER)) {
            QString indexTypeStr = m_currentToken.lexeme.toUpper();
            advance();

            if (indexTypeStr == "BTREE") {
                stmt->type = ast::IndexType::BTREE;
            } else if (indexTypeStr == "HASH") {
                stmt->type = ast::IndexType::HASH;
            } else if (indexTypeStr == "FULLTEXT") {
                stmt->type = ast::IndexType::FULLTEXT;
            } else {
                setError(ErrorCode::SYNTAX_ERROR, "Invalid index type",
                         QString("Expected BTREE, HASH, or FULLTEXT, got '%1'").arg(indexTypeStr));
                return nullptr;
            }
        } else {
            setError(ErrorCode::SYNTAX_ERROR, "Expected index type after USING", "");
            return nullptr;
        }
    }
    // Default to BTREE if not specified
    else {
        stmt->type = ast::IndexType::BTREE;
    }

    return stmt;
}

std::unique_ptr<ast::DropIndexStatement> Parser::parseDropIndex() {
    auto stmt = std::make_unique<ast::DropIndexStatement>();

    consume(TokenType::DROP, "Expected DROP");
    consume(TokenType::INDEX, "Expected INDEX");

    if (match(TokenType::IF)) {
        consume(TokenType::EXISTS, "Expected EXISTS after IF");
        stmt->ifExists = true;
    }

    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected index name", "");
        return nullptr;
    }
    stmt->indexName = m_currentToken.lexeme;
    advance();

    consume(TokenType::ON, "Expected ON after index name");

    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected table name", "");
        return nullptr;
    }
    stmt->tableName = m_currentToken.lexeme;
    advance();

    return stmt;
}

std::unique_ptr<ast::ShowTablesStatement> Parser::parseShowTables() {
    auto stmt = std::make_unique<ast::ShowTablesStatement>();

    consume(TokenType::SHOW, "Expected SHOW");
    consume(TokenType::TABLES, "Expected TABLES");

    return stmt;
}

std::unique_ptr<ast::ShowIndexesStatement> Parser::parseShowIndexes() {
    auto stmt = std::make_unique<ast::ShowIndexesStatement>();

    consume(TokenType::SHOW, "Expected SHOW");
    consume(TokenType::INDEXES, "Expected INDEXES");
    consume(TokenType::FROM, "Expected FROM after INDEXES");

    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected table name", "");
        return nullptr;
    }
    stmt->tableName = m_currentToken.lexeme;
    advance();

    return stmt;
}

std::unique_ptr<ast::BeginTransactionStatement> Parser::parseBeginTransaction() {
    consume(TokenType::BEGIN, "Expected BEGIN");
    // 可选的 TRANSACTION 关键字
    match(TokenType::TRANSACTION);
    return std::make_unique<ast::BeginTransactionStatement>();
}

std::unique_ptr<ast::CommitStatement> Parser::parseCommit() {
    consume(TokenType::COMMIT, "Expected COMMIT");
    return std::make_unique<ast::CommitStatement>();
}

std::unique_ptr<ast::RollbackStatement> Parser::parseRollback() {
    consume(TokenType::ROLLBACK, "Expected ROLLBACK");
    return std::make_unique<ast::RollbackStatement>();
}

std::unique_ptr<ast::CreateDatabaseStatement> Parser::parseCreateDatabase() {
    auto stmt = std::make_unique<ast::CreateDatabaseStatement>();

    consume(TokenType::CREATE, "Expected CREATE");
    // DATABASE 或 DATABASES 都可以
    if (!match(TokenType::DATABASE)) {
        consume(TokenType::DATABASES, "Expected DATABASE or DATABASES");
    }

    if (match(TokenType::IF)) {
        consume(TokenType::NOT, "Expected NOT after IF");
        consume(TokenType::EXISTS, "Expected EXISTS after IF NOT");
        stmt->ifNotExists = true;
    }

    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected database name", "");
        return nullptr;
    }
    stmt->databaseName = m_currentToken.lexeme;
    advance();

    return stmt;
}

std::unique_ptr<ast::DropDatabaseStatement> Parser::parseDropDatabase() {
    auto stmt = std::make_unique<ast::DropDatabaseStatement>();

    consume(TokenType::DROP, "Expected DROP");
    // DATABASE 或 DATABASES 都可以
    if (!match(TokenType::DATABASE)) {
        consume(TokenType::DATABASES, "Expected DATABASE or DATABASES");
    }

    if (match(TokenType::IF)) {
        consume(TokenType::EXISTS, "Expected EXISTS after IF");
        stmt->ifExists = true;
    }

    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected database name", "");
        return nullptr;
    }
    stmt->databaseName = m_currentToken.lexeme;
    advance();

    return stmt;
}

std::unique_ptr<ast::UseDatabaseStatement> Parser::parseUseDatabase() {
    auto stmt = std::make_unique<ast::UseDatabaseStatement>();

    consume(TokenType::USE, "Expected USE");

    // DATABASE 或 DATABASES 关键字是可选的
    if (check(TokenType::DATABASE) || check(TokenType::DATABASES)) {
        advance();
    }

    if (!check(TokenType::IDENTIFIER)) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected database name", "");
        return nullptr;
    }
    stmt->databaseName = m_currentToken.lexeme;
    advance();

    return stmt;
}

std::unique_ptr<ast::ShowDatabasesStatement> Parser::parseShowDatabases() {
    auto stmt = std::make_unique<ast::ShowDatabasesStatement>();

    consume(TokenType::SHOW, "Expected SHOW");
    consume(TokenType::DATABASES, "Expected DATABASES");

    return stmt;
}

std::unique_ptr<ast::SaveStatement> Parser::parseSave() {
    auto stmt = std::make_unique<ast::SaveStatement>();

    consume(TokenType::SAVE, "Expected SAVE");

    return stmt;
}

std::unique_ptr<ast::VacuumStatement> Parser::parseVacuum() {
    auto stmt = std::make_unique<ast::VacuumStatement>();

    consume(TokenType::VACUUM, "Expected VACUUM");

    // VACUUM 可以不带表名（清理所有表）
    // VACUUM <table_name>（清理指定表）
    if (m_currentToken.type == TokenType::IDENTIFIER) {
        stmt->tableName = m_currentToken.lexeme;
        advance();
    }

    return stmt;
}

std::unique_ptr<ast::AnalyzeStatement> Parser::parseAnalyze() {
    auto stmt = std::make_unique<ast::AnalyzeStatement>();

    consume(TokenType::ANALYZE, "Expected ANALYZE");

    // ANALYZE [TABLE] <table_name>
    // ANALYZE（分析所有表）
    if (m_currentToken.type == TokenType::TABLE) {
        advance();  // 跳过 TABLE 关键字
    }

    if (m_currentToken.type == TokenType::IDENTIFIER) {
        stmt->tableName = m_currentToken.lexeme;
        advance();
    }

    return stmt;
}

std::unique_ptr<ast::ExplainStatement> Parser::parseExplain() {
    auto stmt = std::make_unique<ast::ExplainStatement>();

    consume(TokenType::EXPLAIN, "Expected EXPLAIN");

    // EXPLAIN SELECT ...
    if (m_currentToken.type == TokenType::SELECT) {
        stmt->query = parseSelect();
    } else {
        setError(ErrorCode::SYNTAX_ERROR, "EXPLAIN requires SELECT statement",
                 "Expected SELECT after EXPLAIN");
        return nullptr;
    }

    return stmt;
}

// ========== 用户管理语句解析 ==========

// CREATE USER username IDENTIFIED BY 'password' [WITH ADMIN]
std::unique_ptr<ast::CreateUserStatement> Parser::parseCreateUser() {
    auto stmt = std::make_unique<ast::CreateUserStatement>();

    consume(TokenType::CREATE, "Expected CREATE");
    consume(TokenType::USER, "Expected USER");

    // 用户名
    if (m_currentToken.type != TokenType::IDENTIFIER) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected username",
                 QString("Got '%1'").arg(m_currentToken.lexeme));
        return nullptr;
    }
    stmt->username = m_currentToken.lexeme;
    advance();

    // IDENTIFIED BY
    if (!consume(TokenType::IDENTIFIED, "Expected IDENTIFIED")) {
        return nullptr;
    }
    if (!consume(TokenType::BY, "Expected BY after IDENTIFIED")) {
        return nullptr;
    }

    // 密码
    if (m_currentToken.type != TokenType::STRING) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected password string",
                 QString("Got '%1'").arg(m_currentToken.lexeme));
        return nullptr;
    }
    stmt->password = m_currentToken.lexeme;
    advance();

    // [WITH ADMIN]
    if (m_currentToken.type == TokenType::WITH) {
        advance();
        if (m_currentToken.type != TokenType::IDENTIFIER) {
            setError(ErrorCode::SYNTAX_ERROR, "Expected ADMIN after WITH",
                     QString("Got '%1'").arg(m_currentToken.lexeme));
            return nullptr;
        }
        if (m_currentToken.lexeme.toLower() == "admin") {
            stmt->isAdmin = true;
        } else {
            setError(ErrorCode::SYNTAX_ERROR, "Invalid WITH clause",
                     QString("Expected ADMIN, got '%1'").arg(m_currentToken.lexeme));
            return nullptr;
        }
        advance();  // 消费 ADMIN 标识符
    }

    return stmt;
}

// DROP USER username
std::unique_ptr<ast::DropUserStatement> Parser::parseDropUser() {
    auto stmt = std::make_unique<ast::DropUserStatement>();

    consume(TokenType::DROP, "Expected DROP");
    consume(TokenType::USER, "Expected USER");

    // 用户名
    if (m_currentToken.type != TokenType::IDENTIFIER) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected username",
                 QString("Got '%1'").arg(m_currentToken.lexeme));
        return nullptr;
    }
    stmt->username = m_currentToken.lexeme;
    advance();

    return stmt;
}

// ALTER USER username IDENTIFIED BY 'new_password'
std::unique_ptr<ast::AlterUserStatement> Parser::parseAlterUser() {
    auto stmt = std::make_unique<ast::AlterUserStatement>();

    consume(TokenType::ALTER, "Expected ALTER");
    consume(TokenType::USER, "Expected USER");

    // 用户名
    if (m_currentToken.type != TokenType::IDENTIFIER) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected username",
                 QString("Got '%1'").arg(m_currentToken.lexeme));
        return nullptr;
    }
    stmt->username = m_currentToken.lexeme;
    advance();

    // IDENTIFIED BY
    if (!consume(TokenType::IDENTIFIED, "Expected IDENTIFIED")) {
        return nullptr;
    }
    if (!consume(TokenType::BY, "Expected BY after IDENTIFIED")) {
        return nullptr;
    }

    // 新密码
    if (m_currentToken.type != TokenType::STRING) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected password string",
                 QString("Got '%1'").arg(m_currentToken.lexeme));
        return nullptr;
    }
    stmt->newPassword = m_currentToken.lexeme;
    advance();

    return stmt;
}

// ========== 权限管理语句解析 ==========

// GRANT privilege_type ON database.table TO username [WITH GRANT OPTION]
std::unique_ptr<ast::GrantStatement> Parser::parseGrant() {
    auto stmt = std::make_unique<ast::GrantStatement>();

    consume(TokenType::GRANT, "Expected GRANT");

    // 解析权限类型 (SELECT, INSERT, UPDATE, DELETE, ALL)
    if (m_currentToken.type == TokenType::SELECT) {
        stmt->privilegeType = ast::PrivilegeType::SELECT;
        advance();
    } else if (m_currentToken.type == TokenType::INSERT) {
        stmt->privilegeType = ast::PrivilegeType::INSERT;
        advance();
    } else if (m_currentToken.type == TokenType::UPDATE) {
        stmt->privilegeType = ast::PrivilegeType::UPDATE;
        advance();
    } else if (m_currentToken.type == TokenType::DELETE) {
        stmt->privilegeType = ast::PrivilegeType::DELETE_PRIV;
        advance();
    } else if (m_currentToken.type == TokenType::ALL) {
        stmt->privilegeType = ast::PrivilegeType::ALL;
        advance();
    } else {
        setError(ErrorCode::SYNTAX_ERROR, "Expected privilege type",
                 QString("Expected SELECT, INSERT, UPDATE, DELETE, or ALL, got '%1'").arg(m_currentToken.lexeme));
        return nullptr;
    }

    // ON
    if (!consume(TokenType::ON, "Expected ON after privilege type")) {
        return nullptr;
    }

    // 解析 database.table 或 database.* 或 *.*
    if (m_currentToken.type == TokenType::IDENTIFIER) {
        stmt->databaseName = m_currentToken.lexeme;
        advance();

        // 检查是否有 .table 或 .*
        if (match(TokenType::DOT)) {
            if (m_currentToken.type == TokenType::STAR) {
                // database.* (数据库级权限)
                stmt->tableName = "";
                advance();
            } else if (m_currentToken.type == TokenType::IDENTIFIER) {
                // database.table (表级权限)
                stmt->tableName = m_currentToken.lexeme;
                advance();
            } else {
                setError(ErrorCode::SYNTAX_ERROR, "Expected table name or * after dot",
                         QString("Got '%1'").arg(m_currentToken.lexeme));
                return nullptr;
            }
        } else {
            // 只有数据库名，默认为数据库级权限
            stmt->tableName = "";
        }
    } else if (m_currentToken.type == TokenType::STAR) {
        // *.* (全局权限，暂不支持)
        advance();
        if (match(TokenType::DOT)) {
            if (!consume(TokenType::STAR, "Expected * after dot")) {
                return nullptr;
            }
            setError(ErrorCode::NOT_IMPLEMENTED, "Global privileges (.) are not yet supported");
            return nullptr;
        }
    } else {
        setError(ErrorCode::SYNTAX_ERROR, "Expected database name or *",
                 QString("Got '%1'").arg(m_currentToken.lexeme));
        return nullptr;
    }

    // TO
    if (!consume(TokenType::TO, "Expected TO")) {
        return nullptr;
    }

    // 用户名
    if (m_currentToken.type != TokenType::IDENTIFIER) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected username",
                 QString("Got '%1'").arg(m_currentToken.lexeme));
        return nullptr;
    }
    stmt->username = m_currentToken.lexeme;
    advance();

    // [WITH GRANT OPTION]
    if (m_currentToken.type == TokenType::WITH) {
        advance();
        if (m_currentToken.type != TokenType::GRANT) {
            setError(ErrorCode::SYNTAX_ERROR, "Expected GRANT after WITH",
                     QString("Got '%1'").arg(m_currentToken.lexeme));
            return nullptr;
        }
        advance();
        if (m_currentToken.type != TokenType::OPTION) {
            setError(ErrorCode::SYNTAX_ERROR, "Expected OPTION after GRANT",
                     QString("Got '%1'").arg(m_currentToken.lexeme));
            return nullptr;
        }
        advance();
        stmt->withGrantOption = true;
    }

    return stmt;
}

// REVOKE privilege_type ON database.table FROM username
std::unique_ptr<ast::RevokeStatement> Parser::parseRevoke() {
    auto stmt = std::make_unique<ast::RevokeStatement>();

    consume(TokenType::REVOKE, "Expected REVOKE");

    // 解析权限类型 (SELECT, INSERT, UPDATE, DELETE, ALL)
    if (m_currentToken.type == TokenType::SELECT) {
        stmt->privilegeType = ast::PrivilegeType::SELECT;
        advance();
    } else if (m_currentToken.type == TokenType::INSERT) {
        stmt->privilegeType = ast::PrivilegeType::INSERT;
        advance();
    } else if (m_currentToken.type == TokenType::UPDATE) {
        stmt->privilegeType = ast::PrivilegeType::UPDATE;
        advance();
    } else if (m_currentToken.type == TokenType::DELETE) {
        stmt->privilegeType = ast::PrivilegeType::DELETE_PRIV;
        advance();
    } else if (m_currentToken.type == TokenType::ALL) {
        stmt->privilegeType = ast::PrivilegeType::ALL;
        advance();
    } else {
        setError(ErrorCode::SYNTAX_ERROR, "Expected privilege type",
                 QString("Expected SELECT, INSERT, UPDATE, DELETE, or ALL, got '%1'").arg(m_currentToken.lexeme));
        return nullptr;
    }

    // ON
    if (!consume(TokenType::ON, "Expected ON after privilege type")) {
        return nullptr;
    }

    // 解析 database.table 或 database.* 或 *.*
    if (m_currentToken.type == TokenType::IDENTIFIER) {
        stmt->databaseName = m_currentToken.lexeme;
        advance();

        // 检查是否有 .table 或 .*
        if (match(TokenType::DOT)) {
            if (m_currentToken.type == TokenType::STAR) {
                // database.* (数据库级权限)
                stmt->tableName = "";
                advance();
            } else if (m_currentToken.type == TokenType::IDENTIFIER) {
                // database.table (表级权限)
                stmt->tableName = m_currentToken.lexeme;
                advance();
            } else {
                setError(ErrorCode::SYNTAX_ERROR, "Expected table name or * after dot",
                         QString("Got '%1'").arg(m_currentToken.lexeme));
                return nullptr;
            }
        } else {
            // 只有数据库名，默认为数据库级权限
            stmt->tableName = "";
        }
    } else if (m_currentToken.type == TokenType::STAR) {
        // *.* (全局权限，暂不支持)
        advance();
        if (match(TokenType::DOT)) {
            if (!consume(TokenType::STAR, "Expected * after dot")) {
                return nullptr;
            }
            setError(ErrorCode::NOT_IMPLEMENTED, "Global privileges (*.*) are not yet supported");
            return nullptr;
        }
    } else {
        setError(ErrorCode::SYNTAX_ERROR, "Expected database name or *",
                 QString("Got '%1'").arg(m_currentToken.lexeme));
        return nullptr;
    }

    // FROM
    if (!consume(TokenType::FROM, "Expected FROM")) {
        return nullptr;
    }

    // 用户名
    if (m_currentToken.type != TokenType::IDENTIFIER) {
        setError(ErrorCode::SYNTAX_ERROR, "Expected username",
                 QString("Got '%1'").arg(m_currentToken.lexeme));
        return nullptr;
    }
    stmt->username = m_currentToken.lexeme;
    advance();

    return stmt;
}

} // namespace qindb
