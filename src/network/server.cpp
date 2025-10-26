#include "qindb/server.h"
#include "qindb/client_connection.h"
#include "qindb/database_manager.h"
#include "qindb/logger.h"
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpSocket>

namespace qindb {

Server::Server(DatabaseManager* dbManager, AuthManager* authManager, QObject* parent)
    : QObject(parent)
    , tcpServer_(new QTcpServer(this))
    , dbManager_(dbManager)
    , authManager_(authManager)
    , maxConnections_(1000)
    , whitelistEnabled_(false) {

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
        QTcpSocket* socket = tcpServer_->nextPendingConnection();
        QString clientIP = socket->peerAddress().toString();
        QString clientAddress = QString("%1:%2")
                                   .arg(clientIP)
                                   .arg(socket->peerPort());

        LOG_INFO(QString("Incoming connection from %1").arg(clientAddress));

        // 检查是否可以接受新连接
        if (!canAcceptConnection(clientIP)) {
            LOG_WARN(QString("Connection rejected from %1 (whitelist/limit)").arg(clientAddress));
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }

        // 创建客户端连接对象(直接传递socket对象)
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

        LOG_INFO(QString("Client connected: %1 (total: %2/%3)")
                    .arg(clientAddress)
                    .arg(connections_.size())
                    .arg(maxConnections_));

        // 注意: socket现在由ClientConnection管理,不要在这里删除
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
