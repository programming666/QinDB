#include "test_framework.h"
#include "qindb/result_exporter.h"
#include "qindb/query_result.h"
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <iostream>

using namespace qindb;
using namespace qindb::test;

/**
 * @brief ResultExporter 测试套件
 */
class ResultExporterTests : public TestCase {
public:
    ResultExporterTests() : TestCase("ResultExporterTests") {}

    void run() override {
        testExportToCSV();
        testExportToJSON();
        testExportToXML();
        testExportToFile();
        testExportEmptyResult();
        testExportWithNullValues();
        testExportSpecialCharacters();
    }

private:
    QueryResult createTestResult() {
        QueryResult result;
        result.success = true;
        result.columnNames = {"id", "name", "salary"};

        QVector<QVariant> row1 = {1, "Alice", 50000.0};
        QVector<QVariant> row2 = {2, "Bob", 60000.0};
        QVector<QVariant> row3 = {3, "Charlie", 55000.0};

        result.rows.append(row1);
        result.rows.append(row2);
        result.rows.append(row3);

        return result;
    }

    void testExportToCSV() {
        startTimer();
        try {
            QueryResult result = createTestResult();
            QString csv = ResultExporter::exportToCSV(result);

            assertTrue(!csv.isEmpty(), "CSV output should not be empty");
            // CSV格式使用引号包围字段
            assertTrue(csv.contains("\"id\"") && csv.contains("\"name\"") && csv.contains("\"salary\""), "CSV should contain header");
            assertTrue(csv.contains("Alice"), "CSV should contain data");
            assertTrue(csv.contains("50000"), "CSV should contain numeric data");

            addResult("testExportToCSV", true, "CSV export works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExportToCSV", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExportToJSON() {
        startTimer();
        try {
            QueryResult result = createTestResult();
            QString json = ResultExporter::exportToJSON(result);

            assertTrue(!json.isEmpty(), "JSON output should not be empty");
            assertTrue(json.contains("\"columns\""), "JSON should contain columns field");
            assertTrue(json.contains("\"rows\""), "JSON should contain rows field");
            assertTrue(json.contains("\"rowCount\""), "JSON should contain rowCount field");
            assertTrue(json.contains("Alice"), "JSON should contain data");

            addResult("testExportToJSON", true, "JSON export works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExportToJSON", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExportToXML() {
        startTimer();
        try {
            QueryResult result = createTestResult();
            QString xml = ResultExporter::exportToXML(result);

            assertTrue(!xml.isEmpty(), "XML output should not be empty");
            assertTrue(xml.contains("<?xml version=\"1.0\""), "XML should contain declaration");
            assertTrue(xml.contains("<resultset>"), "XML should contain resultset tag");
            assertTrue(xml.contains("<columns>"), "XML should contain columns tag");
            assertTrue(xml.contains("<rows>"), "XML should contain rows tag");
            assertTrue(xml.contains("Alice"), "XML should contain data");

            addResult("testExportToXML", true, "XML export works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExportToXML", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExportToFile() {
        startTimer();
        try {
            QueryResult result = createTestResult();
            QString testFile = "test_export_output.csv";

            // 删除旧文件（如果存在）
            QFile::remove(testFile);

            bool success = ResultExporter::exportToFile(result, ExportFormat::CSV, testFile);
            assertTrue(success, "Export to file should succeed");

            // 验证文件存在
            QFile file(testFile);
            assertTrue(file.exists(), "Exported file should exist");

            // 读取文件内容
            assertTrue(file.open(QIODevice::ReadOnly | QIODevice::Text), "Should be able to open file");
            QTextStream in(&file);
            QString content = in.readAll();
            file.close();

            assertTrue(!content.isEmpty(), "File content should not be empty");
            assertTrue(content.contains("Alice"), "File should contain data");

            // 清理
            QFile::remove(testFile);

            addResult("testExportToFile", true, "File export works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExportToFile", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExportEmptyResult() {
        startTimer();
        try {
            QueryResult result;
            result.success = true;
            result.columnNames = {"id", "name"};
            // No rows

            QString csv = ResultExporter::exportToCSV(result);
            assertTrue(!csv.isEmpty(), "CSV should contain at least header");
            // CSV格式使用引号
            assertTrue(csv.contains("\"id\"") && csv.contains("\"name\""), "CSV should contain header even for empty result");

            QString json = ResultExporter::exportToJSON(result);
            assertTrue(json.contains("\"rowCount\": 0"), "JSON should show rowCount as 0");

            addResult("testExportEmptyResult", true, "Empty result export works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExportEmptyResult", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExportWithNullValues() {
        startTimer();
        try {
            QueryResult result;
            result.success = true;
            result.columnNames = {"id", "name", "age"};

            QVector<QVariant> row1 = {1, "Alice", QVariant()};  // NULL age
            QVector<QVariant> row2 = {2, QVariant(), 25};       // NULL name

            result.rows.append(row1);
            result.rows.append(row2);

            QString csv = ResultExporter::exportToCSV(result);
            assertTrue(csv.contains("NULL") || csv.contains("\"\""), "CSV should handle NULL values");

            QString json = ResultExporter::exportToJSON(result);
            assertTrue(json.contains("null"), "JSON should represent NULL as null");

            addResult("testExportWithNullValues", true, "NULL value export works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExportWithNullValues", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }

    void testExportSpecialCharacters() {
        startTimer();
        try {
            QueryResult result;
            result.success = true;
            result.columnNames = {"text"};

            // 包含特殊字符的数据
            QVector<QVariant> row1 = {"Text with \"quotes\""};
            QVector<QVariant> row2 = {"Text with <xml> tags"};
            QVector<QVariant> row3 = {"Text with, comma"};

            result.rows.append(row1);
            result.rows.append(row2);
            result.rows.append(row3);

            QString csv = ResultExporter::exportToCSV(result);
            assertTrue(!csv.isEmpty(), "CSV should handle special characters");

            QString json = ResultExporter::exportToJSON(result);
            assertTrue(!json.isEmpty(), "JSON should handle special characters");

            QString xml = ResultExporter::exportToXML(result);
            assertTrue(!xml.isEmpty(), "XML should handle special characters");
            assertTrue(xml.contains("&lt;") || xml.contains("<![CDATA["), "XML should escape < character");

            addResult("testExportSpecialCharacters", true, "Special character export works", stopTimer());
        } catch (const std::exception& e) {
            addResult("testExportSpecialCharacters", false, QString("Exception: %1").arg(e.what()), stopTimer());
        }
    }
};

#ifndef QINDB_TEST_MAIN_INCLUDED
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    TestSuite suite("Result Exporter Tests");
    suite.addTest(new ResultExporterTests());

    TestRunner::instance().registerSuite(&suite);
    int result = TestRunner::instance().runAll();

    return result;
}
#endif
