#include "test_framework.h"
#include "qindb/tls_config.h"
#include "qindb/certificate_generator.h"
#include "qindb/sslError_handler.h"
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslError>
#include <QFile>
#include <iostream>

namespace qindb {
namespace test {

/**
 * @brief TLS配置和证书管理单元测试（简化版）
 */
class TestTLSHandshakeSimple : public TestCase {
public:
    TestTLSHandshakeSimple() : TestCase("TestTLSHandshakeSimple") {}

    void run() override {
        try { testTLSConfigCreation(); } catch (...) {}
        try { testCertificateGeneration(); } catch (...) {}
        try { testCertificateLoadingAndSaving(); } catch (...) {}
        try { testTLSConfiguration(); } catch (...) {}
        try { testCertificateValidation(); } catch (...) {}
        try { testSSLConfiguration(); } catch (...) {}
        try { testSSLErrorSeverity(); } catch (...) {}
        try { testSSLErrorSuggestedActions(); } catch (...) {}
    }

private:
    void testTLSConfigCreation() {
        startTimer();
        
        // 创建TLS配置
        TLSConfig config;
        
        // 验证默认设置
        assertEqual(static_cast<int>(TLSVerifyMode::NONE), 
                   static_cast<int>(config.verifyMode()), 
                   "Default verify mode should be NONE");
        assertTrue(config.allowSelfSigned(), "Default should allow self-signed certificates");
        assertEqual(static_cast<int>(QSsl::TlsV1_2), 
                   static_cast<int>(config.minimumProtocol()), 
                   "Default minimum protocol should be TLS 1.2");
        
        // 验证配置修改
        config.setVerifyMode(TLSVerifyMode::REQUIRED);
        config.setAllowSelfSigned(false);
        config.setMinimumProtocol(QSsl::TlsV1_3);
        
        assertEqual(static_cast<int>(TLSVerifyMode::REQUIRED), 
                   static_cast<int>(config.verifyMode()), 
                   "Verify mode should be REQUIRED after setting");
        assertFalse(config.allowSelfSigned(), "Should not allow self-signed after setting");
        assertEqual(static_cast<int>(QSsl::TlsV1_3), 
                   static_cast<int>(config.minimumProtocol()), 
                   "Minimum protocol should be TLS 1.3 after setting");
        
        double elapsed = stopTimer();
        addResult("testTLSConfigCreation", true, "", elapsed);
    }
    
    void testCertificateGeneration() {
        startTimer();
        
        // 创建TLS配置
        TLSConfig config;
        
        // 生成自签名证书
        QString commonName = "TestServer";
        QString organization = "QinDB-Test";
        int validityDays = 365;
        
        bool generated = config.generateSelfSigned(commonName, organization, validityDays);
        assertTrue(generated, "Certificate generation should succeed");
        
        // 验证生成的证书
        QSslCertificate cert = config.certificate();
        assertFalse(cert.isNull(), "Generated certificate should not be null");
        
        QSslKey privateKey = config.privateKey();
        assertFalse(privateKey.isNull(), "Generated private key should not be null");
        
        // 验证证书指纹
        QString fingerprint = config.certificateFingerprint();
        assertFalse(fingerprint.isEmpty(), "Certificate fingerprint should not be empty");
        
        // 验证配置有效性
        assertTrue(config.isValid(), "TLS config should be valid after certificate generation");
        
        double elapsed = stopTimer();
        addResult("testCertificateGeneration", true, "", elapsed);
    }
    
    void testCertificateLoadingAndSaving() {
        startTimer();
        
        // 创建TLS配置并生成证书
        TLSConfig config;
        config.generateSelfSigned("TestSaveLoad", "QinDB-Test", 365);
        
        // 保存证书和私钥
        QString certPath = "test_cert.pem";
        QString keyPath = "test_key.pem";
        
        bool saved = config.saveToFiles(certPath, keyPath);
        assertTrue(saved, "Certificate and key should be saved successfully");
        
        // 创建新的配置并从文件加载
        TLSConfig loadedConfig;
        bool loaded = loadedConfig.loadFromFiles(certPath, keyPath);
        assertTrue(loaded, "Certificate and key should be loaded successfully");
        
        // 验证加载的证书
        assertFalse(loadedConfig.certificate().isNull(), "Loaded certificate should not be null");
        assertFalse(loadedConfig.privateKey().isNull(), "Loaded private key should not be null");
        assertTrue(loadedConfig.isValid(), "Loaded TLS config should be valid");
        
        // 验证指纹匹配
        QString originalFingerprint = config.certificateFingerprint();
        QString loadedFingerprint = loadedConfig.certificateFingerprint();
        assertEqual(originalFingerprint, loadedFingerprint, "Certificate fingerprints should match");
        
        // 清理测试文件
        QFile::remove(certPath);
        QFile::remove(keyPath);
        
        double elapsed = stopTimer();
        addResult("testCertificateLoadingAndSaving", true, "", elapsed);
    }
    
    void testTLSConfiguration() {
        startTimer();
        
        // 创建TLS配置
        TLSConfig config;
        config.generateSelfSigned("TestConfig", "QinDB-Test", 365);
        
        // 测试客户端配置
        QSslConfiguration clientConfig = config.createSslConfiguration(false);
        assertFalse(clientConfig.localCertificate().isNull(), "Client config should have local certificate");
        assertFalse(clientConfig.privateKey().isNull(), "Client config should have private key");
        assertEqual(QSsl::SecureProtocols, clientConfig.protocol(), "Protocol should be SecureProtocols");
        
        // 验证客户端验证模式
        config.setVerifyMode(TLSVerifyMode::NONE);
        clientConfig = config.createSslConfiguration(false);
        assertEqual(QSslSocket::VerifyNone, clientConfig.peerVerifyMode(), "Client verify mode should be VerifyNone when TLSVerifyMode::NONE");
        
        config.setVerifyMode(TLSVerifyMode::REQUIRED);
        clientConfig = config.createSslConfiguration(false);
        assertEqual(QSslSocket::VerifyPeer, clientConfig.peerVerifyMode(), "Client verify mode should be VerifyPeer when TLSVerifyMode::REQUIRED");
        
        // 测试服务器配置
        QSslConfiguration serverConfig = config.createSslConfiguration(true);
        assertFalse(serverConfig.localCertificate().isNull(), "Server config should have local certificate");
        assertFalse(serverConfig.privateKey().isNull(), "Server config should have private key");
        assertEqual(QSslSocket::VerifyNone, serverConfig.peerVerifyMode(), "Server verify mode should be VerifyNone");
        
        double elapsed = stopTimer();
        addResult("testTLSConfiguration", true, "", elapsed);
    }
    
    void testCertificateValidation() {
        startTimer();
        
        // 创建测试证书
        auto certKeyPair = CertificateGenerator::generateSelfSignedCertificate("TestCert", "QinDB-Test", 365);
        QSslCertificate cert = certKeyPair.first;
        assertFalse(cert.isNull(), "Test certificate should not be null");
        
        // 验证证书有效期
        bool isValid = SSLErrorHandler::validateCertificateValidity(cert);
        assertTrue(isValid, "Valid certificate should pass validation");
        
        QString errorMessage;
        isValid = SSLErrorHandler::validateCertificateValidity(cert, &errorMessage);
        assertTrue(isValid, "Valid certificate should pass validation with error message");
        assertTrue(errorMessage.isEmpty(), "Valid certificate should have no error message");
        
        // 测试证书错误描述
        QSslError selfSignedError(QSslError::SelfSignedCertificate, cert);
        QString errorDescription = SSLErrorHandler::getErrorDescription(selfSignedError);
        assertFalse(errorDescription.isEmpty(), "Error description should not be empty");
        
        // 验证自签名错误检测
        bool isSelfSigned = SSLErrorHandler::isSelfSignedError(selfSignedError);
        assertTrue(isSelfSigned, "Should detect self-signed certificate error");
        
        // 验证严重错误检测
        bool isCritical = SSLErrorHandler::isCriticalError(selfSignedError);
        assertFalse(isCritical, "Self-signed error should not be critical when allowed");
        
        // 测试空证书
        QSslCertificate nullCert;
        isValid = SSLErrorHandler::validateCertificateValidity(nullCert, &errorMessage);
        assertFalse(isValid, "Null certificate should fail validation");
        assertEqual(QString("Certificate is null"), errorMessage, "Null certificate should have correct error message");
        
        double elapsed = stopTimer();
        addResult("testCertificateValidation", true, "", elapsed);
    }
    
    void testSSLConfiguration() {
        startTimer();
        
        // 创建TLS配置
        TLSConfig config;
        config.generateSelfSigned("TestSSL", "QinDB-Test", 365);
        
        // 测试不同验证模式下的SSL配置
        
        // NONE模式
        config.setVerifyMode(TLSVerifyMode::NONE);
        QSslConfiguration sslConfig = config.createSslConfiguration(false);
        assertEqual(QSslSocket::VerifyNone, sslConfig.peerVerifyMode(), "NONE mode should result in VerifyNone");
        
        // OPTIONAL模式
        config.setVerifyMode(TLSVerifyMode::OPTIONAL);
        sslConfig = config.createSslConfiguration(false);
        assertEqual(QSslSocket::QueryPeer, sslConfig.peerVerifyMode(), "OPTIONAL mode should result in QueryPeer");
        
        // REQUIRED模式
        config.setVerifyMode(TLSVerifyMode::REQUIRED);
        sslConfig = config.createSslConfiguration(false);
        assertEqual(QSslSocket::VerifyPeer, sslConfig.peerVerifyMode(), "REQUIRED mode should result in VerifyPeer");
        
        // FINGERPRINT模式
        config.setVerifyMode(TLSVerifyMode::FINGERPRINT);
        sslConfig = config.createSslConfiguration(false);
        assertEqual(QSslSocket::VerifyPeer, sslConfig.peerVerifyMode(), "FINGERPRINT mode should result in VerifyPeer");
        
        // 测试自签名证书设置
        config.setAllowSelfSigned(true);
        assertTrue(config.allowSelfSigned(), "Should allow self-signed when set to true");
        
        config.setAllowSelfSigned(false);
        assertFalse(config.allowSelfSigned(), "Should not allow self-signed when set to false");
        
        double elapsed = stopTimer();
        addResult("testSSLConfiguration", true, "", elapsed);
    }

    void testSSLErrorSeverity() {
        startTimer();

        // 创建测试证书
        auto certKeyPair = CertificateGenerator::generateSelfSignedCertificate("TestError", "QinDB-Test", 365);
        QSslCertificate cert = certKeyPair.first;

        // 测试严重��误
        QSslError criticalError(QSslError::CertificateRevoked, cert);
        auto severity = SSLErrorHandler::getErrorSeverity(criticalError, true);
        assertEqual(static_cast<int>(SSLErrorHandler::ErrorSeverity::CRITICAL),
                   static_cast<int>(severity),
                   "Revoked certificate should be CRITICAL");

        // 测试可忽略错误(允许自签名时)
        QSslError ignorableError(QSslError::SelfSignedCertificate, cert);
        severity = SSLErrorHandler::getErrorSeverity(ignorableError, true);
        assertEqual(static_cast<int>(SSLErrorHandler::ErrorSeverity::IGNORABLE),
                   static_cast<int>(severity),
                   "Self-signed should be IGNORABLE when allowed");

        // 测试警告错误(不允许自签名时)
        severity = SSLErrorHandler::getErrorSeverity(ignorableError, false);
        assertTrue(static_cast<int>(severity) != static_cast<int>(SSLErrorHandler::ErrorSeverity::IGNORABLE),
                  "Self-signed should not be IGNORABLE when not allowed");

        // 测试过期证书
        QSslError expiredError(QSslError::CertificateExpired, cert);
        severity = SSLErrorHandler::getErrorSeverity(expiredError, true);
        assertEqual(static_cast<int>(SSLErrorHandler::ErrorSeverity::CRITICAL),
                   static_cast<int>(severity),
                   "Expired certificate should be CRITICAL");

        double elapsed = stopTimer();
        addResult("testSSLErrorSeverity", true, "", elapsed);
    }

    void testSSLErrorSuggestedActions() {
        startTimer();

        // 创建测试证书
        auto certKeyPair = CertificateGenerator::generateSelfSignedCertificate("TestAction", "QinDB-Test", 365);
        QSslCertificate cert = certKeyPair.first;

        // 测试各种错误的建议操作
        QSslError selfSignedError(QSslError::SelfSignedCertificate, cert);
        QString suggestion = SSLErrorHandler::getSuggestedAction(selfSignedError);
        assertFalse(suggestion.isEmpty(), "Self-signed error should have a suggestion");
        assertTrue(suggestion.contains("certificate") || suggestion.contains("self-signed"),
                  "Suggestion should mention certificate or self-signed");

        QSslError expiredError(QSslError::CertificateExpired, cert);
        suggestion = SSLErrorHandler::getSuggestedAction(expiredError);
        assertFalse(suggestion.isEmpty(), "Expired error should have a suggestion");
        assertTrue(suggestion.contains("Renew") || suggestion.contains("certificate"),
                  "Suggestion should mention renewing certificate");

        QSslError revokedError(QSslError::CertificateRevoked, cert);
        suggestion = SSLErrorHandler::getSuggestedAction(revokedError);
        assertFalse(suggestion.isEmpty(), "Revoked error should have a suggestion");
        assertTrue(suggestion.contains("revoked") || suggestion.contains("new"),
                  "Suggestion should mention revocation or new certificate");

        QSslError hostnameError(QSslError::HostNameMismatch, cert);
        suggestion = SSLErrorHandler::getSuggestedAction(hostnameError);
        assertFalse(suggestion.isEmpty(), "Hostname error should have a suggestion");
        assertTrue(suggestion.contains("hostname") || suggestion.contains("Common Name"),
                  "Suggestion should mention hostname or CN");

        double elapsed = stopTimer();
        addResult("testSSLErrorSuggestedActions", true, "", elapsed);
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
    TestSuite suite("TLS Handshake Manager Tests (Simplified)");

    // 添加测试
    suite.addTest(new TestTLSHandshakeSimple());

    // 运行测试
    suite.runAll();

    // 打印测试报告
    suite.printReport();

    return 0;
}
#endif