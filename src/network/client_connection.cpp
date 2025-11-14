#include "qindb/client_connection.h"
#include "qindb/message_codec.h"
#include "qindb/database_manager.h"
#include "qindb/executor.h"
#include "qindb/parser.h"
#include "qindb/logger.h"
#include "qindb/ssLError_handler.h"
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QSslSocket>

namespace qindb {

// 静态成员初始化
uint64_t ClientConnection::nextSessionId_ = 1;

ClientConnection::ClientConnection(QTcpSocket* socket,
                                   DatabaseManager* dbManager,
                                   AuthManager* authManager,
                                   QObject* parent)
    : QObject(parent)
    , socket_(socket)
    , dbManager_(dbManager)
    , authManager_(authManager)
    , sessionId_(0)
    , isAuthenticated_(false) {

    // 设置套接字的父对象为this,这样套接字会随着ClientConnection一起被删除
    socket_->setParent(this);

    // 连接信号槽
    connect(socket_, &QTcpSocket::readyRead, this, &ClientConnection::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, &ClientConnection::onDisconnected);
    connect(socket_, &QTcpSocket::errorOccurred, this, &ClientConnection::onError);

    // 如果是SSL连接，处理SSL错误
    QSslSocket* sslSocket = qobject_cast<QSslSocket*>(socket_);
    if (sslSocket) {
        connect(sslSocket, &QSslSocket::sslErrors, this, &ClientConnection::onSslErrors);
        LOG_INFO("SSL socket detected, connected SSL error handler");
    }

    LOG_INFO(QString("New client connected from %1:%2")
                .arg(socket_->peerAddress().toString())
                .arg(socket_->peerPort()));
}

ClientConnection::~ClientConnection() {
    if (socket_->state() == QAbstractSocket::ConnectedState) {
        socket_->disconnectFromHost();
    }

    LOG_INFO(QString("Client disconnected (session: %1)").arg(sessionId_));
}

QString ClientConnection::clientAddress() const {
    return QString("%1:%2")
        .arg(socket_->peerAddress().toString())
        .arg(socket_->peerPort());
}

// ========== 槽函数 ==========

void ClientConnection::onReadyRead() {
    // 读取所有可用数据
    receiveBuffer_.append(socket_->readAll());

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

void ClientConnection::onDisconnected() {
    LOG_INFO(QString("Client %1 disconnected").arg(clientAddress()));
    emit disconnected();
}

void ClientConnection::onError(QAbstractSocket::SocketError socketError) {
    QString errorMsg = socket_->errorString();
    LOG_ERROR(QString("Socket error (%1): %2").arg(socketError).arg(errorMsg));
    emit error(errorMsg);
}

// ========== 消息处理 ==========

void ClientConnection::handleMessage(const QByteArray& data) {
    MessageType type;
    QByteArray payload;

    if (!MessageCodec::decodeMessage(data, type, payload)) {
        sendError(NetworkErrorCode::PROTOCOL_ERROR, "Invalid message format");
        return;
    }

    LOG_DEBUG(QString("Received message type: %1").arg(static_cast<int>(type)));

    switch (type) {
    case MessageType::AUTH_REQUEST:
        handleAuthRequest(payload);
        break;

    case MessageType::QUERY_REQUEST:
        handleQueryRequest(payload);
        break;

    case MessageType::PING:
        handlePing();
        break;

    case MessageType::DISCONNECT:
        handleDisconnect();
        break;

    default:
        sendError(NetworkErrorCode::INVALID_MESSAGE, "Unsupported message type");
        break;
    }
}

void ClientConnection::handleAuthRequest(const QByteArray& payload) {
    auto requestOpt = MessageCodec::decodeAuthRequest(payload);
    if (!requestOpt) {
        sendError(NetworkErrorCode::PROTOCOL_ERROR, "Failed to decode AUTH_REQUEST");
        return;
    }

    const AuthRequest& request = *requestOpt;

    LOG_INFO(QString("Auth request from user '%1', database '%2'")
                .arg(request.username)
                .arg(request.database));

    // 检查协议版本
    if (request.protocolVersion != PROTOCOL_VERSION) {
        AuthResponse response;
        response.status = AuthStatus::AUTH_FAILED;
        response.message = QString("Protocol version mismatch (server: %1, client: %2)")
                              .arg(PROTOCOL_VERSION)
                              .arg(request.protocolVersion);
        sendMessage(MessageCodec::encodeAuthResponse(response));
        return;
    }

    // 认证用户
    bool authSuccess = authenticateUser(request.username, request.password, request.database);

    AuthResponse response;
    if (authSuccess) {
        response.status = AuthStatus::SUCCESS;
        response.sessionId = sessionId_;
        response.message = "Authentication successful";
        username_ = request.username;
        currentDatabase_ = request.database;
        isAuthenticated_ = true;

        LOG_INFO(QString("User '%1' authenticated successfully (session: %2)")
                    .arg(username_).arg(sessionId_));
    } else {
        response.status = AuthStatus::AUTH_FAILED;
        response.message = "Invalid username or password";

        LOG_WARN(QString("Authentication failed for user '%1'").arg(request.username));
    }

    sendMessage(MessageCodec::encodeAuthResponse(response));
}

void ClientConnection::handleQueryRequest(const QByteArray& payload) {
    // 检查是否已认证
    if (!isAuthenticated_) {
        sendError(NetworkErrorCode::AUTH_FAILED, "Not authenticated");
        return;
    }

    auto requestOpt = MessageCodec::decodeQueryRequest(payload);
    if (!requestOpt) {
        sendError(NetworkErrorCode::PROTOCOL_ERROR, "Failed to decode QUERY_REQUEST");
        return;
    }

    const QueryRequest& request = *requestOpt;

    LOG_INFO(QString("Executing query (session: %1): %2")
                .arg(sessionId_)
                .arg(request.sql.left(100)));  // 只记录前100个字符

    // 验证会话 ID
    if (request.sessionId != sessionId_) {
        sendError(NetworkErrorCode::SESSION_EXPIRED, "Invalid session ID");
        return;
    }

    // 执行查询
    QueryResponse result;

    try {
        // 切换到目标数据库
        if (!dbManager_->useDatabase(currentDatabase_)) {
            sendError(NetworkErrorCode::RUNTIME_ERROR,
                     QString("Failed to switch to database '%1'").arg(currentDatabase_));
            return;
        }

        // 创建执行器
        Executor executor(dbManager_);

        // 解析并执行SQL
        Parser parser(request.sql);
        auto ast = parser.parse();

        if (!ast) {
            sendError(NetworkErrorCode::SYNTAX_ERROR,
                     QString("Failed to parse SQL: %1").arg(request.sql));
            return;
        }

        // 执行 SQL
        auto queryResult = executor.execute(ast);

        if (queryResult.success) {
            result.status = QueryStatus::SUCCESS;
            result.rowsAffected = queryResult.rows.size();

            // 转换列定义
            for (const auto& colName : queryResult.columnNames) {
                ColumnInfo colInfo;
                colInfo.name = colName;
                colInfo.type = static_cast<uint8_t>(DataType::VARCHAR);  // 简化处理
                result.columns.append(colInfo);
            }

            // 转换行数据
            result.rows = queryResult.rows;

            // 设置结果类型
            if (queryResult.rows.isEmpty()) {
                result.resultType = ResultType::EMPTY;
            } else {
                result.resultType = ResultType::TABLE_DATA;
            }
        } else {
            result.status = QueryStatus::RUNTIME_ERROR;

            // 发送错误响应
            sendError(NetworkErrorCode::RUNTIME_ERROR,
                     queryResult.error.message,
                     queryResult.error.detail);
            return;
        }
    } catch (const std::exception& e) {
        sendError(NetworkErrorCode::RUNTIME_ERROR, "Query execution failed", e.what());
        return;
    }

    // 发送查询响应
    sendMessage(MessageCodec::encodeQueryResponse(result));
}

void ClientConnection::handlePing() {
    // 发送 PONG 响应
    QByteArray pong = MessageCodec::encodeMessage(MessageType::PONG, QByteArray());
    sendMessage(pong);
}

void ClientConnection::handleDisconnect() {
    LOG_INFO(QString("Client requested disconnect (session: %1)").arg(sessionId_));
    socket_->disconnectFromHost();
}

// ========== 发送消息 ==========

void ClientConnection::sendMessage(const QByteArray& data) {
    if (socket_->state() != QAbstractSocket::ConnectedState) {
        LOG_WARN("Cannot send message: socket not connected");
        return;
    }

    qint64 written = socket_->write(data);
    if (written != data.size()) {
        LOG_ERROR(QString("Failed to send complete message (sent %1/%2 bytes)")
                     .arg(written).arg(data.size()));
    }

    socket_->flush();
}

void ClientConnection::sendError(uint32_t errorCode, const QString& message, const QString& detail) {
    ErrorResponse error;
    error.errorCode = errorCode;
    error.message = message;
    error.detail = detail;

    sendMessage(MessageCodec::encodeErrorResponse(error));

    LOG_ERROR(QString("Sent error to client: [%1] %2").arg(errorCode).arg(message));
}

// ========== 认证相关 ==========

bool ClientConnection::authenticateUser(const QString& username,
                                        const QString& password,
                                        const QString& database) {
    // 检查数据库是否存在
    if (!dbManager_->databaseExists(database)) {
        LOG_WARN(QString("Database '%1' not found").arg(database));
        return false;
    }

    // 使用 AuthManager 验证用户名和密码
    if (!authManager_->authenticate(username, password)) {
        LOG_WARN(QString("Authentication failed for user '%1'").arg(username));
        return false;
    }

    // 生成会话 ID
    sessionId_ = generateSessionId();

    return true;
}

uint64_t ClientConnection::generateSessionId() {
    return nextSessionId_++;
}

void ClientConnection::onSslErrors(const QList<QSslError>& errors) {
    // SSL错误处理 - 使用SSLErrorHandler统一管理
    QSslSocket* sslSocket = qobject_cast<QSslSocket*>(socket_);
    if (!sslSocket) {
        return;
    }

    // 使用SSLErrorHandler处理错误
    auto criticalErrors = SSLErrorHandler::filterIgnorableErrors(errors, true); // 服务器端允许自签名

    // 记录所有错误
    for (const auto& error : errors) {
        auto severity = SSLErrorHandler::getErrorSeverity(error, true);
        
        switch (severity) {
            case SSLErrorHandler::ErrorSeverity::CRITICAL:
                LOG_ERROR(QString("Critical SSL error: %1").arg(error.errorString()));
                break;
            case SSLErrorHandler::ErrorSeverity::WARNING:
                LOG_WARN(QString("SSL warning: %1").arg(error.errorString()));
                break;
            case SSLErrorHandler::ErrorSeverity::IGNORABLE:
                LOG_DEBUG(QString("Ignoring SSL error: %1").arg(error.errorString()));
                break;
        }
    }

    if (criticalErrors.isEmpty()) {
        // 没有严重错误,忽略所有错误(主要是自签名证书相关)
        LOG_INFO(QString("Ignoring %1 SSL error(s) for self-signed certificate").arg(errors.size()));
        sslSocket->ignoreSslErrors(errors);
    } else {
        LOG_ERROR(QString("Cannot ignore %1 critical SSL error(s)").arg(criticalErrors.size()));
        // 不调用ignoreSslErrors(),让连接失败
    }
}

} // namespace qindb
