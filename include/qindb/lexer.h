#ifndef QINDB_LEXER_H
#define QINDB_LEXER_H

#include "common.h"
#include <QChar>

namespace qindb {

// Token 类型
enum class TokenType {
    // 字面值
    INTEGER,
    FLOAT,
    STRING,
    IDENTIFIER,

    // 关键字
    SELECT, FROM, WHERE, INSERT, UPDATE, DELETE,
    CREATE, DROP, ALTER, TABLE, INDEX,
    INTO, VALUES, SET,
    JOIN, INNER, LEFT, RIGHT, FULL, OUTER, CROSS, ON,
    AND, OR, NOT, IS, NULL_KW,
    LIKE, IN, BETWEEN, EXISTS,
    ORDER, BY, ASC, DESC,
    GROUP, HAVING,
    LIMIT, OFFSET,
    AS, DISTINCT, ALL,
    COUNT, SUM, AVG, MIN_KW, MAX_KW,
    CASE, WHEN, THEN, ELSE, END,
    IF, NOT_EXISTS, IF_EXISTS,
    PRIMARY, KEY, FOREIGN, REFERENCES,
    UNIQUE, CHECK, DEFAULT,
    INT_KW, BIGINT, FLOAT_KW, DOUBLE_KW, DECIMAL,
    CHAR, VARCHAR, TEXT,
    DATE, TIME, DATETIME,
    BOOLEAN, BLOB,
    TRUE_KW, FALSE_KW,
    BEGIN, COMMIT, ROLLBACK, TRANSACTION,
    SHOW, TABLES, INDEXES, DATABASE, DATABASES,
    USE, DESCRIBE, EXPLAIN, ANALYZE, SAVE, VACUUM,
    GRANT, REVOKE, TO, WITH, OPTION,
    USER, PASSWORD, IDENTIFIED,
    ADD, MODIFY, RENAME, COLUMN,
    CONSTRAINT, CASCADE,
    UNION, INTERSECT, EXCEPT,
    MATCH, AGAINST,
    AUTO_INCREMENT, NOT_NULL,
    USING,
    OUTFILE, FORMAT,

    // 操作符
    PLUS, MINUS, STAR, SLASH, PERCENT,
    EQ, NE, LT, LE, GT, GE,
    ASSIGN,  // :=

    // 分隔符
    LPAREN, RPAREN,
    COMMA, SEMICOLON, DOT,

    // 特殊
    EOF_TOKEN,
    INVALID
};

// Token 结构
struct Token {
    TokenType type;
    QString lexeme;
    Value value;
    int line;
    int column;

    Token(TokenType t = TokenType::INVALID, const QString& lex = "", int ln = 0, int col = 0)
        : type(t), lexeme(lex), line(ln), column(col) {}
};

// 词法分析器
class Lexer {
public:
    explicit Lexer(const QString& source);

    Token nextToken();
    Token peekToken();
    bool hasMore() const;

    Error getError() const { return m_error; }

private:
    QChar current() const;
    QChar peek() const;
    QChar advance();
    bool match(QChar expected);
    void skipWhitespace();
    void skipComment();

    Token makeToken(TokenType type, const QString& lexeme = "");
    Token scanNumber();
    Token scanString();
    Token scanIdentifier();

    TokenType identifierType(const QString& ident);

    QString m_source;
    int m_position = 0;
    int m_line = 1;
    int m_column = 1;
    Error m_error;
    std::optional<Token> m_peeked;
};

QString tokenTypeToString(TokenType type);

} // namespace qindb

#endif // QINDB_LEXER_H
