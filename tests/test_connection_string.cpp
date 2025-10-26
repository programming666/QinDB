#include "test_framework.h"
#include "qindb/connection_string_parser.h"
#include <iostream>
#include <vector>

namespace qindb {
namespace test {

/**
 * @brief 连接字符串解析单元测试
 */
class TestConnectionString : public TestCase {
public:
    TestConnectionString() : TestCase("TestConnectionString") {}

    void run() override {
        try { testParseValidConnectionString(); } catch (...) {}
        try { testParseConnectionStringWithDefaultPort(); } catch (...) {}
        try { testParseConnectionStringWithDifferentSslFormats(); } catch (...) {}
        try { testParseConnectionStringWithMissingParams(); } catch (...) {}
        try { testParseInvalidConnectionString(); } catch (...) {}
        try { testIsValidConnectionString(); } catch (...) {}
    }

private:
    void testParseValidConnectionString() {
        startTimer();

        // 测试有效的连接字符串
        QString connectionString = "qindb://localhost:24678?usr=admin&pswd=123&ssl=false";
        auto paramsOpt = qindb::ConnectionStringParser::parse(connectionString);

        assertTrue(paramsOpt.has_value(), "连接字符串解析失败");

        auto params = paramsOpt.value();
        assertEqual(QString("localhost"), params.host, "主机地址解析错误");
        assertEqual(static_cast<int>(24678), static_cast<int>(params.port), "端口号解析错误");
        assertEqual(QString("admin"), params.username, "用户名解析错误");
        assertEqual(QString("123"), params.password, "密码解析错误");
        assertEqual(static_cast<int>(false), static_cast<int>(params.sslEnabled), "SSL设置解析错误");

        double elapsed = stopTimer();
        addResult("testParseValidConnectionString", true, "", elapsed);
    }

    void testParseConnectionStringWithDefaultPort() {
        startTimer();

        // 测试默认端口
        QString connectionString = "qindb://192.168.1.100?usr=test&pswd=password&ssl=true";
        auto paramsOpt = qindb::ConnectionStringParser::parse(connectionString);

        assertTrue(paramsOpt.has_value(), "连接字符串解析失败");

        auto params = paramsOpt.value();
        assertEqual(QString("192.168.1.100"), params.host, "主机地址解析错误");
        assertEqual(static_cast<int>(24678), static_cast<int>(params.port), "默认端口号错误");
        assertEqual(QString("test"), params.username, "用户名解析错误");
        assertEqual(QString("password"), params.password, "密码解析错误");
        assertEqual(static_cast<int>(true), static_cast<int>(params.sslEnabled), "SSL设置解析错误");

        double elapsed = stopTimer();
        addResult("testParseConnectionStringWithDefaultPort", true, "", elapsed);
    }

    void testParseConnectionStringWithDifferentSslFormats() {
        startTimer();

        // 测试不同的SSL格式
        std::vector<QString> sslTrueValues = {"true", "True", "TRUE", "1", "yes", "Yes", "YES", "on", "On", "ON"};
        std::vector<QString> sslFalseValues = {"false", "False", "FALSE", "0", "no", "No", "NO", "off", "Off", "OFF", "invalid"};

        for (const auto& sslValue : sslTrueValues) {
            QString connectionString = QString("qindb://localhost?usr=test&pswd=123&ssl=%1").arg(sslValue);
            auto paramsOpt = qindb::ConnectionStringParser::parse(connectionString);
            assertTrue(paramsOpt.has_value(), QString("SSL值 '%1' 解析失败").arg(sslValue));
            assertEqual(static_cast<int>(true), static_cast<int>(paramsOpt.value().sslEnabled), QString("SSL值 '%1' 应该为true").arg(sslValue));
        }

        for (const auto& sslValue : sslFalseValues) {
            QString connectionString = QString("qindb://localhost?usr=test&pswd=123&ssl=%1").arg(sslValue);
            auto paramsOpt = qindb::ConnectionStringParser::parse(connectionString);
            assertTrue(paramsOpt.has_value(), QString("SSL值 '%1' 解析失败").arg(sslValue));
            assertEqual(static_cast<int>(false), static_cast<int>(paramsOpt.value().sslEnabled), QString("SSL值 '%1' 应该为false").arg(sslValue));
        }

        double elapsed = stopTimer();
        addResult("testParseConnectionStringWithDifferentSslFormats", true, "", elapsed);
    }

    void testParseConnectionStringWithMissingParams() {
        startTimer();

        // 测试缺少参数的连接字符串
        QString connectionString = "qindb://localhost:5432";
        auto paramsOpt = qindb::ConnectionStringParser::parse(connectionString);

        assertTrue(paramsOpt.has_value(), "连接字符串解析失败");

        auto params = paramsOpt.value();
        assertEqual(QString("localhost"), params.host, "主机地址解析错误");
        assertEqual(static_cast<int>(5432), static_cast<int>(params.port), "端口号解析错误");
        assertTrue(params.username.isEmpty(), "用户名应该为空");
        assertTrue(params.password.isEmpty(), "密码应该为空");
        assertEqual(static_cast<int>(false), static_cast<int>(params.sslEnabled), "SSL应该默认为false");

        double elapsed = stopTimer();
        addResult("testParseConnectionStringWithMissingParams", true, "", elapsed);
    }

    void testParseInvalidConnectionString() {
        startTimer();

        // 测试无效的连接字符串
        std::vector<QString> invalidStrings = {
            "invalid://localhost:24678?usr=admin&pswd=123&ssl=false",
            "qindb://",  // 缺少主机名
            "localhost:24678?usr=admin&pswd=123&ssl=false",  // 缺少qindb://前缀
            "",  // 空字符串
            "qindb://localhost:abc?usr=admin&pswd=123&ssl=false"  // 无效的端口
        };

        for (const auto& invalidString : invalidStrings) {
            auto paramsOpt = qindb::ConnectionStringParser::parse(invalidString);
            assertFalse(paramsOpt.has_value(), QString("无效连接字符串 '%1' 不应该被解析成功").arg(invalidString));
        }

        double elapsed = stopTimer();
        addResult("testParseInvalidConnectionString", true, "", elapsed);
    }

    void testIsValidConnectionString() {
        startTimer();

        // 测试连接字符串验证
        std::vector<QString> validStrings = {
            "qindb://localhost:24678?usr=admin&pswd=123&ssl=false",
            "qindb://192.168.1.100?usr=test&ssl=true",
            "qindb://example.com:5432",
            "qindb://host.domain.com:1234?usr=user&pswd=pass",
            "qindb://localhost"  // 这是一个有效的连接字符串，只是没有端口和参数
        };

        std::vector<QString> invalidStrings = {
            "invalid://localhost:24678?usr=admin&pswd=123&ssl=false",
            "qindb://",  // 缺少主机名
            "localhost:24678?usr=admin&pswd=123&ssl=false",  // 缺少qindb://前缀
            "",  // 空字符串
            "qindb://localhost:abc?usr=admin&pswd=123&ssl=false"  // 无效的端口
        };

        for (const auto& validString : validStrings) {
            assertTrue(qindb::ConnectionStringParser::isValid(validString),
                       QString("有效连接字符串 '%1' 验证失败").arg(validString));
        }

        for (const auto& invalidString : invalidStrings) {
            assertFalse(qindb::ConnectionStringParser::isValid(invalidString),
                        QString("无效连接字符串 '%1' 验证应该失败").arg(invalidString));
        }

        double elapsed = stopTimer();
        addResult("testIsValidConnectionString", true, "", elapsed);
    }
};

} // namespace test
} // namespace qindb

#ifndef QINDB_TEST_MAIN_INCLUDED
#include <QCoreApplication>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    using namespace qindb::test;

    // 创建测试套件
    TestSuite suite("Connection String Parser Tests");

    // 添加测试
    suite.addTest(new TestConnectionString());

    // 运行测试
    suite.runAll();

    // 打印测试报告
    suite.printReport();

    return 0;
}
#endif
