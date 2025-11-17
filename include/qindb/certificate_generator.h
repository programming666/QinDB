#ifndef QINDB_CERTIFICATE_GENERATOR_H  // 防止头文件重复包含
#define QINDB_CERTIFICATE_GENERATOR_H

#include <QString>    // Qt字符串类
#include <QSslCertificate>  // Qt SSL证书类
#include <QSslKey>    // Qt SSL密钥类
#include <QPair>      // Qt键值对容器

namespace qindb {  // 命名空间声明

/**
 * @brief 证书生成器 - 用于生成自签名TLS证书
 * 这个类提供了生成、保存、加载SSL证书和私钥的功能，以及一些辅助方法
 */
class CertificateGenerator {
public:
    /**
     * @brief 生成自签名证书和私钥
     * @param commonName 证书通用名称(CN)
     * @param organization 组织名称，默认为"QinDB"
     * @param validityDays 证书有效期(天)，默认为365天
     * @return QPair<证书, 私钥> 返回生成的证书和私钥对
     */
    static QPair<QSslCertificate, QSslKey> generateSelfSignedCertificate(
        const QString& commonName,
        const QString& organization = "QinDB",
        int validityDays = 365
    );

    /**
     * @brief 保存证书到文件
     * @param cert 证书
     * @param certPath 证书文件路径
     * @return 是否成功
     */
    static bool saveCertificate(const QSslCertificate& cert, const QString& certPath);

    /**
     * @brief 保存私钥到文件
     * @param key 私钥
     * @param keyPath 私钥文件路径
     * @param passphrase 密码(可选)
     * @return 是否成功
     */
    static bool savePrivateKey(const QSslKey& key, const QString& keyPath,
                              const QByteArray& passphrase = QByteArray());

    /**
     * @brief 从文件加载证书
     * @param certPath 证书文件路径
     * @return 证书
     */
    static QSslCertificate loadCertificate(const QString& certPath);

    /**
     * @brief 从文件加载私钥
     * @param keyPath 私钥文件路径
     * @param passphrase 密码(可选)
     * @return 私钥
     */
    static QSslKey loadPrivateKey(const QString& keyPath,
                                  const QByteArray& passphrase = QByteArray());

    /**
     * @brief 获取证书指纹(SHA256)
     * @param cert 证书
     * @return 指纹(十六进制字符串)
     */
    static QString getCertificateFingerprint(const QSslCertificate& cert);

    /**
     * @brief 格式化指纹为易读形式(XX:XX:XX:...)
     * @param fingerprint 原始指纹
     * @return 格式化后的指纹
     */
    static QString formatFingerprint(const QString& fingerprint);

private:
    /**
     * @brief 生成RSA密钥对
     * @param keySize 密钥大小(位)
     * @return 私钥
     */
    static QSslKey generateRSAKey(int keySize = 2048);

    /**
     * @brief 创建X.509证书
     * @param publicKey 公钥
     * @param privateKey 私钥
     * @param commonName CN
     * @param organization O
     * @param validityDays 有效期
     * @return 证书
     */
    static QSslCertificate createX509Certificate(
        const QSslKey& publicKey,
        const QSslKey& privateKey,
        const QString& commonName,
        const QString& organization,
        int validityDays
    );
};

} // namespace qindb

#endif // QINDB_CERTIFICATE_GENERATOR_H
