#include "qindb/tls_handshake_manager.h"
#include "qindb/sslError_handler.h"

namespace qindb {

TLSHandshakeManager::TLSHandshakeManager(const TLSConfig& config, QObject* parent)
    : QObject(parent)
    , config_(config)
    , currentSocket_(nullptr)
    , state_(TLSHandshakeState::IDLE)
    , isServerMode_(false)
    , isActive_(false) {
    
    // 创建超时定时器
    timeoutTimer_ = std::make_unique<QTimer>(this);
    timeoutTimer_->setSingleShot(true);
    timeoutTimer_->setInterval(30000); // 默认30秒
    
    connect(timeoutTimer_.get(), &QTimer::timeout, this, &TLSHandshakeManager::onHandshakeTimeout);
}

TLSHandshakeManager::~TLSHandshakeManager() {
    cancelHandshake();
}

bool TLSHandshakeManager::startHandshake(QSslSocket* socket, bool isServer, int timeoutMs) {
    if (!socket) {
        LOG_ERROR("TLSHandshake: Cannot start - socket is null");
        return false;
    }

    if (socket->state() != QAbstractSocket::ConnectedState) {
        LOG_ERROR(QString("TLSHandshake: Cannot start - socket not connected (state: %1)")
            .arg(static_cast<int>(socket->state())));
        return false;
    }

    if (isActive_) {
        LOG_WARN("TLSHandshake: Already in progress, cancelling previous handshake");
        cancelHandshake();
    }

    // 设置当前状态
    currentSocket_ = socket;
    isServerMode_ = isServer;
    isActive_ = true;

    // 重置结果
    result_ = TLSHandshakeResult();

    LOG_INFO(QString("TLSHandshake: Starting %1 handshake (timeout: %2ms, socket: %3)")
        .arg(isServer ? "server" : "client")
        .arg(timeoutMs)
        .arg(reinterpret_cast<quintptr>(socket), 0, 16));

    // 设置超时定时器
    timeoutTimer_->setInterval(timeoutMs);
    timeoutTimer_->start();

    // 连接信号(使用UniqueConnection避免重复连接)
    connect(socket, &QSslSocket::sslErrors, this, &TLSHandshakeManager::onSslErrors, Qt::UniqueConnection);
    connect(socket, &QSslSocket::encrypted, this, &TLSHandshakeManager::onEncrypted, Qt::UniqueConnection);
    connect(socket, &QSslSocket::stateChanged, this, &TLSHandshakeManager::onSocketStateChanged, Qt::UniqueConnection);

    // 设置初始状态
    setState(TLSHandshakeState::HANDSHAKE_STARTED);

    // 开始SSL握手
    if (isServer) {
        LOG_DEBUG("TLSHandshake: Starting server encryption");
        socket->startServerEncryption();
    } else {
        LOG_DEBUG("TLSHandshake: Starting client encryption");
        socket->startClientEncryption();
    }

    return true;
}

void TLSHandshakeManager::cancelHandshake() {
    if (!isActive_) {
        return;
    }

    LOG_INFO("TLSHandshake: Cancelling handshake");

    // 停止超时定时器
    timeoutTimer_->stop();

    // 断开信号连接
    disconnectSocketSignals();

    // 清理状态
    cleanup();
}

void TLSHandshakeManager::onSslErrors(const QList<QSslError>& errors) {
    if (!isActive_) {
        LOG_WARN("TLSHandshake: Received SSL errors but handshake is not active");
        return;
    }

    LOG_INFO(QString("TLSHandshake: Received %1 SSL error(s)").arg(errors.size()));

    // 处理SSL错误
    processSslErrors(errors);
}

void TLSHandshakeManager::onEncrypted() {
    if (!isActive_) {
        LOG_WARN("TLSHandshake: Encryption established but handshake is not active");
        return;
    }

    LOG_INFO("TLSHandshake: Encryption established successfully");

    // 停止超时定时器
    timeoutTimer_->stop();

    // 获取对等证书
    if (currentSocket_) {
        result_.peerCertificate = currentSocket_->peerCertificate();
        result_.success = true;

        if (!result_.peerCertificate.isNull()) {
            LOG_DEBUG(QString("TLSHandshake: Peer certificate received (CN: %1)")
                .arg(result_.peerCertificate.subjectInfo(QSslCertificate::CommonName).join(",")));

            // 验证证书（如果需要）
            bool certValid = validateCertificate(result_.peerCertificate);
            if (!certValid) {
                result_.success = false;
                result_.errorMessage = "Certificate validation failed";
                LOG_ERROR("TLSHandshake: Certificate validation failed");
            }
        } else {
            LOG_WARN("TLSHandshake: Peer certificate is null");
        }
    }

    // 设置完成状态
    if (result_.success) {
        setState(TLSHandshakeState::HANDSHAKE_COMPLETED);
        LOG_INFO("TLSHandshake: Handshake completed successfully");
    } else {
        setState(TLSHandshakeState::HANDSHAKE_FAILED);
        LOG_ERROR("TLSHandshake: Handshake failed");
    }

    // 发送完成信号
    emit handshakeCompleted(result_);

    // 清理
    cleanup();
}

void TLSHandshakeManager::onHandshakeTimeout() {
    if (!isActive_) {
        LOG_WARN("TLSHandshake: Timeout occurred but handshake is not active");
        return;
    }

    LOG_ERROR("TLSHandshake: Timeout occurred");

    // 设置超时状态
    setState(TLSHandshakeState::HANDSHAKE_TIMEOUT);

    // 设置结果
    result_.success = false;
    result_.errorMessage = "TLS handshake timeout";

    // 发送完成信号
    emit handshakeCompleted(result_);

    // 断开连接
    if (currentSocket_ && currentSocket_->state() == QAbstractSocket::ConnectedState) {
        LOG_DEBUG("TLSHandshake: Disconnecting socket due to timeout");
        currentSocket_->disconnectFromHost();
    }

    // 清理
    cleanup();
}

void TLSHandshakeManager::onSocketStateChanged(QAbstractSocket::SocketState socketState) {
    if (!isActive_) {
        return;
    }

    LOG_DEBUG(QString("TLSHandshake: Socket state changed to %1").arg(static_cast<int>(socketState)));

    switch (socketState) {
        case QAbstractSocket::ConnectedState:
            // 连接状态，继续等待握手完成
            LOG_DEBUG("TLSHandshake: Socket connected, waiting for handshake");
            break;

        case QAbstractSocket::UnconnectedState:
            // 连接断开，检查是否还在握手过程中
            if (state_ != TLSHandshakeState::HANDSHAKE_COMPLETED) {
                LOG_WARN("TLSHandshake: Socket disconnected during handshake");

                if (state_ != TLSHandshakeState::HANDSHAKE_FAILED &&
                    state_ != TLSHandshakeState::HANDSHAKE_TIMEOUT) {
                    setState(TLSHandshakeState::HANDSHAKE_FAILED);

                    result_.success = false;
                    result_.errorMessage = "Socket disconnected during handshake";

                    emit handshakeCompleted(result_);
                }

                cleanup();
            }
            break;

        case QAbstractSocket::ClosingState:
            LOG_DEBUG("TLSHandshake: Socket is closing");
            break;

        default:
            break;
    }
}

void TLSHandshakeManager::setState(TLSHandshakeState newState) {
    if (state_ == newState) {
        return;
    }

    // 验证状态转换是否合法
    if (!isValidStateTransition(state_, newState)) {
        LOG_WARN(QString("TLSHandshake: Invalid state transition %1 -> %2")
            .arg(static_cast<int>(state_))
            .arg(static_cast<int>(newState)));
        return;
    }

    LOG_DEBUG(QString("TLSHandshake: State changed: %1 -> %2")
        .arg(static_cast<int>(state_))
        .arg(static_cast<int>(newState)));

    state_ = newState;
    emit stateChanged(newState);
}

bool TLSHandshakeManager::isValidStateTransition(TLSHandshakeState from, TLSHandshakeState to) const {
    // 定义合法的状态转换
    switch (from) {
        case TLSHandshakeState::IDLE:
            return to == TLSHandshakeState::HANDSHAKE_STARTED;

        case TLSHandshakeState::HANDSHAKE_STARTED:
            return to == TLSHandshakeState::CERTIFICATE_RECEIVED ||
                   to == TLSHandshakeState::HANDSHAKE_COMPLETED ||
                   to == TLSHandshakeState::HANDSHAKE_FAILED ||
                   to == TLSHandshakeState::HANDSHAKE_TIMEOUT;

        case TLSHandshakeState::CERTIFICATE_RECEIVED:
            return to == TLSHandshakeState::HANDSHAKE_COMPLETED ||
                   to == TLSHandshakeState::HANDSHAKE_FAILED ||
                   to == TLSHandshakeState::HANDSHAKE_TIMEOUT;

        case TLSHandshakeState::HANDSHAKE_COMPLETED:
        case TLSHandshakeState::HANDSHAKE_FAILED:
        case TLSHandshakeState::HANDSHAKE_TIMEOUT:
            // 终态，不允许再转换
            return false;

        default:
            return false;
    }
}

void TLSHandshakeManager::processSslErrors(const QList<QSslError>& errors) {
    if (!isActive_) {
        LOG_WARN("TLSHandshake: Processing SSL errors but handshake is not active");
        return;
    }

    // 保存错误列表
    result_.errors = errors;

    // 使用SSLErrorHandler过滤错误
    auto criticalErrors = SSLErrorHandler::filterIgnorableErrors(errors, config_.allowSelfSigned());

    // 记录所有错误
    for (const auto& error : errors) {
        auto severity = SSLErrorHandler::getErrorSeverity(error, config_.allowSelfSigned());

        QString errorDesc = SSLErrorHandler::getErrorDescription(error);
        switch (severity) {
            case SSLErrorHandler::ErrorSeverity::CRITICAL:
                LOG_ERROR(QString("TLSHandshake: Critical SSL error - %1").arg(errorDesc));
                break;
            case SSLErrorHandler::ErrorSeverity::WARNING:
                LOG_WARN(QString("TLSHandshake: SSL warning - %1").arg(errorDesc));
                break;
            case SSLErrorHandler::ErrorSeverity::IGNORABLE:
                LOG_DEBUG(QString("TLSHandshake: Ignoring SSL error - %1").arg(errorDesc));
                break;
        }
    }

    // 如果没有严重错误，忽略所有错误继续握手
    if (criticalErrors.isEmpty()) {
        LOG_INFO(QString("TLSHandshake: Ignoring %1 non-critical SSL error(s)").arg(errors.size()));

        if (currentSocket_) {
            currentSocket_->ignoreSslErrors(errors);
        }
    } else {
        LOG_ERROR(QString("TLSHandshake: Cannot continue - %1 critical SSL error(s)").arg(criticalErrors.size()));

        // 停止超时定时器
        timeoutTimer_->stop();

        // 设置失败状态
        setState(TLSHandshakeState::HANDSHAKE_FAILED);

        // 设置结果
        result_.success = false;
        result_.errorMessage = QString("SSL errors occurred: %1 critical error(s)").arg(criticalErrors.size());

        // 发送完成信号
        emit handshakeCompleted(result_);

        // 清理
        cleanup();
    }
}

bool TLSHandshakeManager::validateCertificate(const QSslCertificate& cert) {
    if (cert.isNull()) {
        LOG_WARN("TLSHandshake: Peer certificate is null");
        return false;
    }

    // 根据验证模式进行证书验证
    switch (config_.verifyMode()) {
        case TLSVerifyMode::NONE:
            // 不验证证书
            LOG_DEBUG("TLSHandshake: Certificate validation disabled (NONE mode)");
            return true;

        case TLSVerifyMode::OPTIONAL:
            // 可选验证 - 记录警告但允许连接
            LOG_INFO("TLSHandshake: Certificate validation optional - accepting certificate");
            return true;

        case TLSVerifyMode::REQUIRED: {
            // 必须验证 - 检查证书有效性
            LOG_INFO("TLSHandshake: Certificate validation required");

            QString errorMessage;
            bool isValid = SSLErrorHandler::validateCertificateValidity(cert, &errorMessage);

            if (!isValid) {
                LOG_ERROR(QString("TLSHandshake: Certificate validation failed - %1").arg(errorMessage));
                return false;
            }

            LOG_DEBUG("TLSHandshake: Certificate is valid");
            return true;
        }

        case TLSVerifyMode::FINGERPRINT:
            // 指纹验证 - 将在后续步骤中处理
            LOG_INFO("TLSHandshake: Certificate fingerprint validation will be performed later");
            return true;

        default:
            LOG_WARN("TLSHandshake: Unknown TLS verify mode");
            return false;
    }
}

void TLSHandshakeManager::cleanup() {
    LOG_DEBUG("TLSHandshake: Cleaning up resources");

    // 断开信号连接
    disconnectSocketSignals();

    // 重置状态
    currentSocket_ = nullptr;
    isActive_ = false;
    isServerMode_ = false;

    // 停止定时器
    if (timeoutTimer_ && timeoutTimer_->isActive()) {
        timeoutTimer_->stop();
    }
}

void TLSHandshakeManager::disconnectSocketSignals() {
    if (currentSocket_) {
        LOG_DEBUG("TLSHandshake: Disconnecting socket signals");
        disconnect(currentSocket_, &QSslSocket::sslErrors, this, &TLSHandshakeManager::onSslErrors);
        disconnect(currentSocket_, &QSslSocket::encrypted, this, &TLSHandshakeManager::onEncrypted);
        disconnect(currentSocket_, &QSslSocket::stateChanged, this, &TLSHandshakeManager::onSocketStateChanged);
    }
}

} // namespace qindb