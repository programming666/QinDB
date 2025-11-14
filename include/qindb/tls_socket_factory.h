#ifndef QINDB_TLS_SOCKET_FACTORY_H
#define QINDB_TLS_SOCKET_FACTORY_H

#include "qindb/tls_config.h"
#include "qindb/tls_handshake_manager.h"
#include "qindb/fingerprint_manager.h"
#include "qindb/ssLError_handler.h"
#include <QTcpSocket>
#include <QSslSocket>
#include <memory>

namespace qindb {

/**
 * @brief TLS Socket工厂 - 简化TLS socket的创建和配置
 */
class TLSSocketFactory {
public:
    explicit TLSSocketFactory(const TLSConfig& config);
    ~TLSSocketFactory();

    /**
     * @brief 创建服务器端SSL socket
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
