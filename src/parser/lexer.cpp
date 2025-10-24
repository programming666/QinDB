#include "qindb/lexer.h"
#include <QHash>

namespace qindb {

// 关键字映射表
static QHash<QString, TokenType> createKeywordMap() {
    QHash<QString, TokenType> map;
    map["select"] = TokenType::SELECT;
    map["from"] = TokenType::FROM;
    map["where"] = TokenType::WHERE;
    map["insert"] = TokenType::INSERT;
    map["update"] = TokenType::UPDATE;
    map["delete"] = TokenType::DELETE;
    map["create"] = TokenType::CREATE;
    map["drop"] = TokenType::DROP;
    map["alter"] = TokenType::ALTER;
    map["table"] = TokenType::TABLE;
    map["index"] = TokenType::INDEX;
    map["into"] = TokenType::INTO;
    map["values"] = TokenType::VALUES;
    map["set"] = TokenType::SET;
    map["join"] = TokenType::JOIN;
    map["inner"] = TokenType::INNER;
    map["left"] = TokenType::LEFT;
    map["right"] = TokenType::RIGHT;
    map["full"] = TokenType::FULL;
    map["outer"] = TokenType::OUTER;
    map["cross"] = TokenType::CROSS;
    map["on"] = TokenType::ON;
    map["and"] = TokenType::AND;
    map["or"] = TokenType::OR;
    map["not"] = TokenType::NOT;
    map["is"] = TokenType::IS;
    map["null"] = TokenType::NULL_KW;
    map["like"] = TokenType::LIKE;
    map["in"] = TokenType::IN;
    map["between"] = TokenType::BETWEEN;
    map["exists"] = TokenType::EXISTS;
    map["order"] = TokenType::ORDER;
    map["by"] = TokenType::BY;
    map["asc"] = TokenType::ASC;
    map["desc"] = TokenType::DESC;
    map["group"] = TokenType::GROUP;
    map["having"] = TokenType::HAVING;
    map["limit"] = TokenType::LIMIT;
    map["offset"] = TokenType::OFFSET;
    map["as"] = TokenType::AS;
    map["distinct"] = TokenType::DISTINCT;
    map["all"] = TokenType::ALL;
    map["count"] = TokenType::COUNT;
    map["sum"] = TokenType::SUM;
    map["avg"] = TokenType::AVG;
    map["min"] = TokenType::MIN_KW;
    map["max"] = TokenType::MAX_KW;
    map["case"] = TokenType::CASE;
    map["when"] = TokenType::WHEN;
    map["then"] = TokenType::THEN;
    map["else"] = TokenType::ELSE;
    map["end"] = TokenType::END;
    map["if"] = TokenType::IF;
    map["exists"] = TokenType::EXISTS;
    map["primary"] = TokenType::PRIMARY;
    map["key"] = TokenType::KEY;
    map["foreign"] = TokenType::FOREIGN;
    map["references"] = TokenType::REFERENCES;
    map["unique"] = TokenType::UNIQUE;
    map["using"] = TokenType::USING;
    map["check"] = TokenType::CHECK;
    map["default"] = TokenType::DEFAULT;
    map["int"] = TokenType::INT_KW;
    map["bigint"] = TokenType::BIGINT;
    map["float"] = TokenType::FLOAT_KW;
    map["double"] = TokenType::DOUBLE_KW;
    map["decimal"] = TokenType::DECIMAL;
    map["char"] = TokenType::CHAR;
    map["varchar"] = TokenType::VARCHAR;
    map["text"] = TokenType::TEXT;
    map["date"] = TokenType::DATE;
    map["time"] = TokenType::TIME;
    map["datetime"] = TokenType::DATETIME;
    map["boolean"] = TokenType::BOOLEAN;
    map["bool"] = TokenType::BOOLEAN;
    map["blob"] = TokenType::BLOB;
    map["true"] = TokenType::TRUE_KW;
    map["false"] = TokenType::FALSE_KW;
    map["begin"] = TokenType::BEGIN;
    map["commit"] = TokenType::COMMIT;
    map["rollback"] = TokenType::ROLLBACK;
    map["transaction"] = TokenType::TRANSACTION;
    map["show"] = TokenType::SHOW;
    map["tables"] = TokenType::TABLES;
    map["indexes"] = TokenType::INDEXES;
    map["database"] = TokenType::DATABASE;
    map["databases"] = TokenType::DATABASES;
    map["use"] = TokenType::USE;
    map["describe"] = TokenType::DESCRIBE;
    map["explain"] = TokenType::EXPLAIN;
    map["analyze"] = TokenType::ANALYZE;
    map["save"] = TokenType::SAVE;
    map["vacuum"] = TokenType::VACUUM;
    map["grant"] = TokenType::GRANT;
    map["revoke"] = TokenType::REVOKE;
    map["to"] = TokenType::TO;
    map["with"] = TokenType::WITH;
    map["option"] = TokenType::OPTION;
    map["user"] = TokenType::USER;
    map["password"] = TokenType::PASSWORD;
    map["identified"] = TokenType::IDENTIFIED;
    map["add"] = TokenType::ADD;
    map["modify"] = TokenType::MODIFY;
    map["rename"] = TokenType::RENAME;
    map["column"] = TokenType::COLUMN;
    map["constraint"] = TokenType::CONSTRAINT;
    map["cascade"] = TokenType::CASCADE;
    map["union"] = TokenType::UNION;
    map["intersect"] = TokenType::INTERSECT;
    map["except"] = TokenType::EXCEPT;
    map["match"] = TokenType::MATCH;
    map["against"] = TokenType::AGAINST;
    map["auto_increment"] = TokenType::AUTO_INCREMENT;
    return map;
}

static const QHash<QString, TokenType>& keywordMap() {
    static QHash<QString, TokenType> map = createKeywordMap();
    return map;
}

// Lexer implementation
Lexer::Lexer(const QString& source) : m_source(source) {}

QChar Lexer::current() const {
    if (m_position >= m_source.length()) {
        return QChar('\0');
    }
    return m_source[m_position];
}

QChar Lexer::peek() const {
    if (m_position + 1 >= m_source.length()) {
        return QChar('\0');
    }
    return m_source[m_position + 1];
}

QChar Lexer::advance() {
    if (m_position >= m_source.length()) {
        return QChar('\0');
    }
    QChar ch = m_source[m_position++];
    if (ch == '\n') {
        m_line++;
        m_column = 1;
    } else {
        m_column++;
    }
    return ch;
}

bool Lexer::match(QChar expected) {
    if (current() == expected) {
        advance();
        return true;
    }
    return false;
}

void Lexer::skipWhitespace() {
    while (current().isSpace()) {
        advance();
    }
}

void Lexer::skipComment() {
    if (current() == '-' && peek() == '-') {
        // 单行注释 --
        while (current() != '\n' && current() != '\0') {
            advance();
        }
    } else if (current() == '/' && peek() == '*') {
        // 多行注释 /* */
        advance(); // /
        advance(); // *
        while (!(current() == '*' && peek() == '/') && current() != '\0') {
            advance();
        }
        if (current() == '*') {
            advance(); // *
            advance(); // /
        }
    }
}

Token Lexer::makeToken(TokenType type, const QString& lexeme) {
    Token token(type, lexeme, m_line, m_column);
    return token;
}

Token Lexer::scanNumber() {
    int startPos = m_position;
    int startCol = m_column;

    while (current().isDigit()) {
        advance();
    }

    // 检查是否是浮点数
    if (current() == '.' && peek().isDigit()) {
        advance(); // .
        while (current().isDigit()) {
            advance();
        }

        // 科学计数法
        if (current() == 'e' || current() == 'E') {
            advance();
            if (current() == '+' || current() == '-') {
                advance();
            }
            while (current().isDigit()) {
                advance();
            }
        }

        QString lexeme = m_source.mid(startPos, m_position - startPos);
        Token token = makeToken(TokenType::FLOAT, lexeme);
        token.column = startCol;
        token.value = lexeme.toDouble();
        return token;
    }

    QString lexeme = m_source.mid(startPos, m_position - startPos);
    Token token = makeToken(TokenType::INTEGER, lexeme);
    token.column = startCol;
    token.value = lexeme.toLongLong();
    return token;
}

Token Lexer::scanString() {
    int startCol = m_column;
    QChar quote = advance(); // ' 或 "
    QString str;

    while (current() != quote && current() != '\0') {
        if (current() == '\\') {
            advance();
            QChar escaped = advance();
            switch (escaped.toLatin1()) {
                case 'n': str += '\n'; break;
                case 't': str += '\t'; break;
                case 'r': str += '\r'; break;
                case '\\': str += '\\'; break;
                case '\'': str += '\''; break;
                case '\"': str += '\"'; break;
                default: str += escaped; break;
            }
        } else {
            str += advance();
        }
    }

    if (current() == '\0') {
        m_error = Error(ErrorCode::SYNTAX_ERROR, "Unterminated string literal");
        return makeToken(TokenType::INVALID, str);
    }

    advance(); // 结束的引号

    Token token = makeToken(TokenType::STRING, str);
    token.column = startCol;
    token.value = str;
    return token;
}

Token Lexer::scanIdentifier() {
    int startPos = m_position;
    int startCol = m_column;

    while (current().isLetterOrNumber() || current() == '_') {
        advance();
    }

    QString lexeme = m_source.mid(startPos, m_position - startPos);
    TokenType type = identifierType(lexeme);

    Token token = makeToken(type, lexeme);
    token.column = startCol;
    return token;
}

TokenType Lexer::identifierType(const QString& ident) {
    QString lower = ident.toLower();
    if (keywordMap().contains(lower)) {
        return keywordMap()[lower];
    }
    return TokenType::IDENTIFIER;
}

Token Lexer::nextToken() {
    if (m_peeked.has_value()) {
        Token token = m_peeked.value();
        m_peeked.reset();
        return token;
    }

    skipWhitespace();

    while ((current() == '-' && peek() == '-') || (current() == '/' && peek() == '*')) {
        skipComment();
        skipWhitespace();
    }

    if (current() == '\0') {
        return makeToken(TokenType::EOF_TOKEN, "");
    }

    QChar ch = current();

    // 数字
    if (ch.isDigit()) {
        return scanNumber();
    }

    // 字符串
    if (ch == '\'' || ch == '\"') {
        return scanString();
    }

    // 标识符或关键字
    if (ch.isLetter() || ch == '_') {
        return scanIdentifier();
    }

    // 单字符 token
    advance();
    switch (ch.toLatin1()) {
        case '+': return makeToken(TokenType::PLUS, "+");
        case '-': return makeToken(TokenType::MINUS, "-");
        case '*': return makeToken(TokenType::STAR, "*");
        case '/': return makeToken(TokenType::SLASH, "/");
        case '%': return makeToken(TokenType::PERCENT, "%");
        case '(': return makeToken(TokenType::LPAREN, "(");
        case ')': return makeToken(TokenType::RPAREN, ")");
        case ',': return makeToken(TokenType::COMMA, ",");
        case ';': return makeToken(TokenType::SEMICOLON, ";");
        case '.': return makeToken(TokenType::DOT, ".");
    }

    // 双字符 token
    switch (ch.toLatin1()) {
        case '=':
            return makeToken(TokenType::EQ, "=");
        case '<':
            if (match('=')) return makeToken(TokenType::LE, "<=");
            if (match('>')) return makeToken(TokenType::NE, "<>");
            return makeToken(TokenType::LT, "<");
        case '>':
            if (match('=')) return makeToken(TokenType::GE, ">=");
            return makeToken(TokenType::GT, ">");
        case '!':
            if (match('=')) return makeToken(TokenType::NE, "!=");
            break;
        case ':':
            if (match('=')) return makeToken(TokenType::ASSIGN, ":=");
            break;
    }

    m_error = Error(ErrorCode::SYNTAX_ERROR, QString("Unexpected character: ") + ch);
    return makeToken(TokenType::INVALID, QString(ch));
}

Token Lexer::peekToken() {
    if (!m_peeked.has_value()) {
        m_peeked = nextToken();
    }
    return m_peeked.value();
}

bool Lexer::hasMore() const {
    return m_position < m_source.length();
}

QString tokenTypeToString(TokenType type) {
    switch (type) {
        // 字面值
        case TokenType::INTEGER: return "INTEGER";
        case TokenType::FLOAT: return "FLOAT";
        case TokenType::STRING: return "STRING";
        case TokenType::IDENTIFIER: return "IDENTIFIER";

        // DML关键字
        case TokenType::SELECT: return "SELECT";
        case TokenType::FROM: return "FROM";
        case TokenType::WHERE: return "WHERE";
        case TokenType::INSERT: return "INSERT";
        case TokenType::UPDATE: return "UPDATE";
        case TokenType::DELETE: return "DELETE";
        case TokenType::INTO: return "INTO";
        case TokenType::VALUES: return "VALUES";
        case TokenType::SET: return "SET";

        // DDL关键字
        case TokenType::CREATE: return "CREATE";
        case TokenType::DROP: return "DROP";
        case TokenType::ALTER: return "ALTER";
        case TokenType::TABLE: return "TABLE";
        case TokenType::INDEX: return "INDEX";
        case TokenType::ADD: return "ADD";
        case TokenType::MODIFY: return "MODIFY";
        case TokenType::RENAME: return "RENAME";
        case TokenType::COLUMN: return "COLUMN";

        // JOIN关键字
        case TokenType::JOIN: return "JOIN";
        case TokenType::INNER: return "INNER";
        case TokenType::LEFT: return "LEFT";
        case TokenType::RIGHT: return "RIGHT";
        case TokenType::FULL: return "FULL";
        case TokenType::OUTER: return "OUTER";
        case TokenType::CROSS: return "CROSS";
        case TokenType::ON: return "ON";

        // 逻辑运算符
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::NOT: return "NOT";
        case TokenType::IS: return "IS";
        case TokenType::NULL_KW: return "NULL";
        case TokenType::LIKE: return "LIKE";
        case TokenType::IN: return "IN";
        case TokenType::BETWEEN: return "BETWEEN";
        case TokenType::EXISTS: return "EXISTS";

        // 排序/分组
        case TokenType::ORDER: return "ORDER";
        case TokenType::BY: return "BY";
        case TokenType::ASC: return "ASC";
        case TokenType::DESC: return "DESC";
        case TokenType::GROUP: return "GROUP";
        case TokenType::HAVING: return "HAVING";
        case TokenType::LIMIT: return "LIMIT";
        case TokenType::OFFSET: return "OFFSET";

        // 其他关键字
        case TokenType::AS: return "AS";
        case TokenType::DISTINCT: return "DISTINCT";
        case TokenType::ALL: return "ALL";

        // 聚合函数
        case TokenType::COUNT: return "COUNT";
        case TokenType::SUM: return "SUM";
        case TokenType::AVG: return "AVG";
        case TokenType::MIN_KW: return "MIN";
        case TokenType::MAX_KW: return "MAX";

        // CASE表达式
        case TokenType::CASE: return "CASE";
        case TokenType::WHEN: return "WHEN";
        case TokenType::THEN: return "THEN";
        case TokenType::ELSE: return "ELSE";
        case TokenType::END: return "END";

        // 约束
        case TokenType::IF: return "IF";
        case TokenType::NOT_EXISTS: return "NOT EXISTS";
        case TokenType::IF_EXISTS: return "IF EXISTS";
        case TokenType::PRIMARY: return "PRIMARY";
        case TokenType::KEY: return "KEY";
        case TokenType::FOREIGN: return "FOREIGN";
        case TokenType::REFERENCES: return "REFERENCES";
        case TokenType::UNIQUE: return "UNIQUE";
        case TokenType::CHECK: return "CHECK";
        case TokenType::DEFAULT: return "DEFAULT";
        case TokenType::CONSTRAINT: return "CONSTRAINT";
        case TokenType::CASCADE: return "CASCADE";
        case TokenType::AUTO_INCREMENT: return "AUTO_INCREMENT";
        case TokenType::NOT_NULL: return "NOT NULL";

        // 数据类型
        case TokenType::INT_KW: return "INT";
        case TokenType::BIGINT: return "BIGINT";
        case TokenType::FLOAT_KW: return "FLOAT";
        case TokenType::DOUBLE_KW: return "DOUBLE";
        case TokenType::DECIMAL: return "DECIMAL";
        case TokenType::CHAR: return "CHAR";
        case TokenType::VARCHAR: return "VARCHAR";
        case TokenType::TEXT: return "TEXT";
        case TokenType::DATE: return "DATE";
        case TokenType::TIME: return "TIME";
        case TokenType::DATETIME: return "DATETIME";
        case TokenType::BOOLEAN: return "BOOLEAN";
        case TokenType::BLOB: return "BLOB";

        // 布尔值
        case TokenType::TRUE_KW: return "TRUE";
        case TokenType::FALSE_KW: return "FALSE";

        // 事务
        case TokenType::BEGIN: return "BEGIN";
        case TokenType::COMMIT: return "COMMIT";
        case TokenType::ROLLBACK: return "ROLLBACK";
        case TokenType::TRANSACTION: return "TRANSACTION";

        // 元数据
        case TokenType::SHOW: return "SHOW";
        case TokenType::TABLES: return "TABLES";
        case TokenType::INDEXES: return "INDEXES";
        case TokenType::DATABASE: return "DATABASE";
        case TokenType::DATABASES: return "DATABASES";
        case TokenType::USE: return "USE";
        case TokenType::DESCRIBE: return "DESCRIBE";
        case TokenType::EXPLAIN: return "EXPLAIN";
        case TokenType::SAVE: return "SAVE";
        case TokenType::VACUUM: return "VACUUM";

        // 权限
        case TokenType::GRANT: return "GRANT";
        case TokenType::REVOKE: return "REVOKE";
        case TokenType::TO: return "TO";
        case TokenType::WITH: return "WITH";
        case TokenType::OPTION: return "OPTION";

        // 集合操作
        case TokenType::UNION: return "UNION";
        case TokenType::INTERSECT: return "INTERSECT";
        case TokenType::EXCEPT: return "EXCEPT";

        // 全文搜索
        case TokenType::MATCH: return "MATCH";
        case TokenType::AGAINST: return "AGAINST";

        // 运算符
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::STAR: return "STAR";
        case TokenType::SLASH: return "SLASH";
        case TokenType::PERCENT: return "PERCENT";
        case TokenType::EQ: return "EQ";
        case TokenType::NE: return "NE";
        case TokenType::LT: return "LT";
        case TokenType::LE: return "LE";
        case TokenType::GT: return "GT";
        case TokenType::GE: return "GE";
        case TokenType::ASSIGN: return "ASSIGN";

        // 分隔符
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::COMMA: return "COMMA";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::DOT: return "DOT";

        // 特殊
        case TokenType::EOF_TOKEN: return "EOF";
        case TokenType::INVALID: return "INVALID";

        default: return "UNKNOWN";
    }
}

} // namespace qindb
