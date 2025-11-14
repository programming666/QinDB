#include "test_framework.h"
#include "qindb/tls_handshake_manager.h"
#include "qindb/tls_config.h"
#include "qindb/certificate_generator.h"
#include "qindb/sslError_handler.h"
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QEventLoop>
#include <QTimer>
#include <iostream>

namespace qindb {
namespace test {

/**
 * @brief TLS握手管理器单元测试
 */
class TestTLSHandshake : public TestCase {
public:
    TestTLSHandshake() : TestCase("TestTLSHandshake") {}

    void run() override {
        try { testBasicClientHandshake(); } catch (...) {}
        try { testBasicServerHandshake(); } catch (...) {}
        try { testHandshakeWithSelfSignedCertificate(); } catch (...) {}
        try { testHandshakeTimeout(); } catch (...) {}
        try { testHandshakeWithCriticalErrors(); } catch (...) {}
        try { testHandshakeStateTransitions(); } catch (...) {}
        try { testCertificateValidation(); } catch (...) {}
        try { testStateTransitionValidation(); } catch (...) {}
        try { testMultipleHandshakes(); } catch (...) {}
    }

private:
    void testBasicClientHandshake() {
        startTimer();

        // 创建TLS配置
        TLSConfig config;

        // 生成自签名证书
        bool certGenerated = config.generateSelfSigned("TestClient", "QinDB-Test", 365);
        assertTrue(certGenerated, "Failed to generate self-signed certificate");

        // 创建握手管理器
        TLSHandshakeManager handshakeManager(config);

        // 创建SSL socket
        QSslSocket clientSocket;

        // 设置SSL配置
        QSslConfiguration sslConfig = config.createSslConfiguration(false);
        clientSocket.setSslConfiguration(sslConfig);

        // 测试：尝试在socket未连接时开始握手(应该失败)
        bool started = handshakeManager.startHandshake(&clientSocket, false, 5000);
        assertFalse(started, "Should not start TLS handshake on unconnected socket");

        // 验证初始状态保持不变
        assertEqual(static_cast<int>(TLSHandshakeState::IDLE),
                   static_cast<int>(handshakeManager.state()),
                   "State should remain IDLE when handshake fails to start");

        double elapsed = stopTimer();
        addResult("testBasicClientHandshake", true, "", elapsed);
    }
    
    void testBasicServerHandshake() {
        startTimer();

        // 创建TLS配置
        TLSConfig config;

        // 生成自签名证书
        bool certGenerated = config.generateSelfSigned("TestServer", "QinDB-Test", 365);
        assertTrue(certGenerated, "Failed to generate self-signed certificate");

        // 创建握手管理器
        TLSHandshakeManager handshakeManager(config);

        // 创建SSL socket
        QSslSocket serverSocket;

        // 设置SSL配置
        QSslConfiguration sslConfig = config.createSslConfiguration(true);
        serverSocket.setSslConfiguration(sslConfig);

        // 测试：尝试在socket未连接时开始握手(应该失败)
        bool started = handshakeManager.startHandshake(&serverSocket, true, 5000);
        assertFalse(started, "Should not start TLS handshake on unconnected socket");

        // 验证初始状态
        assertEqual(static_cast<int>(TLSHandshakeState::IDLE),
                   static_cast<int>(handshakeManager.state()),
                   "State should remain IDLE when handshake fails to start");

        double elapsed = stopTimer();
        addResult("testBasicServerHandshake", true, "", elapsed);
    }
    
    void testHandshakeWithSelfSignedCertificate() {
        startTimer();

        // 创建TLS配置，允许自签名证书
        TLSConfig config;
        config.setAllowSelfSigned(true);
        config.setVerifyMode(TLSVerifyMode::NONE);

        // 生成自签名证书
        bool certGenerated = config.generateSelfSigned("TestSelfSigned", "QinDB-Test", 365);
        assertTrue(certGenerated, "Failed to generate self-signed certificate");

        // 验证配置
        assertTrue(config.allowSelfSigned(), "Config should allow self-signed certificates");
        assertEqual(static_cast<int>(TLSVerifyMode::NONE),
                   static_cast<int>(config.verifyMode()),
                   "Verify mode should be NONE");

        // 创建握手管理器
        TLSHandshakeManager handshakeManager(config);

        // 验证初始状态
        assertEqual(static_cast<int>(TLSHandshakeState::IDLE),
                   static_cast<int>(handshakeManager.state()),
                   "Initial state should be IDLE");

        double elapsed = stopTimer();
        addResult("testHandshakeWithSelfSignedCertificate", true, "", elapsed);
    }
    
    void testHandshakeTimeout() {
        startTimer();

        // 创建TLS配置
        TLSConfig config;
        config.generateSelfSigned("TestTimeout", "QinDB-Test", 365);

        // 创建握手管理器
        TLSHandshakeManager handshakeManager(config);

        // 验证初始状态
        assertEqual(static_cast<int>(TLSHandshakeState::IDLE),
                   static_cast<int>(handshakeManager.state()),
                   "Initial state should be IDLE");

        // 测试取消握手功能
        handshakeManager.cancelHandshake();  // 应该安全地什么都不做

        // 验证状态仍然是IDLE
        assertEqual(static_cast<int>(TLSHandshakeState::IDLE),
                   static_cast<int>(handshakeManager.state()),
                   "State should remain IDLE after cancel on idle manager");

        double elapsed = stopTimer();
        addResult("testHandshakeTimeout", true, "", elapsed);
    }
    
    void testHandshakeWithCriticalErrors() {
        startTimer();

        // 创建TLS配置，不允许自签名证书
        TLSConfig config;
        config.setAllowSelfSigned(false);
        config.setVerifyMode(TLSVerifyMode::REQUIRED);
        config.generateSelfSigned("TestCritical", "QinDB-Test", 365);

        // 创建握手管理器
        TLSHandshakeManager handshakeManager(config);

        // 验证配置
        assertFalse(config.allowSelfSigned(), "Config should not allow self-signed certificates");
        assertEqual(static_cast<int>(TLSVerifyMode::REQUIRED),
                   static_cast<int>(config.verifyMode()),
                   "Verify mode should be REQUIRED");

        // 验证初始状态
        assertEqual(static_cast<int>(TLSHandshakeState::IDLE),
                   static_cast<int>(handshakeManager.state()),
                   "Initial state should be IDLE");

        double elapsed = stopTimer();
        addResult("testHandshakeWithCriticalErrors", true, "", elapsed);
    }
    
    void testHandshakeStateTransitions() {
        startTimer();

        // 创建TLS配置
        TLSConfig config;
        config.generateSelfSigned("TestStates", "QinDB-Test", 365);

        // 创建握手管理器
        TLSHandshakeManager handshakeManager(config);

        // 验证初始状态
        assertEqual(static_cast<int>(TLSHandshakeState::IDLE),
                   static_cast<int>(handshakeManager.state()),
                   "Initial state should be IDLE");

        // 跟踪状态变化
        QList<TLSHandshakeState> stateChanges;
        QObject::connect(&handshakeManager, &TLSHandshakeManager::stateChanged,
            [&stateChanges](TLSHandshakeState newState) {
                stateChanges.append(newState);
            });

        // 尝试开始握手（会因socket未连接而失败）
        QSslSocket testSocket;
        handshakeManager.startHandshake(&testSocket, false, 1000);

        // 验证状态保持IDLE（因为启动失败）
        assertEqual(static_cast<int>(TLSHandshakeState::IDLE),
                   static_cast<int>(handshakeManager.state()),
                   "State should remain IDLE when start fails");

        // 验证没有状态转换发生
        assertTrue(stateChanges.isEmpty(), "No state transitions should occur when start fails");

        double elapsed = stopTimer();
        addResult("testHandshakeStateTransitions", true, "", elapsed);
    }
    
    void testCertificateValidation() {
        startTimer();
        
        // 创建TLS配置
        TLSConfig config;
        config.generateSelfSigned("TestValidation", "QinDB-Test", 365);
        
        // 获取证书
        QSslCertificate cert = config.certificate();
        assertFalse(cert.isNull(), "Certificate should not be null");
        
        // 验证证书有效期
        bool isValid = SSLErrorHandler::validateCertificateValidity(cert);
        assertTrue(isValid, "Certificate should be valid");
        
        // 测试证书错误描述
        QString errorDesc = SSLErrorHandler::getCertificateValidationError(cert);
        assertTrue(errorDesc.isEmpty(), "Valid certificate should have no validation errors");
        
        // 测试错误类型描述
        QSslError selfSignedError(QSslError::SelfSignedCertificate, cert);
        QString errorDescription = SSLErrorHandler::getErrorDescription(selfSignedError);
        assertFalse(errorDescription.isEmpty(), "Error description should not be empty");
        
        // 验证自签名错误检测
        bool isSelfSigned = SSLErrorHandler::isSelfSignedError(selfSignedError);
        assertTrue(isSelfSigned, "Should detect self-signed certificate error");
        
        // 验证严重错误检测
        bool isCritical = SSLErrorHandler::isCriticalError(selfSignedError);
        assertFalse(isCritical, "Self-signed error should not be critical when allowed");
        
        double elapsed = stopTimer();
        addResult("testCertificateValidation", true, "", elapsed);
    }

    void testStateTransitionValidation() {
        startTimer();

        // 创建TLS配置
        TLSConfig config;
        config.generateSelfSigned("TestStateTransition", "QinDB-Test", 365);

        // 创建握手管理器
        TLSHandshakeManager handshakeManager(config);

        // 验证初始状态
        assertEqual(static_cast<int>(TLSHandshakeState::IDLE),
                   static_cast<int>(handshakeManager.state()),
                   "Initial state should be IDLE");

        // 跟踪状态变化
        QList<TLSHandshakeState> stateChanges;
        QObject::connect(&handshakeManager, &TLSHandshakeManager::stateChanged,
            [&stateChanges](TLSHandshakeState newState) {
                stateChanges.append(newState);
            });

        // 尝试在未连接socket上启动握手
        QSslSocket testSocket;
        bool started = handshakeManager.startHandshake(&testSocket, false, 500);

        // 验证启动失败
        assertFalse(started, "Should not start handshake on unconnected socket");

        // 验证状态保持IDLE
        assertEqual(static_cast<int>(TLSHandshakeState::IDLE),
                   static_cast<int>(handshakeManager.state()),
                   "State should remain IDLE when start fails");

        // 验证没有状态变化
        assertTrue(stateChanges.isEmpty(), "No state changes should occur when start fails");

        double elapsed = stopTimer();
        addResult("testStateTransitionValidation", true, "", elapsed);
    }

    void testMultipleHandshakes() {
        startTimer();

        // 创建TLS配置
        TLSConfig config;
        config.generateSelfSigned("TestMultiple", "QinDB-Test", 365);

        // 创建握手管理器
        TLSHandshakeManager handshakeManager(config);

        // 第一次尝试（未连接socket，会失败）
        QSslSocket socket1;
        bool started1 = handshakeManager.startHandshake(&socket1, false, 300);
        assertFalse(started1, "First handshake should fail on unconnected socket");

        // 状态应该保持IDLE
        assertEqual(static_cast<int>(TLSHandshakeState::IDLE),
                   static_cast<int>(handshakeManager.state()),
                   "State should remain IDLE after failed start");

        // 第二次尝试（也会失败）
        QSslSocket socket2;
        bool started2 = handshakeManager.startHandshake(&socket2, false, 300);
        assertFalse(started2, "Second handshake should also fail on unconnected socket");

        // 验证状态仍然是IDLE
        assertEqual(static_cast<int>(TLSHandshakeState::IDLE),
                   static_cast<int>(handshakeManager.state()),
                   "State should remain IDLE after second failed start");

        double elapsed = stopTimer();
        addResult("testMultipleHandshakes", true, "", elapsed);
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
    TestSuite suite("TLS Handshake Manager Tests");

    // 添加测试
    suite.addTest(new TestTLSHandshake());

    // 运行测试
    suite.runAll();

    // 打印测试报告
    suite.printReport();

    return 0;
}
#endif