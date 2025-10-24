#ifndef QINDB_TEST_FRAMEWORK_H
#define QINDB_TEST_FRAMEWORK_H

#include <QString>
#include <QVector>
#include <QMap>
#include <functional>
#include <iostream>

namespace qindb {
namespace test {

/**
 * @brief 测试结果
 */
struct TestResult {
    QString testName;
    bool passed;
    QString message;
    double elapsedMs;  // 执行时间（毫秒）

    TestResult(const QString& name, bool p, const QString& msg = "", double elapsed = 0.0)
        : testName(name), passed(p), message(msg), elapsedMs(elapsed) {}
};

/**
 * @brief 测试统计
 */
struct TestStatistics {
    int totalTests = 0;
    int passedTests = 0;
    int failedTests = 0;
    double totalTimeMs = 0.0;

    void addResult(const TestResult& result) {
        totalTests++;
        if (result.passed) {
            passedTests++;
        } else {
            failedTests++;
        }
        totalTimeMs += result.elapsedMs;
    }

    double passRate() const {
        return totalTests > 0 ? (passedTests * 100.0 / totalTests) : 0.0;
    }
};

/**
 * @brief 测试用例基类
 */
class TestCase {
public:
    explicit TestCase(const QString& name) : name_(name) {}
    virtual ~TestCase() = default;

    /**
     * @brief 运行测试
     */
    virtual void run() = 0;

    /**
     * @brief 获取测试结果
     */
    const QVector<TestResult>& getResults() const { return results_; }

    /**
     * @brief 获取测试名称
     */
    const QString& getName() const { return name_; }

protected:
    /**
     * @brief 断言条件为真
     */
    void assertTrue(bool condition, const QString& message = "Assertion failed");

    /**
     * @brief 断言两个值相等
     */
    template<typename T>
    void assertEqual(const T& expected, const T& actual, const QString& message = "Values not equal");

    /**
     * @brief 断言两个值不相等
     */
    template<typename T>
    void assertNotEqual(const T& expected, const T& actual, const QString& message = "Values should not be equal");

    /**
     * @brief 断言条件为假
     */
    void assertFalse(bool condition, const QString& message = "Assertion failed");

    /**
     * @brief 断言值为NULL
     */
    template<typename T>
    void assertNull(const T* ptr, const QString& message = "Pointer is not null");

    /**
     * @brief 断言值不为NULL
     */
    template<typename T>
    void assertNotNull(const T* ptr, const QString& message = "Pointer is null");

    /**
     * @brief 添加测试结果
     */
    void addResult(const QString& testName, bool passed, const QString& message = "", double elapsedMs = 0.0);

    /**
     * @brief 开始计时
     */
    void startTimer();

    /**
     * @brief 结束计时并返回经过的毫秒数
     */
    double stopTimer();

private:
    QString name_;
    QVector<TestResult> results_;
    qint64 startTimeNs_ = 0;  // 使用纳秒精度
};

/**
 * @brief 测试套件
 */
class TestSuite {
public:
    TestSuite(const QString& name) : name_(name) {}

    /**
     * @brief 添加测试用例
     */
    void addTest(TestCase* test);

    /**
     * @brief 运行所有测试
     */
    void runAll();

    /**
     * @brief 打印测试报告
     */
    void printReport() const;

    /**
     * @brief 获取测试统计
     */
    const TestStatistics& getStatistics() const { return stats_; }

    /**
     * @brief 获取套件名称
     */
    const QString& getName() const { return name_; }

private:
    QString name_;
    QVector<TestCase*> tests_;
    TestStatistics stats_;
};

/**
 * @brief 测试运行器
 */
class TestRunner {
public:
    static TestRunner& instance();

    /**
     * @brief 注册测试套件
     */
    void registerSuite(TestSuite* suite);

    /**
     * @brief 运行所有测试套件
     */
    int runAll();

    /**
     * @brief 打印总体报告
     */
    void printSummary() const;

private:
    TestRunner() = default;
    QVector<TestSuite*> suites_;
    TestStatistics globalStats_;
};

/**
 * @brief 辅助宏：定义测试用例类
 */
#define TEST_CLASS(ClassName) \
    class ClassName : public qindb::test::TestCase { \
    public: \
        ClassName() : TestCase(#ClassName) {} \
        void run() override; \
    };

/**
 * @brief 辅助宏：定义并注册测试
 */
#define TEST_CASE(TestName) \
    void TestName(); \
    static bool TestName##_registered = []() { \
        /* 测试注册逻辑将在实现时添加 */ \
        return true; \
    }(); \
    void TestName()

} // namespace test
} // namespace qindb

#endif // QINDB_TEST_FRAMEWORK_H
