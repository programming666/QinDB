#include "test_framework.h"
#include "qindb/ssLError_handler.h"
#include "qindb/certificate_generator.h"
#include <QSslCertificate>
#include <QSslError>
#include <QDateTime>
#include <iostream>

namespace qindb {
namespace test {

/**
 * @brief SSL错误处理器单元测试
 */
class TestSSLErrorHandler : public TestCase {
public:
    TestSSLErrorHandler() : TestCase("TestSSLErrorHandler") {}

    void run() override {
        try { testShouldIgnoreError(); } catch (...) {}
        try { testFilterIgnorableErrors(); } catch (...) {}
        try { testGetErrorSeverity(); } catch (...) {}
        try { testGetErrorDescription(); } catch (...) {}
        try { testIsSelfSignedError(); } catch (...) {}
        try { testIsCriticalError(); } catch (...) {}
        try { testValidateCertificateValidity(); } catch (...) {}
        try { testGetCertificateValidationError(); } catch (...) {}
    }

private:
    void testShouldIgnoreError() {
        startTimer();
        
        // 创建测试证书
        auto certKeyPair = CertificateGenerator::generateSelfSignedCertificate("TestCert", "QinDB-Test", 365);
        QSslCertificate cert = certKeyPair.first;
        assertFalse(cert.isNull(), "Test certificate should not be null");
        
        // 测试自签名证书错误
        QSslError selfSignedError(QSslError::SelfSignedCertificate, cert);
        
        // 当允许自签名时，应该忽略自签名错误
        bool shouldIgnore = SSLErrorHandler::shouldIgnoreError(selfSignedError, true);
        assertTrue(shouldIgnore, "Should ignore self-signed error when allowed");
        
        // 当不允许自签名时，不应该忽略自签名错误
        shouldIgnore = SSLErrorHandler::shouldIgnoreError(selfSignedError, false);
        assertFalse(shouldIgnore, "Should not ignore self-signed error when not allowed");
        
        // 测试其他错误类型
        QSslError expiredError(QSslError::CertificateExpired, cert);
        shouldIgnore = SSLErrorHandler::shouldIgnoreError(expiredError, true);
        assertFalse(shouldIgnore, "Should not ignore certificate expired error");
        
        double elapsed = stopTimer();
        addResult("testShouldIgnoreError", true, "", elapsed);
    }
    
    void testFilterIgnorableErrors() {
        startTimer();
        
        // 创建测试证书
        auto certKeyPair = CertificateGenerator::generateSelfSignedCertificate("TestCert", "QinDB-Test", 365);
        QSslCertificate cert = certKeyPair.first;
        
        // 创建错误列表
        QList<QSslError> errors;
        errors.append(QSslError(QSslError::SelfSignedCertificate, cert));
        errors.append(QSslError(QSslError::SelfSignedCertificateInChain, cert));
        errors.append(QSslError(QSslError::CertificateExpired, cert));
        errors.append(QSslError(QSslError::UnableToGetLocalIssuerCertificate, cert));
        
        // 当允许自签名时
        QList<QSslError> criticalErrors = SSLErrorHandler::filterIgnorableErrors(errors, true);
        assertEqual(static_cast<int>(1), static_cast<int>(criticalErrors.size()), "Should have 1 critical error when self-signed allowed");
        assertEqual(QSslError::CertificateExpired, criticalErrors[0].error(), "CertificateExpired should be critical");
        
        // 当不允许自签名时
        criticalErrors = SSLErrorHandler::filterIgnorableErrors(errors, false);
        assertEqual(static_cast<int>(4), static_cast<int>(criticalErrors.size()), "Should have 4 critical errors when self-signed not allowed");
        
        double elapsed = stopTimer();
        addResult("testFilterIgnorableErrors", true, "", elapsed);
    }
    
    void testGetErrorSeverity() {
        startTimer();
        
        // 创建测试证书
        auto certKeyPair = CertificateGenerator::generateSelfSignedCertificate("TestCert", "QinDB-Test", 365);
        QSslCertificate cert = certKeyPair.first;
        
        // 测试可忽略的错误
        QSslError selfSignedError(QSslError::SelfSignedCertificate, cert);
        auto severity = SSLErrorHandler::getErrorSeverity(selfSignedError, true);
        assertEqual(static_cast<int>(SSLErrorHandler::ErrorSeverity::IGNORABLE), 
                   static_cast<int>(severity), 
                   "Self-signed error should be ignorable when allowed");
        
        // 测试警告级别的错误
        severity = SSLErrorHandler::getErrorSeverity(selfSignedError, false);
        assertEqual(static_cast<int>(SSLErrorHandler::ErrorSeverity::WARNING), 
                   static_cast<int>(severity), 
                   "Self-signed error should be warning when not allowed");
        
        // 测试严重错误
        QSslError expiredError(QSslError::CertificateExpired, cert);
        severity = SSLErrorHandler::getErrorSeverity(expiredError, true);
        assertEqual(static_cast<int>(SSLErrorHandler::ErrorSeverity::CRITICAL), 
                   static_cast<int>(severity), 
                   "Certificate expired should be critical");
        
        QSslError revokedError(QSslError::CertificateRevoked, cert);
        severity = SSLErrorHandler::getErrorSeverity(revokedError, false);
        assertEqual(static_cast<int>(SSLErrorHandler::ErrorSeverity::CRITICAL), 
                   static_cast<int>(severity), 
                   "Certificate revoked should be critical");
        
        double elapsed = stopTimer();
        addResult("testGetErrorSeverity", true, "", elapsed);
    }
    
    void testGetErrorDescription() {
        startTimer();
        
        // 创建测试证书
        auto certKeyPair = CertificateGenerator::generateSelfSignedCertificate("TestCert", "QinDB-Test", 365);
        QSslCertificate cert = certKeyPair.first;
        
        // 测试各种错误类型的描述
        QSslError selfSignedError(QSslError::SelfSignedCertificate, cert);
        QString description = SSLErrorHandler::getErrorDescription(selfSignedError);
        assertFalse(description.isEmpty(), "Self-signed error description should not be empty");
        
        QSslError expiredError(QSslError::CertificateExpired, cert);
        description = SSLErrorHandler::getErrorDescription(expiredError);
        assertFalse(description.isEmpty(), "Expired error description should not be empty");
        
        QSslError noPeerError(QSslError::NoPeerCertificate);
        description = SSLErrorHandler::getErrorDescription(noPeerError);
        assertFalse(description.isEmpty(), "No peer certificate error description should not be empty");
        
        // 测试未知错误
        QSslError unknownError(static_cast<QSslError::SslError>(999), cert);
        description = SSLErrorHandler::getErrorDescription(unknownError);
        assertEqual(QString("Unknown SSL error"), description, "Unknown error should return default description");
        
        double elapsed = stopTimer();
        addResult("testGetErrorDescription", true, "", elapsed);
    }
    
    void testIsSelfSignedError() {
        startTimer();
        
        // 创建测试证书
        auto certKeyPair = CertificateGenerator::generateSelfSignedCertificate("TestCert", "QinDB-Test", 365);
        QSslCertificate cert = certKeyPair.first;
        
        // 测试自签名错误
        QSslError selfSignedError(QSslError::SelfSignedCertificate, cert);
        bool isSelfSigned = SSLErrorHandler::isSelfSignedError(selfSignedError);
        assertTrue(isSelfSigned, "SelfSignedCertificate should be detected as self-signed error");
        
        QSslError selfSignedChainError(QSslError::SelfSignedCertificateInChain, cert);
        isSelfSigned = SSLErrorHandler::isSelfSignedError(selfSignedChainError);
        assertTrue(isSelfSigned, "SelfSignedCertificateInChain should be detected as self-signed error");
        
        QSslError untrustedError(QSslError::CertificateUntrusted, cert);
        isSelfSigned = SSLErrorHandler::isSelfSignedError(untrustedError);
        assertTrue(isSelfSigned, "CertificateUntrusted should be detected as self-signed error");
        
        // 测试非自签名错误
        QSslError expiredError(QSslError::CertificateExpired, cert);
        isSelfSigned = SSLErrorHandler::isSelfSignedError(expiredError);
        assertFalse(isSelfSigned, "CertificateExpired should not be detected as self-signed error");
        
        QSslError revokedError(QSslError::CertificateRevoked, cert);
        isSelfSigned = SSLErrorHandler::isSelfSignedError(revokedError);
        assertFalse(isSelfSigned, "CertificateRevoked should not be detected as self-signed error");
        
        double elapsed = stopTimer();
        addResult("testIsSelfSignedError", true, "", elapsed);
    }
    
    void testIsCriticalError() {
        startTimer();
        
        // 创建测试证书
        auto certKeyPair = CertificateGenerator::generateSelfSignedCertificate("TestCert", "QinDB-Test", 365);
        QSslCertificate cert = certKeyPair.first;
        
        // 测试严重错误
        QSslError expiredError(QSslError::CertificateExpired, cert);
        bool isCritical = SSLErrorHandler::isCriticalError(expiredError);
        assertTrue(isCritical, "CertificateExpired should be critical error");
        
        QSslError revokedError(QSslError::CertificateRevoked, cert);
        isCritical = SSLErrorHandler::isCriticalError(revokedError);
        assertTrue(isCritical, "CertificateRevoked should be critical error");
        
        QSslError invalidCaError(QSslError::InvalidCaCertificate, cert);
        isCritical = SSLErrorHandler::isCriticalError(invalidCaError);
        assertTrue(isCritical, "InvalidCaCertificate should be critical error");
        
        // 测试非严重错误（自签名错误默认不是严重的）
        QSslError selfSignedError(QSslError::SelfSignedCertificate, cert);
        isCritical = SSLErrorHandler::isCriticalError(selfSignedError);
        assertFalse(isCritical, "SelfSignedCertificate should not be critical error by default");
        
        QSslError untrustedError(QSslError::CertificateUntrusted, cert);
        isCritical = SSLErrorHandler::isCriticalError(untrustedError);
        assertFalse(isCritical, "CertificateUntrusted should not be critical error by default");
        
        double elapsed = stopTimer();
        addResult("testIsCriticalError", true, "", elapsed);
    }
    
    void testValidateCertificateValidity() {
        startTimer();
        
        // 创建有效证书
        auto certKeyPair = CertificateGenerator::generateSelfSignedCertificate("ValidCert", "QinDB-Test", 365);
        QSslCertificate validCert = certKeyPair.first;
        
        // 验证有效证书
        bool isValid = SSLErrorHandler::validateCertificateValidity(validCert);
        assertTrue(isValid, "Valid certificate should pass validation");
        
        QString errorMessage;
        isValid = SSLErrorHandler::validateCertificateValidity(validCert, &errorMessage);
        assertTrue(isValid, "Valid certificate should pass validation with error message");
        assertTrue(errorMessage.isEmpty(), "Valid certificate should have no error message");
        
        // 测试空证书
        QSslCertificate nullCert;
        isValid = SSLErrorHandler::validateCertificateValidity(nullCert, &errorMessage);
        assertFalse(isValid, "Null certificate should fail validation");
        assertEqual(QString("Certificate is null"), errorMessage, "Null certificate should have correct error message");
        
        double elapsed = stopTimer();
        addResult("testValidateCertificateValidity", true, "", elapsed);
    }
    
    void testGetCertificateValidationError() {
        startTimer();
        
        // 创建有效证书
        auto certKeyPair = CertificateGenerator::generateSelfSignedCertificate("ValidCert", "QinDB-Test", 365);
        QSslCertificate validCert = certKeyPair.first;
        
        // 测试有效证书
        QString error = SSLErrorHandler::getCertificateValidationError(validCert);
        assertTrue(error.isEmpty(), "Valid certificate should have no validation error");
        
        // 测试空证书
        QSslCertificate nullCert;
        error = SSLErrorHandler::getCertificateValidationError(nullCert);
        assertEqual(QString("Certificate is null"), error, "Null certificate should have correct validation error");
        
        double elapsed = stopTimer();
        addResult("testGetCertificateValidationError", true, "", elapsed);
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
    TestSuite suite("SSL Error Handler Tests");

    // 添加测试
    suite.addTest(new TestSSLErrorHandler());

    // 运行测试
    suite.runAll();

    // 打印测试报告
    suite.printReport();

    return 0;
}
#endif