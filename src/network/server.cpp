#include "qindb/server.h"
#include "qindb/client_connection.h"
#include "qindb/database_manager.h"
#include "qindb/tls_config.h"
#include "qindb/tls_socket_factory.h"
#include "qindb/logger.h"
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QSslSocket>
#include <QFile>

namespace qindb {

Server::Server(DatabaseManager* dbManager, AuthManager* authManager, QObject* parent)
    : QObject(parent)
    , tcpServer_(new QTcpServer(this))
    , dbManager_(dbManager)
    , authManager_(authManager)
    , maxConnections_(1000)
    , whitelistEnabled_(false)
    , sslEnabled_(false) {

    // 连接信号槽
    connect(tcpServer_, &QTcpServer::newConnection, this, &Server::onNewConnection);
}

Server::~Server() {
    stop();
}

bool Server::start(const QString& address, uint16_t port) {
    if (tcpServer_->isListening()) {
        LOG_WARN("Server is already running");
        return true;
    }

    QHostAddress hostAddress;
    if (address == "0.0.0.0" || address.isEmpty()) {
        hostAddress = QHostAddress::Any;
    } else {
        hostAddress = QHostAddress(address);
    }

    // 监听指定地址和端口
    if (!tcpServer_->listen(hostAddress, port)) {
        QString errorMsg = tcpServer_->errorString();
        LOG_ERROR(QString("Failed to start server: %1").arg(errorMsg));
        emit serverError(errorMsg);
        return false;
    }

    LOG_INFO(QString("Server started on %1:%2 (max connections: %3)")
                .arg(tcpServer_->serverAddress().toString())
                .arg(tcpServer_->serverPort())
                .arg(maxConnections_));

    return true;
}

void Server::stop() {
    if (!tcpServer_->isListening()) {
        return;
    }

    LOG_INFO("Stopping server...");

    // 关闭所有客户端连接
    for (ClientConnection* conn : connections_) {
        conn->deleteLater();
    }
    connections_.clear();

    // 停止监听
    tcpServer_->close();

    LOG_INFO("Server stopped");
}

bool Server::isRunning() const {
    return tcpServer_->isListening();
}

int Server::connectionCount() const {
    return connections_.size();
}

void Server::setMaxConnections(int maxConnections) {
    maxConnections_ = maxConnections;
    LOG_INFO(QString("Max connections set to %1").arg(maxConnections_));
}

bool Server::enableTLS(const QString& certPath, const QString& keyPath, bool autoGenerate) {
    LOG_INFO(QString("Configuring TLS: cert=%1, key=%2, autoGenerate=%3")
                .arg(certPath).arg(keyPath).arg(autoGenerate));

    // 创建TLS配置
    tlsConfig_ = std::make_unique<TLSConfig>();
    tlsConfig_->setAllowSelfSigned(true);
    tlsConfig_->setVerifyMode(TLSVerifyMode::NONE);

    // 检查证书和密钥文件是否存在
    bool certExists = QFile::exists(certPath);
    bool keyExists = QFile::exists(keyPath);

    if (certExists && keyExists) {
        // 加载现有证书和私钥
        if (!tlsConfig_->loadFromFiles(certPath, keyPath)) {
            LOG_ERROR("Failed to load TLS configuration from files");
            tlsConfig_.reset();
            return false;
        }
        LOG_INFO(QString("Loaded TLS certificate (fingerprint: %1)")
            .arg(tlsConfig_->certificateFingerprint()));
    } else if (autoGenerate) {
        // 生成自签名证书
        LOG_INFO("Certificate or key file not found, generating self-signed certificate...");

        if (!tlsConfig_->generateSelfSigned("QinDB Server", "QinDB", 365)) {
            LOG_ERROR("Failed to generate self-signed certificate");
            tlsConfig_.reset();
            return false;
        }

        // 保存证书和私钥
        if (!tlsConfig_->saveToFiles(certPath, keyPath)) {
            LOG_ERROR("Failed to save generated certificate");
            tlsConfig_.reset();
            return false;
        }

        LOG_INFO(QString("Self-signed certificate generated and saved (fingerprint: %1)")
            .arg(tlsConfig_->certificateFingerprint()));
    } else {
        LOG_ERROR("Certificate or key file not found and autoGenerate=false");
        tlsConfig_.reset();
        return false;
    }

    // 创建TLS socket工厂
    tlsSocketFactory_ = std::make_unique<TLSSocketFactory>(*tlsConfig_);

    sslEnabled_ = true;
    LOG_INFO("TLS enabled successfully");
    return true;
}

// ========== IP 白名单管理 ==========

void Server::addWhitelistIP(const QString& cidr) {
    ipWhitelist_.insert(cidr);
    whitelistEnabled_ = true;
    LOG_INFO(QString("Added IP to whitelist: %1").arg(cidr));
}

void Server::removeWhitelistIP(const QString& cidr) {
    ipWhitelist_.remove(cidr);
    if (ipWhitelist_.isEmpty()) {
        whitelistEnabled_ = false;
    }
    LOG_INFO(QString("Removed IP from whitelist: %1").arg(cidr));
}

void Server::clearWhitelist() {
    ipWhitelist_.clear();
    whitelistEnabled_ = false;
    LOG_INFO("Cleared IP whitelist");
}

bool Server::isIPWhitelisted(const QString& ip) const {
    if (!whitelistEnabled_) {
        return true;  // 白名单未启用，允许所有 IP
    }

    // 将IP地址字符串转换为四字节整数
    QStringList ipParts = ip.split('.');
    if (ipParts.size() != 4) {
        return false;  // 无效的IPv4地址
    }

    bool ok;
    uint32_t ipValue = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t octet = ipParts[i].toUInt(&ok);
        if (!ok || ipParts[i].isEmpty()) {
            return false;  // 无效的IP八位字节
        }
        ipValue = (ipValue << 8) | octet;
    }

    // 检查每个CIDR范围
    for (const QString& cidr : ipWhitelist_) {
        // 解析CIDR表示法（例如：192.168.1.0/24）
        int slashIndex = cidr.indexOf('/');
        QString networkStr;
        int prefixLen = 32;  // 默认为单个IP地址

        if (slashIndex != -1) {
            networkStr = cidr.left(slashIndex);
            prefixLen = cidr.mid(slashIndex + 1).toInt(&ok);
            if (!ok || prefixLen < 0 || prefixLen > 32) {
                LOG_WARN(QString("Invalid CIDR prefix length: %1").arg(cidr));
                continue;
            }
        } else {
            networkStr = cidr;
        }

        // 解析网络地址
        QStringList networkParts = networkStr.split('.');
        if (networkParts.size() != 4) {
            LOG_WARN(QString("Invalid CIDR network address: %1").arg(networkStr));
            continue;
        }

        uint32_t networkValue = 0;
        for (int i = 0; i < 4; i++) {
            uint8_t octet = networkParts[i].toUInt(&ok);
            if (!ok || networkParts[i].isEmpty()) {
                LOG_WARN(QString("Invalid CIDR network octet: %1").arg(networkStr));
                networkValue = 0;
                break;
            }
            networkValue = (networkValue << 8) | octet;
        }

        // 计算网络掩码
        uint32_t mask = (prefixLen == 0) ? 0 : (0xFFFFFFFFU << (32 - prefixLen));

        // 检查IP是否在CIDR范围内
        if ((ipValue & mask) == (networkValue & mask)) {
            return true;
        }
    }

    return false;
}

// ========== 槽函数 ==========

void Server::onNewConnection() {
    while (tcpServer_->hasPendingConnections()) {
        QTcpSocket* rawSocket = tcpServer_->nextPendingConnection();
        QTcpSocket* socket = rawSocket;

        // 如果启用TLS,升级为SSL socket
        if (sslEnabled_ && tlsSocketFactory_) {
            QSslSocket* sslSocket = tlsSocketFactory_->createServerSocket(rawSocket);
            if (!sslSocket) {
                LOG_ERROR("Failed to create SSL socket");
                rawSocket->deleteLater();
                continue;
            }

            // 使rawSocket成为sslSocket的子对象,这样当sslSocket被删除时rawSocket也会被删除
            rawSocket->setParent(sslSocket);

            QString clientIP = sslSocket->peerAddress().toString();
            QString clientAddress = QString("%1:%2")
                                       .arg(clientIP)
                                       .arg(sslSocket->peerPort());

            LOG_INFO(QString("Incoming TLS connection from %1")
                        .arg(clientAddress));

            // 检查是否可以接受新连接
            if (!canAcceptConnection(clientIP)) {
                LOG_WARN(QString("Connection rejected from %1 (whitelist/limit)").arg(clientAddress));
                sslSocket->disconnectFromHost();
                sslSocket->deleteLater();
                continue;
            }

            // 启动服务器加密 - 在创建ClientConnection之前
            sslSocket->startServerEncryption();
            LOG_INFO(QString("Started TLS handshake for %1").arg(clientAddress));

            // 立即创建ClientConnection，让它处理TLS握手过程
            // ClientConnection会在构造函数中设置socket的parent，确保socket不会被过早删除
            socket = sslSocket;  // 使用SSL socket而不是原始socket
        }

        // 非TLS连接的处理
        QString clientIP = socket->peerAddress().toString();
        QString clientAddress = QString("%1:%2")
                                   .arg(clientIP)
                                   .arg(socket->peerPort());

        LOG_INFO(QString("Incoming TCP connection from %1")
                    .arg(clientAddress));

        // 检查是否可以接受新连接
        if (!canAcceptConnection(clientIP)) {
            LOG_WARN(QString("Connection rejected from %1 (whitelist/limit)").arg(clientAddress));
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }

        // 创建客户端连接对象
        ClientConnection* connection = new ClientConnection(
            socket,
            dbManager_,
            authManager_,
            this
        );

        // 连接信号槽
        connect(connection, &ClientConnection::disconnected,
                this, &Server::onClientDisconnected);

        connections_.append(connection);

        emit clientConnected(clientAddress);

        LOG_INFO(QString("Client connected: %1 (total: %2/%3, plain TCP)")
                    .arg(clientAddress)
                    .arg(connections_.size())
                    .arg(maxConnections_));
    }
}

void Server::onClientDisconnected() {
    ClientConnection* connection = qobject_cast<ClientConnection*>(sender());
    if (!connection) {
        return;
    }

    QString clientAddress = connection->clientAddress();

    // 从连接列表中移除
    connections_.removeOne(connection);

    // 删除连接对象
    connection->deleteLater();

    emit clientDisconnected(clientAddress);

    LOG_INFO(QString("Client disconnected: %1 (total: %2/%3)")
                .arg(clientAddress)
                .arg(connections_.size())
                .arg(maxConnections_));
}

// ========== 辅助方法 ==========

bool Server::canAcceptConnection(const QString& clientIP) const {
    // 检查连接数限制
    if (connections_.size() >= maxConnections_) {
        return false;
    }

    // 检查 IP 白名单
    if (!isIPWhitelisted(clientIP)) {
        return false;
    }

    return true;
}

} // namespace qindb
