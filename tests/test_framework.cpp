#include "test_framework.h"
#include <QElapsedTimer>
#include <QDateTime>
#include <QDebug>
#include <iostream>
#include <iomanip>

namespace qindb {
namespace test {

// ========== TestCase 实现 ==========

void TestCase::assertTrue(bool condition, const QString& message) {
    if (!condition) {
        addResult("assertTrue", false, message);
        throw std::runtime_error(message.toStdString());
    }
}

void TestCase::assertFalse(bool condition, const QString& message) {
    if (condition) {
        addResult("assertFalse", false, message);
        throw std::runtime_error(message.toStdString());
    }
}

void TestCase::addResult(const QString& testName, bool passed, const QString& message, double elapsedMs) {
    results_.append(TestResult(testName, passed, message, elapsedMs));
}

void TestCase::startTimer() {
    startTimeNs_ = QDateTime::currentMSecsSinceEpoch() * 1000000;  // 转换为纳秒
}

double TestCase::stopTimer() {
    qint64 endTimeNs = QDateTime::currentMSecsSinceEpoch() * 1000000;
    return (endTimeNs - startTimeNs_) / 1000000.0;  // 转换为毫秒
}

// ========== TestSuite 实现 ==========

void TestSuite::addTest(TestCase* test) {
    tests_.append(test);
}

void TestSuite::runAll() {
    std::wcout << L"\n========================================" << std::endl;
    std::cout << "Running Test Suite: " << name_.toStdString() << std::endl;
    std::wcout << L"========================================" << std::endl;

    for (TestCase* test : tests_) {
        std::cout << "\n[TEST] " << test->getName().toStdString() << " ... ";
        std::cout.flush();

        try {
            test->run();

            const auto& results = test->getResults();
            bool allPassed = true;
            for (const auto& result : results) {
                stats_.addResult(result);
                if (!result.passed) {
                    allPassed = false;
                }
            }

            if (allPassed) {
                std::cout << "PASSED" << std::endl;
            } else {
                std::cout << "FAILED" << std::endl;
                for (const auto& result : results) {
                    if (!result.passed) {
                        std::cout << "  ✗ " << result.testName.toStdString()
                                  << ": " << result.message.toStdString() << std::endl;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cout << "EXCEPTION: " << e.what() << std::endl;
            stats_.addResult(TestResult(test->getName(), false, e.what()));
        }
    }
}

void TestSuite::printReport() const {
    std::wcout << L"\n========================================" << std::endl;
    std::cout << "Test Suite Report: " << name_.toStdString() << std::endl;
    std::wcout << L"========================================" << std::endl;
    std::cout << "Total Tests:  " << stats_.totalTests << std::endl;
    std::cout << "Passed:       " << stats_.passedTests << " (" << std::fixed << std::setprecision(1)
              << stats_.passRate() << "%)" << std::endl;
    std::cout << "Failed:       " << stats_.failedTests << std::endl;
    std::cout << "Total Time:   " << std::fixed << std::setprecision(2)
              << stats_.totalTimeMs << " ms" << std::endl;
    std::wcout << L"========================================\n" << std::endl;
}

// ========== TestRunner 实现 ==========

TestRunner& TestRunner::instance() {
    static TestRunner runner;
    return runner;
}

void TestRunner::registerSuite(TestSuite* suite) {
    suites_.append(suite);
}

int TestRunner::runAll() {
    std::wcout << L"\n╔════════════════════════════════════════╗" << std::endl;
    std::wcout << L"║     qinDB Automated Test Runner       ║" << std::endl;
    std::wcout << L"╚════════════════════════════════════════╝\n" << std::endl;

    for (TestSuite* suite : suites_) {
        suite->runAll();
        suite->printReport();

        const auto& stats = suite->getStatistics();
        globalStats_.totalTests += stats.totalTests;
        globalStats_.passedTests += stats.passedTests;
        globalStats_.failedTests += stats.failedTests;
        globalStats_.totalTimeMs += stats.totalTimeMs;
    }

    printSummary();

    return globalStats_.failedTests > 0 ? 1 : 0;  // 返回失败数（0表示全部通过）
}

void TestRunner::printSummary() const {
    std::wcout << L"\n╔════════════════════════════════════════╗" << std::endl;
    std::wcout << L"║          OVERALL SUMMARY               ║" << std::endl;
    std::wcout << L"╚════════════════════════════════════════╝" << std::endl;
    std::cout << "Total Test Suites: " << suites_.size() << std::endl;
    std::cout << "Total Tests:       " << globalStats_.totalTests << std::endl;
    std::cout << "Passed:            " << globalStats_.passedTests << " ("
              << std::fixed << std::setprecision(1) << globalStats_.passRate() << "%)" << std::endl;
    std::cout << "Failed:            " << globalStats_.failedTests << std::endl;
    std::cout << "Total Time:        " << std::fixed << std::setprecision(2)
              << globalStats_.totalTimeMs << " ms" << std::endl;

    if (globalStats_.failedTests == 0) {
        std::wcout << L"\n✓ ALL TESTS PASSED!" << std::endl;
    } else {
        std::wcout << L"\n✗ SOME TESTS FAILED!" << std::endl;
    }
    std::wcout << L"════════════════════════════════════════\n" << std::endl;
}

} // namespace test
} // namespace qindb
