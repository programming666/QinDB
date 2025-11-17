#include "qindb/client_manager.h"
#include "qindb/certificate_generator.h"
#include "qindb/logger.h"
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QSslConfiguration>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>

namespace qindb {

ClientManager::ClientManager(QObject* parent)
    : QObject(parent)
    , socket_(nullptr)
    , currentSessionId_(0)
    , isAuthenticated_(false)
    , heartbeatTimer_(nullptr)
    , heartbeatInterval_(30000)  // 30秒心跳间隔
    , lastActivityTime_(0)
    , maxRetries_(3)  // 最大重试3次
    , currentRetryCount_(0)
    , retryTimer_(nullptr)
    , fingerprintManager_(std::make_unique<FingerprintManager>()) {
}

ClientManager::~ClientManager() {
    disconnectFromServer();
    delete heartbeatTimer_;
    delete retryTimer_;
}

bool ClientManager::connectToServer(const ConnectionParams& params) {
    // 如果已经连接，先断开
    if (socket_ && socket_->state() == QAbstractSocket::ConnectedState) {
        disconnectFromServer();
    }

    connectionParams_ = params;

    // 根据是否启用SSL创建相应的套接字
    if (params.sslEnabled) {
        LOG_INFO("Creating SSL socket for secure connection");
        QSslSocket* sslSocket = new QSslSocket(this);

        // Configure SSL socket with more robust settings
        sslSocket->setPeerVerifyMode(QSslSocket::VerifyNone);  // We use fingerprint verification instead
        sslSocket->setProtocol(QSsl::TlsV1_2OrLater);
        
        // 使用默认SSL配置但允许更灵活的握手
        QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
        sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
        sslSocket->setSslConfiguration(sslConfig);
        
        // Connect SSL-specific signals
        connect(sslSocket, &QSslSocket::encrypted, this, &ClientManager::onEncrypted);
        connect(sslSocket, &QSslSocket::sslErrors, this, &ClientManager::onSslErrors);

        socket_ = sslSocket;
    } else {
        LOG_INFO("Creating standard TCP socket");
        socket_ = new QTcpSocket(this);
    }

    // 连接通用信号槽
    connect(socket_, &QTcpSocket::connected, this, &ClientManager::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &ClientManager::onDisconnected);
    connect(socket_, &QTcpSocket::readyRead, this, &ClientManager::onReadyRead);
    connect(socket_, &QTcpSocket::errorOccurred, this, &ClientManager::onError);

    // 更新连接状态
    updateConnectionStatus(QString("正在连接到 %1:%2%3...")
                              .arg(params.host)
                              .arg(params.port)
                              .arg(params.sslEnabled ? " (TLS)" : ""));

    // 连接到服务器
    if (params.sslEnabled) {
        QSslSocket* sslSocket = qobject_cast<QSslSocket*>(socket_);
        sslSocket->connectToHostEncrypted(params.host, params.port);
    } else {
        socket_->connectToHost(params.host, params.port);
    }

    // 等待连接建立（增加到30秒超时）
    if (!socket_->waitForConnected(30000)) {
        QString errorMsg = socket_->errorString();
        updateConnectionStatus(QString("连接失败: %1").arg(errorMsg));
        
        // 尝试重连
        if (currentRetryCount_ < maxRetries_) {
            currentRetryCount_++;
            updateConnectionStatus(QString("连接失败，正在重试 (%1/%2): %3")
                                 .arg(currentRetryCount_).arg(maxRetries_).arg(errorMsg));
            attemptReconnection();
            return false;  // 返回false但不发送error信号，因为正在重试
        } else {
            emit error(errorMsg);
            return false;
        }
    }

    // 如果是SSL连接，等待加密建立（增加到60秒超时）
    if (params.sslEnabled) {
        QSslSocket* sslSocket = qobject_cast<QSslSocket*>(socket_);
        if (!sslSocket->waitForEncrypted(60000)) {  // 增加到60秒
            QString errorMsg = QString("TLS握手失败: %1").arg(sslSocket->errorString());
            updateConnectionStatus(errorMsg);
            
            // TLS握手失败也尝试重连
            if (currentRetryCount_ < maxRetries_) {
                currentRetryCount_++;
                updateConnectionStatus(QString("TLS握手失败，正在重试 (%1/%2): %3")
                                     .arg(currentRetryCount_).arg(maxRetries_).arg(errorMsg));
                attemptReconnection();
                return false;
            } else {
                emit sslError(errorMsg);
                return false;
            }
        }
    }

    // 连接成功，重置重试计数器
    resetRetryCounter();

    // 初始化心跳定时器
    if (!heartbeatTimer_) {
        heartbeatTimer_ = new QTimer(this);
        connect(heartbeatTimer_, &QTimer::timeout, this, &ClientManager::onHeartbeatTimeout);
        heartbeatTimer_->start(heartbeatInterval_);
    }

    // 记录最后活动时间
    lastActivityTime_ = QDateTime::currentMSecsSinceEpoch();

    return true;
}

void ClientManager::disconnectFromServer() {
    if (socket_) {
        socket_->disconnectFromHost();
        socket_->deleteLater();
        socket_ = nullptr;
    }

    if (heartbeatTimer_) {
        heartbeatTimer_->stop();
    }

    if (retryTimer_) {
        retryTimer_->stop();
    }

    isAuthenticated_ = false;
    currentSessionId_ = 0;
    receiveBuffer_.clear();

    updateConnectionStatus("已断开连接");
    emit disconnected();
}

bool ClientManager::isConnected() const {
    return socket_ && socket_->state() == QAbstractSocket::ConnectedState;
}

bool ClientManager::isAuthenticated() const {
    return isAuthenticated_;
}

bool ClientManager::sendQuery(const QString& sql) {
    if (!isConnected() || !isAuthenticated_) {
        emit error("未连接或未认证，无法发送查询");
        return false;
    }

    // 创建查询请求
    QueryRequest request;
    request.sessionId = currentSessionId_;
    request.sql = sql;

    // 编码并发送
    QByteArray data = MessageCodec::encodeQueryRequest(request);
    qint64 written = socket_->write(data);

    if (written != data.size()) {
        emit error("发送查询失败");
        return false;
    }

    socket_->flush();
    lastActivityTime_ = QDateTime::currentMSecsSinceEpoch();

    return true;
}

bool ClientManager::sendDatabaseSwitch(const QString& databaseName) {
    if (!isConnected() || !isAuthenticated_) {
        emit error("未连接或未认证，无法发送数据库切换消息");
        return false;
    }

    // 创建数据库切换消息
    DatabaseSwitchMessage message;
    message.databaseName = databaseName;

    // 编码并发送
    QByteArray data = MessageCodec::encodeDatabaseSwitch(message);
    qint64 written = socket_->write(data);

    if (written != data.size()) {
        emit error("发送数据库切换消息失败");
        return false;
    }

    socket_->flush();
    lastActivityTime_ = QDateTime::currentMSecsSinceEpoch();

    LOG_INFO(QString("Sent database switch to: %1").arg(databaseName));
    return true;
}

QString ClientManager::getConnectionInfo() const {
    if (!isConnected()) {
        return "未连接";
    }

    return QString("连接到 %1:%2 (会话: %3)")
        .arg(connectionParams_.host)
        .arg(connectionParams_.port)
        .arg(currentSessionId_);
}

void ClientManager::onConnected() {
    updateConnectionStatus("连接成功，正在认证...");
    emit connected();

    // 连接成功后立即发送认证请求
    QTimer::singleShot(100, this, &ClientManager::sendAuthRequest);
}

void ClientManager::onDisconnected() {
    disconnectFromServer();
    emit disconnected();
}

void ClientManager::onReadyRead() {
    if (!socket_) {
        return;
    }

    // 读取所有可用数据
    receiveBuffer_.append(socket_->readAll());
    lastActivityTime_ = QDateTime::currentMSecsSinceEpoch();

    // 处理接收缓冲区中的消息
    while (receiveBuffer_.size() >= 5) {  // 至少需要 4字节长度 + 1字节类型
        // 解析消息长度
        QDataStream lengthStream(receiveBuffer_);
        lengthStream.setByteOrder(QDataStream::BigEndian);

        uint32_t messageLength;
        lengthStream >> messageLength;

        // 检查消息是否完整
        int totalSize = 4 + messageLength;
        if (receiveBuffer_.size() < totalSize) {
            break;  // 等待更多数据
        }

        // 提取完整消息
        QByteArray message = receiveBuffer_.left(totalSize);
        receiveBuffer_.remove(0, totalSize);

        // 处理消息
        handleMessage(message);
    }
}

void ClientManager::onError(QAbstractSocket::SocketError /* socketError */) {
    QString errorMsg = socket_->errorString();
    updateConnectionStatus(QString("连接错误: %1").arg(errorMsg));
    emit error(errorMsg);

    // 自动重连逻辑可以在这里实现
    LOG_ERROR(QString("Client connection error: %1").arg(errorMsg));
}

void ClientManager::onHeartbeatTimeout() {
    if (!isConnected()) {
        return;
    }

    // 检查是否长时间没有活动
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    if (currentTime - lastActivityTime_ > heartbeatInterval_ * 3) {
        // 超过3个心跳周期没有活动，发送心跳包
        sendHeartbeat();
    }
}

void ClientManager::handleMessage(const QByteArray& data) {
    MessageType type;
    QByteArray payload;

    if (!MessageCodec::decodeMessage(data, type, payload)) {
        emit error("消息格式错误");
        return;
    }

    switch (type) {
    case MessageType::AUTH_RESPONSE: {
        auto authResponse = MessageCodec::decodeAuthResponse(payload);
        if (authResponse.has_value()) {
            handleAuthResponse(authResponse.value());
        } else {
            emit error("认证响应解析失败");
        }
        break;
    }

    case MessageType::QUERY_RESPONSE: {
        auto queryResponse = MessageCodec::decodeQueryResponse(payload);
        if (queryResponse.has_value()) {
            handleQueryResponse(queryResponse.value());
        } else {
            emit error("查询响应解析失败");
        }
        break;
    }

    case MessageType::ERROR_RESPONSE: {
        auto errorResponse = MessageCodec::decodeErrorResponse(payload);
        if (errorResponse.has_value()) {
            handleErrorResponse(errorResponse.value());
        } else {
            emit error("错误响应解析失败");
        }
        break;
    }

    case MessageType::PONG:
        // 收到心跳响应
        lastActivityTime_ = QDateTime::currentMSecsSinceEpoch();
        break;

    default:
        emit error(QString("收到未知消息类型: %1").arg(static_cast<int>(type)));
        break;
    }
}

void ClientManager::handleAuthResponse(const AuthResponse& response) {
    switch (response.status) {
    case AuthStatus::SUCCESS:
        isAuthenticated_ = true;
        currentSessionId_ = response.sessionId;
        updateConnectionStatus(QString("认证成功 (会话: %1)").arg(currentSessionId_));
        emit authenticated();
        break;

    case AuthStatus::AUTH_FAILED:
        isAuthenticated_ = false;
        updateConnectionStatus("认证失败: " + response.message);
        emit authenticationFailed(response.message);
        break;

    case AuthStatus::DATABASE_NOT_FOUND:
        isAuthenticated_ = false;
        updateConnectionStatus("数据库不存在: " + response.message);
        emit authenticationFailed(response.message);
        break;

    case AuthStatus::PERMISSION_DENIED:
        isAuthenticated_ = false;
        updateConnectionStatus("权限被拒绝: " + response.message);
        emit authenticationFailed(response.message);
        break;
    }
}

void ClientManager::handleQueryResponse(const QueryResponse& response) {
    emit queryResponse(response);
}

void ClientManager::handleErrorResponse(const ErrorResponse& errorResponse) {
    emit error(QString("服务器错误 [%1]: %2").arg(errorResponse.errorCode).arg(errorResponse.message));
}

void ClientManager::sendAuthRequest() {
    if (!isConnected()) {
        return;
    }

    // 创建认证请求
    AuthRequest request;
    request.protocolVersion = PROTOCOL_VERSION;
    request.username = connectionParams_.username;
    request.password = connectionParams_.password;
    request.database = "qindb";  // 默认连接到系统数据库

    // 编码并发送
    QByteArray data = MessageCodec::encodeAuthRequest(request);
    qint64 written = socket_->write(data);

    if (written != data.size()) {
        emit error("发送认证请求失败");
        return;
    }

    socket_->flush();
    lastActivityTime_ = QDateTime::currentMSecsSinceEpoch();
}

void ClientManager::sendHeartbeat() {
    if (!isConnected()) {
        return;
    }

    // 发送PING消息
    QByteArray ping = MessageCodec::encodeMessage(MessageType::PING, QByteArray());
    qint64 written = socket_->write(ping);

    if (written != ping.size()) {
        LOG_WARN("Failed to send heartbeat");
    } else {
        lastActivityTime_ = QDateTime::currentMSecsSinceEpoch();
    }
}

void ClientManager::updateConnectionStatus(const QString& status) {
    emit connectionStatusChanged(status);
    LOG_INFO(QString("Client status: %1").arg(status));
}

void ClientManager::setFingerprintConfirmationCallback(FingerprintManager::ConfirmationCallback callback) {
    if (fingerprintManager_) {
        fingerprintManager_->setConfirmationCallback(callback);
    }
}

void ClientManager::onEncrypted() {
    QSslSocket* sslSocket = qobject_cast<QSslSocket*>(socket_);
    if (!sslSocket) {
        return;
    }

    LOG_INFO("TLS handshake completed successfully");

    // Verify server certificate fingerprint
    QSslCertificate peerCert = sslSocket->peerCertificate();
    if (peerCert.isNull()) {
        QString errorMsg = "Server did not provide a certificate";
        LOG_ERROR(errorMsg);
        emit sslError(errorMsg);
        disconnectFromServer();
        return;
    }

    // Verify fingerprint using FingerprintManager
    FingerprintStatus status = fingerprintManager_->verifyFingerprint(
        connectionParams_.host,
        connectionParams_.port,
        peerCert
    );

    switch (status) {
    case FingerprintStatus::TRUSTED:
        LOG_INFO("Server certificate fingerprint verified and trusted");
        updateConnectionStatus(QString("TLS连接已建立并验证"));
        // Continue with normal connection flow
        break;

    case FingerprintStatus::MISMATCH:
        {
            QString errorMsg = QString("警告: 服务器指纹不匹配！可能遭到中间人攻击！\n"
                                     "服务器: %1:%2")
                                .arg(connectionParams_.host)
                                .arg(connectionParams_.port);
            LOG_ERROR(errorMsg);
            emit sslError(errorMsg);
            disconnectFromServer();
        }
        break;

    case FingerprintStatus::UNKNOWN:
        {
            QString errorMsg = QString("服务器指纹未知，连接被拒绝\n"
                                     "服务器: %1:%2")
                                .arg(connectionParams_.host)
                                .arg(connectionParams_.port);
            LOG_WARN(errorMsg);
            emit sslError(errorMsg);
            disconnectFromServer();
        }
        break;

    case FingerprintStatus::ERROR:
        {
            QString errorMsg = "指纹验证过程中发生错误";
            LOG_ERROR(errorMsg);
            emit sslError(errorMsg);
            disconnectFromServer();
        }
        break;
    }
}

void ClientManager::onSslErrors(const QList<QSslError>& errors) {
    // Log all SSL errors with more detail
    for (const QSslError& error : errors) {
        LOG_WARN(QString("SSL error [%1]: %2").arg(static_cast<int>(error.error())).arg(error.errorString()));
    }

    // We ignore Qt's built-in SSL verification errors because we use fingerprint verification
    // However, we still want to be aware of critical errors
    QSslSocket* sslSocket = qobject_cast<QSslSocket*>(socket_);
    if (sslSocket) {
        // Ignore expected errors related to self-signed certificates
        bool hasCriticalError = false;
        QStringList criticalErrorMessages;
        
        for (const QSslError& error : errors) {
            // 检查是否为可忽略的错误
            if (error.error() != QSslError::SelfSignedCertificate &&
                error.error() != QSslError::SelfSignedCertificateInChain &&
                error.error() != QSslError::CertificateUntrusted &&
                error.error() != QSslError::HostNameMismatch &&
                error.error() != QSslError::UnableToGetLocalIssuerCertificate &&
                error.error() != QSslError::UnableToVerifyFirstCertificate) {
                hasCriticalError = true;
                criticalErrorMessages.append(error.errorString());
            }
        }

        if (!hasCriticalError) {
            // Ignore non-critical errors (we use fingerprint verification)
            LOG_INFO(QString("Ignoring %1 non-critical SSL errors for fingerprint-based verification").arg(errors.size()));
            sslSocket->ignoreSslErrors(errors);
        } else {
            QString errorMsg = QString("严重的TLS错误: %1").arg(criticalErrorMessages.join("; "));
            LOG_ERROR(errorMsg);
            emit sslError(errorMsg);
        }
    }
}

void ClientManager::attemptReconnection() {
    // 清理当前连接
    if (socket_) {
        socket_->deleteLater();
        socket_ = nullptr;
    }

    // 设置重试定时器（延迟2秒后重试）
    if (!retryTimer_) {
        retryTimer_ = new QTimer(this);
        connect(retryTimer_, &QTimer::timeout, this, &ClientManager::onRetryConnection);
        retryTimer_->setSingleShot(true);
    }

    retryTimer_->start(2000);  // 2秒后重试
}

void ClientManager::onRetryConnection() {
    LOG_INFO(QString("Attempting reconnection (retry %1/%2)").arg(currentRetryCount_).arg(maxRetries_));
    
    // 重新尝试连接，但不重置重试计数器
    ConnectionParams params = connectionParams_;
    
    // 根据是否启用SSL创建相应的套接字
    if (params.sslEnabled) {
        LOG_INFO("Creating SSL socket for secure connection (retry)");
        QSslSocket* sslSocket = new QSslSocket(this);

        // Configure SSL socket with more robust settings
        sslSocket->setPeerVerifyMode(QSslSocket::VerifyNone);
        sslSocket->setProtocol(QSsl::TlsV1_2OrLater);
        
        // 使用默认SSL配置但允许更灵活的握手
        QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
        sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
        sslSocket->setSslConfiguration(sslConfig);
        
        // Connect SSL-specific signals
        connect(sslSocket, &QSslSocket::encrypted, this, &ClientManager::onEncrypted);
        connect(sslSocket, &QSslSocket::sslErrors, this, &ClientManager::onSslErrors);

        socket_ = sslSocket;
    } else {
        LOG_INFO("Creating standard TCP socket (retry)");
        socket_ = new QTcpSocket(this);
    }

    // 连接通用信号槽
    connect(socket_, &QTcpSocket::connected, this, &ClientManager::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &ClientManager::onDisconnected);
    connect(socket_, &QTcpSocket::readyRead, this, &ClientManager::onReadyRead);
    connect(socket_, &QTcpSocket::errorOccurred, this, &ClientManager::onError);

    // 连接到服务器
    if (params.sslEnabled) {
        QSslSocket* sslSocket = qobject_cast<QSslSocket*>(socket_);
        sslSocket->connectToHostEncrypted(params.host, params.port);
    } else {
        socket_->connectToHost(params.host, params.port);
    }

    // 等待连接建立（增加到30秒超时）
    if (!socket_->waitForConnected(30000)) {
        QString errorMsg = socket_->errorString();
        
        // 如果还有重试机会，继续重试
        if (currentRetryCount_ < maxRetries_) {
            currentRetryCount_++;
            updateConnectionStatus(QString("重试连接失败，继续重试 (%1/%2): %3")
                                 .arg(currentRetryCount_).arg(maxRetries_).arg(errorMsg));
            attemptReconnection();
            return;
        } else {
            updateConnectionStatus(QString("连接失败，已达到最大重试次数: %1").arg(errorMsg));
            emit error(errorMsg);
            return;
        }
    }

    // 如果是SSL连接，等待加密建立
    if (params.sslEnabled) {
        QSslSocket* sslSocket = qobject_cast<QSslSocket*>(socket_);
        if (!sslSocket->waitForEncrypted(60000)) {
            QString errorMsg = QString("TLS握手失败: %1").arg(sslSocket->errorString());
            
            // 如果还有重试机会，继续重试
            if (currentRetryCount_ < maxRetries_) {
                currentRetryCount_++;
                updateConnectionStatus(QString("TLS握手重试失败，继续重试 (%1/%2): %3")
                                     .arg(currentRetryCount_).arg(maxRetries_).arg(errorMsg));
                attemptReconnection();
                return;
            } else {
                updateConnectionStatus(QString("TLS握手失败，已达到最大重试次数: %1").arg(errorMsg));
                emit sslError(errorMsg);
                return;
            }
        }
    }

    // 连接成功，重置重试计数器
    resetRetryCounter();
    
    // 初始化心跳定时器
    if (!heartbeatTimer_) {
        heartbeatTimer_ = new QTimer(this);
        connect(heartbeatTimer_, &QTimer::timeout, this, &ClientManager::onHeartbeatTimeout);
        heartbeatTimer_->start(heartbeatInterval_);
    }

    // 记录最后活动时间
    lastActivityTime_ = QDateTime::currentMSecsSinceEpoch();
}

void ClientManager::resetRetryCounter() {
    currentRetryCount_ = 0;
    LOG_INFO("Retry counter reset - connection successful");
}

} // namespace qindb
