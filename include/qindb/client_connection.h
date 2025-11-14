#ifndef QINDB_CLIENT_CONNECTION_H
#define QINDB_CLIENT_CONNECTION_H

#include "qindb/protocol.h"
#include "qindb/auth_manager.h"
#include <QtCore/QObject>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QSslSocket>
#include <QtCore/QByteArray>
#include <memory>

namespace qindb {

class DatabaseManager;

/**
 * @brief 客户端连接处理类
 *
 * 每个客户端连接对应一个 ClientConnection 对象。
 * 负责处理单个客户端的所有网络通信和查询请求。
 */
class ClientConnection : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param socket TCP套接字(已连接)
     * @param dbManager 数据库管理器指针
     * @param authManager 认证管理器指针
     * @param parent 父对象
     */
    explicit ClientConnection(QTcpSocket* socket,
                              DatabaseManager* dbManager,
                              AuthManager* authManager,
                              QObject* parent = nullptr);

    ~ClientConnection() override;

    /**
     * @brief 获取客户端地址
     */
    QString clientAddress() const;

    /**
     * @brief 获取会话 ID
     */
    uint64_t sessionId() const { return sessionId_; }

    /**
     * @brief 是否已认证
     */
    bool isAuthenticated() const { return isAuthenticated_; }

signals:
    /**
     * @brief 连接已断开信号
     */
    void disconnected();

    /**
     * @brief 错误信号
     */
    void error(const QString& errorMessage);

private slots:
    /**
     * @brief 读取数据槽函数
     */
    void onReadyRead();

    /**
     * @brief 断开连接槽函数
     */
    void onDisconnected();

    /**
     * @brief 错误槽函数
     */
    void onError(QAbstractSocket::SocketError socketError);

    /**
     * @brief SSL错误槽函数
     */
    void onSslErrors(const QList<QSslError>& errors);

private:
    /**
     * @brief 处理接收到的消息
     */
    void handleMessage(const QByteArray& data);

    /**
     * @brief 处理认证请求
     */
    void handleAuthRequest(const QByteArray& payload);

    /**
     * @brief 处理查询请求
     */
    void handleQueryRequest(const QByteArray& payload);

    /**
     * @brief 处理 PING 请求
     */
    void handlePing();

    /**
     * @brief 处理断开连接请求
     */
    void handleDisconnect();

    /**
     * @brief 发送响应消息
     */
    void sendMessage(const QByteArray& data);

    /**
     * @brief 发送错误响应
     */
    void sendError(uint32_t errorCode, const QString& message, const QString& detail = QString());

    /**
     * @brief 认证用户
     * @return 认证是否成功
     */
    bool authenticateUser(const QString& username, const QString& password, const QString& database);

    /**
     * @brief 生成会话 ID
     */
    uint64_t generateSessionId();

private:
    QTcpSocket* socket_;               // TCP 套接字
    DatabaseManager* dbManager_;       // 数据库管理器
    AuthManager* authManager_;         // 认证管理器
    QByteArray receiveBuffer_;         // 接收缓冲区
    uint64_t sessionId_;               // 会话 ID
    bool isAuthenticated_;             // 是否已认证
    QString currentDatabase_;          // 当前数据库名
    QString username_;                 // 用户名

    static uint64_t nextSessionId_;    // 下一个会话 ID（全局计数器）
};

} // namespace qindb

#endif // QINDB_CLIENT_CONNECTION_H
