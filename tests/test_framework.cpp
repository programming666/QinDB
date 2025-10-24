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

template<typename T>
void TestCase::assertEqual(const T& expected, const T& actual, const QString& message) {
    if (expected != actual) {
        QString msg = message.isEmpty()
            ? QString("Expected: %1, Actual: %2").arg(expected).arg(actual)
            : message;
        addResult("assertEqual", false, msg);
        throw std::runtime_error(msg.toStdString());
    }
}

// 显式实例化常用类型
template void TestCase::assertEqual<int>(const int&, const int&, const QString&);
template void TestCase::assertEqual<QString>(const QString&, const QString&, const QString&);
template void TestCase::assertEqual<bool>(const bool&, const bool&, const QString&);
template void TestCase::assertEqual<double>(const double&, const double&, const QString&);
template void TestCase::assertEqual<unsigned long long>(const unsigned long long&, const unsigned long long&, const QString&);
template void TestCase::assertEqual<unsigned int>(const unsigned int&, const unsigned int&, const QString&);
template void TestCase::assertEqual<std::string>(const std::string&, const std::string&, const QString&);

template void TestCase::assertNotEqual<int>(const int&, const int&, const QString&);
template void TestCase::assertNotEqual<QString>(const QString&, const QString&, const QString&);

template<typename T>
void TestCase::assertNotEqual(const T& expected, const T& actual, const QString& message) {
    if (expected == actual) {
        QString msg = message.isEmpty()
            ? QString("Values should not be equal: %1").arg(expected)
            : message;
        addResult("assertNotEqual", false, msg);
        throw std::runtime_error(msg.toStdString());
    }
}

void TestCase::assertFalse(bool condition, const QString& message) {
    if (condition) {
        addResult("assertFalse", false, message);
        throw std::runtime_error(message.toStdString());
    }
}

template<typename T>
void TestCase::assertNull(const T* ptr, const QString& message) {
    if (ptr != nullptr) {
        addResult("assertNull", false, message);
        throw std::runtime_error(message.toStdString());
    }
}

template void TestCase::assertNull<int>(const int*, const QString&);
template void TestCase::assertNull<void>(const void*, const QString&);

template<typename T>
void TestCase::assertNotNull(const T* ptr, const QString& message) {
    if (ptr == nullptr) {
        addResult("assertNotNull", false, message);
        throw std::runtime_error(message.toStdString());
    }
}

template void TestCase::assertNotNull<int>(const int*, const QString&);
template void TestCase::assertNotNull<void>(const void*, const QString&);

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
    std::cout << "\n========================================" << std::endl;
    std::cout << "Running Test Suite: " << name_.toStdString() << std::endl;
    std::cout << "========================================" << std::endl;

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
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test Suite Report: " << name_.toStdString() << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total Tests:  " << stats_.totalTests << std::endl;
    std::cout << "Passed:       " << stats_.passedTests << " (" << std::fixed << std::setprecision(1)
              << stats_.passRate() << "%)" << std::endl;
    std::cout << "Failed:       " << stats_.failedTests << std::endl;
    std::cout << "Total Time:   " << std::fixed << std::setprecision(2)
              << stats_.totalTimeMs << " ms" << std::endl;
    std::cout << "========================================\n" << std::endl;
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
    std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║     qinDB Automated Test Runner       ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝\n" << std::endl;

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
    std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║          OVERALL SUMMARY               ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    std::cout << "Total Test Suites: " << suites_.size() << std::endl;
    std::cout << "Total Tests:       " << globalStats_.totalTests << std::endl;
    std::cout << "Passed:            " << globalStats_.passedTests << " ("
              << std::fixed << std::setprecision(1) << globalStats_.passRate() << "%)" << std::endl;
    std::cout << "Failed:            " << globalStats_.failedTests << std::endl;
    std::cout << "Total Time:        " << std::fixed << std::setprecision(2)
              << globalStats_.totalTimeMs << " ms" << std::endl;

    if (globalStats_.failedTests == 0) {
        std::cout << "\n✓ ALL TESTS PASSED!" << std::endl;
    } else {
        std::cout << "\n✗ SOME TESTS FAILED!" << std::endl;
    }
    std::cout << "════════════════════════════════════════\n" << std::endl;
}

} // namespace test
} // namespace qindb
