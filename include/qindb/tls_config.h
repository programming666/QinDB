#ifndef QINDB_TLS_CONFIG_H
#define QINDB_TLS_CONFIG_H

#include <QString>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslConfiguration>
#include <QSsl>

namespace qindb {

/**
 * @brief TLS验证模式
 */
enum class TLSVerifyMode {
    NONE,              // 不验证对方证书
    OPTIONAL,          // 可选验证(证书错误时继续)
    REQUIRED,          // 必须验证(证书错误时断开)
    FINGERPRINT        // 使用指纹验证(类似SSH)
};

/**
 * @brief TLS配置类 - 统一管理TLS相关配置
 */
class TLSConfig {
public:
    TLSConfig();

    /**
     * @brief 设置证书和私钥
     */
    void setCertificate(const QSslCertificate& cert);
    void setPrivateKey(const QSslKey& key);

    /**
     * @brief 从文件加载证书和私钥
     */
    bool loadFromFiles(const QString& certPath, const QString& keyPath,
                      const QByteArray& keyPassphrase = QByteArray());

    /**
     * @brief 保存证书和私钥到文件
     */
    bool saveToFiles(const QString& certPath, const QString& keyPath) const;

    /**
     * @brief 生成自签名证书
     */
    bool generateSelfSigned(const QString& commonName,
                           const QString& organization = "QinDB",
                           int validityDays = 365);

    /**
     * @brief 设置验证模式
     */
    void setVerifyMode(TLSVerifyMode mode);
    TLSVerifyMode verifyMode() const { return verifyMode_; }

    /**
     * @brief 设置是否允许自签名证书
     */
    void setAllowSelfSigned(bool allow) { allowSelfSigned_ = allow; }
    bool allowSelfSigned() const { return allowSelfSigned_; }

    /**
     * @brief 获取证书和私钥
     */
    QSslCertificate certificate() const { return certificate_; }
    QSslKey privateKey() const { return privateKey_; }

    /**
     * @brief 检查配置是否有效
     */
    bool isValid() const;

    /**
     * @brief 创建QSslConfiguration
     */
    QSslConfiguration createSslConfiguration(bool isServer) const;

    /**
     * @brief 获取证书指纹
     */
    QString certificateFingerprint() const;

    /**
     * @brief 设置最小协议版本
     */
    void setMinimumProtocol(QSsl::SslProtocol protocol) {
        minimumProtocol_ = protocol;
    }
    QSsl::SslProtocol minimumProtocol() const { return minimumProtocol_; }

private:
    QSslCertificate certificate_;
    QSslKey privateKey_;
    TLSVerifyMode verifyMode_;
    bool allowSelfSigned_;
    QSsl::SslProtocol minimumProtocol_;
};

} // namespace qindb

#endif // QINDB_TLS_CONFIG_H
