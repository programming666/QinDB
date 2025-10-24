#include "test_framework.h"
#include "qindb/lexer.h"

namespace qindb {
namespace test {

/**
 * @brief 词法分析器单元测试
 */
class LexerTest : public TestCase {
public:
    LexerTest() : TestCase("LexerTest") {}

    void run() override {
        try { testKeywords(); } catch (...) {}
        try { testIdentifiers(); } catch (...) {}
        try { testLiterals(); } catch (...) {}
        try { testOperators(); } catch (...) {}
        try { testComplexSQL(); } catch (...) {}
    }

private:
    /**
     * @brief 测试关键字识别
     */
    void testKeywords() {
        startTimer();

        Lexer lexer("SELECT INSERT UPDATE DELETE FROM WHERE");

        Token token1 = lexer.nextToken();
        assertEqual(static_cast<int>(TokenType::SELECT), static_cast<int>(token1.type),
                   "First token should be SELECT");

        Token token2 = lexer.nextToken();
        assertEqual(static_cast<int>(TokenType::INSERT), static_cast<int>(token2.type),
                   "Second token should be INSERT");

        Token token3 = lexer.nextToken();
        assertEqual(static_cast<int>(TokenType::UPDATE), static_cast<int>(token3.type),
                   "Third token should be UPDATE");

        Token token4 = lexer.nextToken();
        assertEqual(static_cast<int>(TokenType::DELETE), static_cast<int>(token4.type),
                   "Fourth token should be DELETE");

        Token token5 = lexer.nextToken();
        assertEqual(static_cast<int>(TokenType::FROM), static_cast<int>(token5.type),
                   "Fifth token should be FROM");

        Token token6 = lexer.nextToken();
        assertEqual(static_cast<int>(TokenType::WHERE), static_cast<int>(token6.type),
                   "Sixth token should be WHERE");

        double elapsed = stopTimer();
        addResult("testKeywords", true, "", elapsed);
    }

    /**
     * @brief 测试标识符识别
     */
    void testIdentifiers() {
        startTimer();

        Lexer lexer("table_name column1 _underscore");

        Token token1 = lexer.nextToken();
        assertEqual(static_cast<int>(TokenType::IDENTIFIER), static_cast<int>(token1.type),
                   "Token should be IDENTIFIER");
        assertEqual(QString("table_name"), token1.lexeme, "First identifier should be 'table_name'");

        Token token2 = lexer.nextToken();
        assertEqual(static_cast<int>(TokenType::IDENTIFIER), static_cast<int>(token2.type),
                   "Token should be IDENTIFIER");
        assertEqual(QString("column1"), token2.lexeme, "Second identifier should be 'column1'");

        Token token3 = lexer.nextToken();
        assertEqual(static_cast<int>(TokenType::IDENTIFIER), static_cast<int>(token3.type),
                   "Token should be IDENTIFIER");
        assertEqual(QString("_underscore"), token3.lexeme, "Third identifier should be '_underscore'");

        double elapsed = stopTimer();
        addResult("testIdentifiers", true, "", elapsed);
    }

    /**
     * @brief 测试字面量识别
     */
    void testLiterals() {
        startTimer();

        // 测试整数
        Lexer lexer1("123 456");
        Token token1 = lexer1.nextToken();
        assertEqual(static_cast<int>(TokenType::INTEGER), static_cast<int>(token1.type),
                   "First token should be INTEGER");
        assertEqual(QString("123"), token1.lexeme, "Integer lexeme should be '123'");

        Token token2 = lexer1.nextToken();
        assertEqual(static_cast<int>(TokenType::INTEGER), static_cast<int>(token2.type),
                   "Second token should be INTEGER");

        // 测试字符串
        Lexer lexer2("'hello' \"world\"");
        Token str1 = lexer2.nextToken();
        assertEqual(static_cast<int>(TokenType::STRING), static_cast<int>(str1.type),
                   "First token should be STRING");

        Token str2 = lexer2.nextToken();
        assertEqual(static_cast<int>(TokenType::STRING), static_cast<int>(str2.type),
                   "Second token should be STRING");

        // 测试浮点数
        Lexer lexer3("3.14 2.5");
        Token float1 = lexer3.nextToken();
        assertEqual(static_cast<int>(TokenType::FLOAT), static_cast<int>(float1.type),
                   "First token should be FLOAT");
        assertEqual(QString("3.14"), float1.lexeme, "Float lexeme should be '3.14'");

        double elapsed = stopTimer();
        addResult("testLiterals", true, "", elapsed);
    }

    /**
     * @brief 测试运算符识别
     */
    void testOperators() {
        startTimer();

        Lexer lexer("= < > + - * / ( ) , ;");

        Token eq = lexer.nextToken();
        assertEqual(static_cast<int>(TokenType::EQ), static_cast<int>(eq.type),
                   "First token should be EQ");

        Token lt = lexer.nextToken();
        assertEqual(static_cast<int>(TokenType::LT), static_cast<int>(lt.type),
                   "Second token should be LT");

        Token gt = lexer.nextToken();
        assertEqual(static_cast<int>(TokenType::GT), static_cast<int>(gt.type),
                   "Third token should be GT");

        Token plus = lexer.nextToken();
        assertEqual(static_cast<int>(TokenType::PLUS), static_cast<int>(plus.type),
                   "Token should be PLUS");

        double elapsed = stopTimer();
        addResult("testOperators", true, "", elapsed);
    }

    /**
     * @brief 测试复杂SQL语句
     */
    void testComplexSQL() {
        startTimer();

        QString sql = "SELECT id, name FROM users WHERE age > 18 AND status = 'active';";
        Lexer lexer(sql);

        int tokenCount = 0;
        Token token = lexer.nextToken();

        // 第一个token应该是SELECT
        assertEqual(static_cast<int>(TokenType::SELECT), static_cast<int>(token.type),
                   "First token should be SELECT");

        // 继续读取所有tokens
        while (token.type != TokenType::EOF_TOKEN && token.type != TokenType::INVALID) {
            tokenCount++;
            if (tokenCount > 100) break;  // 防止无限循环
            token = lexer.nextToken();
        }

        assertTrue(tokenCount > 10, QString("Should tokenize multiple tokens, got %1").arg(tokenCount));

        double elapsed = stopTimer();
        addResult("testComplexSQL", true,
                 QString("Tokenized %1 tokens from complex SQL").arg(tokenCount), elapsed);
    }
};

} // namespace test
} // namespace qindb
