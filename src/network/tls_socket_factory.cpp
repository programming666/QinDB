#include "qindb/tls_socket_factory.h"
#include "qindb/logger.h"
#include "qindb/ssLError_handler.h"
#include <QSslError>

namespace qindb {

// ========== TLSSocketFactory ==========

TLSSocketFactory::TLSSocketFactory(const TLSConfig& config)
    : config_(config)
    , fingerprintManager_(nullptr)
{
    if (!config_.isValid()) {
        LOG_WARN("TLSSocketFactory created with invalid TLS configuration");
    }
}

TLSSocketFactory::~TLSSocketFactory() {
    // fingerprintManager_ 不由我们管理,不需要删除
}

QSslSocket* TLSSocketFactory::createServerSocket(QTcpSocket* rawSocket) {
    if (!rawSocket) {
        LOG_ERROR("Cannot create SSL socket from null raw socket");
        return nullptr;
    }

    if (!config_.isValid()) {
        LOG_ERROR("Cannot create SSL socket: invalid TLS configuration");
        return nullptr;
    }

    // 创建SSL socket
    QSslSocket* sslSocket = new QSslSocket();

    // 配置SSL
    QSslConfiguration sslConfig = config_.createSslConfiguration(true);
    sslSocket->setSslConfiguration(sslConfig);

    // 配置错误处理
    configureErrorHandling(sslSocket, true);

    // 转移socket descriptor
    qintptr socketDescriptor = rawSocket->socketDescriptor();
    if (!sslSocket->setSocketDescriptor(socketDescriptor)) {
        LOG_ERROR("Failed to set socket descriptor for SSL socket");
        delete sslSocket;
        return nullptr;
    }

    LOG_DEBUG("Created server SSL socket");
    return sslSocket;
}

QSslSocket* TLSSocketFactory::createClientSocket() {
    if (!config_.isValid()) {
        LOG_ERROR("Cannot create SSL socket: invalid TLS configuration");
        return nullptr;
    }

    // 创建SSL socket
    QSslSocket* sslSocket = new QSslSocket();

    // 配置SSL
    QSslConfiguration sslConfig = config_.createSslConfiguration(false);
    sslSocket->setSslConfiguration(sslConfig);

    // 配置错误处理
    configureErrorHandling(sslSocket, false);

    LOG_DEBUG("Created client SSL socket");
    return sslSocket;
}

void TLSSocketFactory::configureErrorHandling(QSslSocket* socket, bool isServer) {
    if (!socket) {
        return;
    }

    // 连接SSL错误信号
    QObject::connect(socket, &QSslSocket::sslErrors,
        [this, socket, isServer](const QList<QSslError>& errors) {
            // 记录所有错误
            for (const auto& error : errors) {
                auto severity = SSLErrorHandler::getErrorSeverity(
                    error, config_.allowSelfSigned());

                switch (severity) {
                    case SSLErrorHandler::ErrorSeverity::CRITICAL:
                        LOG_ERROR(QString("SSL error: %1 (treating as %2)")
                            .arg(error.errorString())
                            .arg(isServer ? "ignorable for server" : "critical"));
                        break;
                    case SSLErrorHandler::ErrorSeverity::WARNING:
                        LOG_WARN(QString("SSL warning: %1")
                            .arg(error.errorString()));
                        break;
                    case SSLErrorHandler::ErrorSeverity::IGNORABLE:
                        LOG_DEBUG(QString("Ignoring SSL error: %1")
                            .arg(error.errorString()));
                        break;
                }
            }

            // 对于服务器端，忽略所有SSL错误以允许自签名证书和其他验证问题
            // 对于客户端，仍然检查严重错误
            if (isServer) {
                LOG_INFO(QString("Server: Ignoring all %1 SSL error(s) to allow handshake")
                    .arg(errors.size()));
                socket->ignoreSslErrors();
            } else {
                // 客户端：过滤可忽略的错误
                auto criticalErrors = SSLErrorHandler::filterIgnorableErrors(
                    errors, config_.allowSelfSigned());

                if (criticalErrors.isEmpty()) {
                    LOG_INFO(QString("Client: Ignoring %1 SSL error(s)")
                        .arg(errors.size()));
                    socket->ignoreSslErrors();
                } else {
                    LOG_ERROR(QString("Client: Cannot ignore %1 critical SSL error(s)")
                        .arg(criticalErrors.size()));
                    // 不调用ignoreSslErrors(),让连接失败
                }
            }
        });

    // 对于客户端,如果使用指纹验证,连接加密完成信号
    if (!isServer && config_.verifyMode() == TLSVerifyMode::FINGERPRINT
        && fingerprintManager_) {
        QObject::connect(socket, &QSslSocket::encrypted,
            [this, socket]() {
                auto cert = socket->peerCertificate();
                QString host = socket->peerAddress().toString();
                uint16_t port = socket->peerPort();

                auto status = fingerprintManager_->verifyFingerprint(host, port, cert);

                switch (status) {
                    case FingerprintStatus::TRUSTED:
                        LOG_INFO(QString("Certificate fingerprint verified for %1:%2")
                            .arg(host).arg(port));
                        break;
                    case FingerprintStatus::UNKNOWN:
                        LOG_WARN(QString("Unknown certificate fingerprint for %1:%2")
                            .arg(host).arg(port));
                        // 用户拒绝或未设置回调,断开连接
                        socket->disconnectFromHost();
                        break;
                    case FingerprintStatus::MISMATCH:
                        LOG_ERROR(QString("Certificate fingerprint MISMATCH for %1:%2")
                            .arg(host).arg(port));
                        socket->disconnectFromHost();
                        break;
                    case FingerprintStatus::ERROR:
                        LOG_ERROR(QString("Error verifying fingerprint for %1:%2")
                            .arg(host).arg(port));
                        socket->disconnectFromHost();
                        break;
                }
            });
    }
}

void TLSSocketFactory::setFingerprintManager(FingerprintManager* manager) {
    fingerprintManager_ = manager;
}

} // namespace qindb
