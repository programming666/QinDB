#ifndef QINDB_CLIENT_MANAGER_H
#define QINDB_CLIENT_MANAGER_H

#include "qindb/connection_string_parser.h"
#include "qindb/protocol.h"
#include "qindb/message_codec.h"
#include <QtCore/QObject>
#include <QtNetwork/QTcpSocket>
#include <QtCore/QByteArray>
#include <QtCore/QTimer>
#include <memory>

namespace qindb {

/**
 * @brief 客户端连接管理器
 *
 * 负责管理与服务器的连接，处理认证和基本的网络通信。
 */
class ClientManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit ClientManager(QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~ClientManager() override;

    /**
     * @brief 连接到服务器
     * @param params 连接参数
     * @return 是否连接成功
     */
    bool connectToServer(const ConnectionParams& params);

    /**
     * @brief 断开与服务器的连接
     */
    void disconnectFromServer();

    /**
     * @brief 检查是否已连接
     * @return 是否已连接
     */
    bool isConnected() const;

    /**
     * @brief 检查是否已认证
     * @return 是否已认证
     */
    bool isAuthenticated() const;

    /**
     * @brief 发送SQL查询
     * @param sql SQL语句
     * @return 是否发送成功
     */
    bool sendQuery(const QString& sql);

    /**
     * @brief 获取连接状态信息
     * @return 状态信息
     */
    QString getConnectionInfo() const;

signals:
    /**
     * @brief 连接成功信号
     */
    void connected();

    /**
     * @brief 连接断开信号
     */
    void disconnected();

    /**
     * @brief 认证成功信号
     */
    void authenticated();

    /**
     * @brief 认证失败信号
     */
    void authenticationFailed(const QString& error);

    /**
     * @brief 查询响应信号
     */
    void queryResponse(const QueryResponse& response);

    /**
     * @brief 错误信号
     */
    void error(const QString& errorMessage);

    /**
     * @brief 连接状态变化信号
     */
    void connectionStatusChanged(const QString& status);

private slots:
    /**
     * @brief 连接槽函数
     */
    void onConnected();

    /**
     * @brief 断开连接槽函数
     */
    void onDisconnected();

    /**
     * @brief 读取数据槽函数
     */
    void onReadyRead();

    /**
     * @brief 错误槽函数
     */
    void onError(QAbstractSocket::SocketError socketError);

    /**
     * @brief 心跳超时槽函数
     */
    void onHeartbeatTimeout();

private:
    /**
     * @brief 处理接收到的消息
     */
    void handleMessage(const QByteArray& data);

    /**
     * @brief 处理认证响应
     */
    void handleAuthResponse(const AuthResponse& response);

    /**
     * @brief 处理查询响应
     */
    void handleQueryResponse(const QueryResponse& response);

    /**
     * @brief 处理错误响应
     */
    void handleErrorResponse(const ErrorResponse& error);

    /**
     * @brief 发送认证请求
     */
    void sendAuthRequest();

    /**
     * @brief 发送心跳包
     */
    void sendHeartbeat();

    /**
     * @brief 更新连接状态
     */
    void updateConnectionStatus(const QString& status);

private:
    QTcpSocket* socket_;                    // TCP套接字
    QByteArray receiveBuffer_;               // 接收缓冲区
    ConnectionParams connectionParams_;      // 连接参数
    uint64_t currentSessionId_;              // 当前会话ID
    bool isAuthenticated_;                   // 是否已认证
    QTimer* heartbeatTimer_;                 // 心跳定时器
    int heartbeatInterval_;                  // 心跳间隔（毫秒）
    int lastActivityTime_;                   // 最后活动时间
};

} // namespace qindb

#endif // QINDB_CLIENT_MANAGER_H
