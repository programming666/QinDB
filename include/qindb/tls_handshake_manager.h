#ifndef QINDB_TLS_HANDSHAKE_MANAGER_H  // 防止头文件重复包含
#define QINDB_TLS_HANDSHAKE_MANAGER_H

#include "qindb/tls_config.h"    // 引入TLS配置头文件
#include "qindb/logger.h"        // 引入日志功能头文件
#include <QObject>               // Qt对象基类
#include <QSslSocket>            // Qt SSL套接字类
#include <QTimer>                // Qt定时器类
#include <memory>                // 智能指针相关头文件

namespace qindb {  // 定义qindb命名空间

/**
 * @brief TLS握手状态枚举
 * 定义了TLS握手过程中可能的各种状态
 */
enum class TLSHandshakeState {
    IDLE,                    // 初始状态 - 握手尚未开始
    HANDSHAKE_STARTED,      // 握手开始 - SSL握手过程已启动
    CERTIFICATE_RECEIVED,   // 证书接收完成 - 已收到对端证书
    HANDSHAKE_COMPLETED,    // 握手成功完成 - TLS握手成功完成
    HANDSHAKE_FAILED,       // 握手失败 - 握手过程中出现错误
    HANDSHAKE_TIMEOUT       // 握手超时 - 握手过程超过设定时间
};

/**
 * @brief TLS握手结果结构体
 * 用于存储握手操作的结果信息
 */
struct TLSHandshakeResult {
    bool success;                    // 握手是否成功
    QString errorMessage;            // 错误信息描述
    QList<QSslError> errors;        // SSL错误列表
    QSslCertificate peerCertificate; // 对端证书
    
    // 默认构造函数
    TLSHandshakeResult() : success(false) {}
    
    // 带参数的构造函数
    TLSHandshakeResult(bool succ, const QString& error = "") 
        : success(succ), errorMessage(error) {}
};

/**
 * @brief TLS握手管理器 - 统一管理TLS握手过程
 */
class TLSHandshakeManager : public QObject {
    Q_OBJECT

public:
    explicit TLSHandshakeManager(const TLSConfig& config, QObject* parent = nullptr);
    ~TLSHandshakeManager();

    /**
     * @brief 开始TLS握手
     * @param socket SSL socket
     * @param isServer 是否为服务器模式
     * @param timeoutMs 超时时间(毫秒)
     * @return 是否成功开始握手
     */
    bool startHandshake(QSslSocket* socket, bool isServer, int timeoutMs = 30000);

    /**
     * @brief 取消当前握手
     */
    void cancelHandshake();

    /**
     * @brief 获取当前握手状态
     */
    TLSHandshakeState state() const { return state_; }

    /**
     * @brief 获取握手结果
     */
    TLSHandshakeResult result() const { return result_; }

    /**
     * @brief 检查握手是否已完成
     */
    bool isHandshakeComplete() const { 
        return state_ == TLSHandshakeState::HANDSHAKE_COMPLETED || 
               state_ == TLSHandshakeState::HANDSHAKE_FAILED ||
               state_ == TLSHandshakeState::HANDSHAKE_TIMEOUT; 
    }

    /**
     * @brief 检查握手是否成功
     */
    bool isHandshakeSuccessful() const { 
        return state_ == TLSHandshakeState::HANDSHAKE_COMPLETED; 
    }

signals:
    /**
     * @brief 握手完成信号
     */
    void handshakeCompleted(const TLSHandshakeResult& result);

    /**
     * @brief 握手状态改变信号
     */
    void stateChanged(TLSHandshakeState newState);

private slots:
    /**
     * @brief SSL错误处理
     */
    void onSslErrors(const QList<QSslError>& errors);

    /**
     * @brief 加密完成处理
     */
    void onEncrypted();

    /**
     * @brief 握手超时处理
     */
    void onHandshakeTimeout();

    /**
     * @brief 连接状态改变处理
     */
    void onSocketStateChanged(QAbstractSocket::SocketState socketState);

private:
    /**
     * @brief 设置握手状态(带状态转换验证)
     */
    void setState(TLSHandshakeState newState);

    /**
     * @brief 验证状态转换是否合法
     */
    bool isValidStateTransition(TLSHandshakeState from, TLSHandshakeState to) const;

    /**
     * @brief 处理SSL错误
     */
    void processSslErrors(const QList<QSslError>& errors);

    /**
     * @brief 验证证书
     */
    bool validateCertificate(const QSslCertificate& cert);

    /**
     * @brief 清理资源
     */
    void cleanup();

    /**
     * @brief 断开socket信号连接
     */
    void disconnectSocketSignals();

private:
    TLSConfig config_;
    QSslSocket* currentSocket_;
    TLSHandshakeState state_;
    TLSHandshakeResult result_;
    std::unique_ptr<QTimer> timeoutTimer_;
    bool isServerMode_;
    bool isActive_;
};

} // namespace qindb

#endif // QINDB_TLS_HANDSHAKE_MANAGER_H