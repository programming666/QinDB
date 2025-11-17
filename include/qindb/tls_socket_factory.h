#ifndef QINDB_TLS_SOCKET_FACTORY_H  // 防止重复包含宏定义
#define QINDB_TLS_SOCKET_FACTORY_H

#include "qindb/tls_config.h"        // 包含TLS配置相关头文件
#include "qindb/tls_handshake_manager.h"  // 包含TLS握手管理器相关头文件
#include "qindb/fingerprint_manager.h"    // 包含指纹管理器相关头文件
#include "qindb/ssLError_handler.h"       // 包含SSL错误处理相关头文件
#include <QTcpSocket>                    // Qt TCP套接字类
#include <QSslSocket>                    // Qt SSL套接字类
#include <memory>                        // 智能指针相关头文件

namespace qindb {  // 定义qindb命名空间

/**
 * @brief TLS Socket工厂 - 简化TLS socket的创建和配置
 * 
 * 这个类负责创建和配置SSL/TLS socket，支持服务器端和客户端模式。
 * 它封装了SSL socket的创建、配置和错误处理等复杂操作。
 */
class TLSSocketFactory {
public:
    explicit TLSSocketFactory(const TLSConfig& config);  // 构造函数，接收TLS配置参数
    ~TLSSocketFactory();  // 析构函数

    /**
     * @brief 创建服务器端SSL socket
     *
     * @param rawSocket 原始TCP socket(已连接)
     * @return SSL socket,如果失败返回nullptr
     */
    QSslSocket* createServerSocket(QTcpSocket* rawSocket);

    /**
     * @brief 创建客户端SSL socket
     * @return SSL socket,调用者负责连接
     */
    QSslSocket* createClientSocket();

    /**
     * @brief 配置socket的SSL错误处理
     * @param socket SSL socket
     * @param isServer 是否为服务器端
     */
    void configureErrorHandling(QSslSocket* socket, bool isServer);

    /**
     * @brief 设置指纹管理器(用于客户端指纹验证)
     */
    void setFingerprintManager(FingerprintManager* manager);

    /**
     * @brief 获取TLS配置
     */
    const TLSConfig& config() const { return config_; }

private:
    TLSConfig config_;
    FingerprintManager* fingerprintManager_;
};

} // namespace qindb

#endif // QINDB_TLS_SOCKET_FACTORY_H
