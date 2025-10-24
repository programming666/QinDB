#ifndef QINDB_SERVER_H
#define QINDB_SERVER_H

#include <QtCore/QObject>
#include <QtNetwork/QTcpServer>
#include <QtCore/QVector>
#include <QtCore/QSet>
#include <memory>

namespace qindb {

class DatabaseManager;
class AuthManager;
class ClientConnection;

/**
 * @brief qinDB TCP/IP 服务器
 *
 * 负责监听客户端连接，为每个连接创建 ClientConnection 对象。
 * 支持并发连接管理、IP 白名单、连接限制等功能。
 */
class Server : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param dbManager 数据库管理器指针
     * @param parent 父对象
     */
    explicit Server(DatabaseManager* dbManager, AuthManager* authManager, QObject* parent = nullptr);

    ~Server() override;

    /**
     * @brief 启动服务器
     * @param address 监听地址（默认：0.0.0.0，监听所有网卡）
     * @param port 监听端口（默认：24678）
     * @return 是否启动成功
     */
    bool start(const QString& address = "0.0.0.0", uint16_t port = 24678);

    /**
     * @brief 停止服务器
     */
    void stop();

    /**
     * @brief 服务器是否正在运行
     */
    bool isRunning() const;

    /**
     * @brief 获取当前连接数
     */
    int connectionCount() const;

    /**
     * @brief 设置最大连接数
     */
    void setMaxConnections(int maxConnections);

    /**
     * @brief 获取最大连接数
     */
    int maxConnections() const { return maxConnections_; }

    /**
     * @brief 添加 IP 白名单
     * @param cidr CIDR 格式的 IP 地址（例如：192.168.1.0/24）
     */
    void addWhitelistIP(const QString& cidr);

    /**
     * @brief 移除 IP 白名单
     */
    void removeWhitelistIP(const QString& cidr);

    /**
     * @brief 清空 IP 白名单
     */
    void clearWhitelist();

    /**
     * @brief 检查 IP 是否在白名单中
     */
    bool isIPWhitelisted(const QString& ip) const;

signals:
    /**
     * @brief 新客户端连接信号
     */
    void clientConnected(const QString& clientAddress);

    /**
     * @brief 客户端断开连接信号
     */
    void clientDisconnected(const QString& clientAddress);

    /**
     * @brief 服务器错误信号
     */
    void serverError(const QString& errorMessage);

private slots:
    /**
     * @brief 新连接槽函数
     */
    void onNewConnection();

    /**
     * @brief 客户端断开连接槽函数
     */
    void onClientDisconnected();

private:
    /**
     * @brief 检查是否可以接受新连接
     */
    bool canAcceptConnection(const QString& clientIP) const;

private:
    QTcpServer* tcpServer_;                          // TCP 服务器
    DatabaseManager* dbManager_;                     // 数据库管理器
    AuthManager* authManager_;                       // 认证管理器
    QVector<ClientConnection*> connections_;         // 活动连接列表
    int maxConnections_;                             // 最大连接数
    QSet<QString> ipWhitelist_;                      // IP 白名单（CIDR 格式）
    bool whitelistEnabled_;                          // 是否启用白名单
};

} // namespace qindb

#endif // QINDB_SERVER_H
