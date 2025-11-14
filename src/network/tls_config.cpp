#include "qindb/tls_config.h"
#include "qindb/certificate_generator.h"
#include "qindb/logger.h"
#include <QFile>

namespace qindb {

TLSConfig::TLSConfig()
    : verifyMode_(TLSVerifyMode::NONE)
    , allowSelfSigned_(true)
    , minimumProtocol_(QSsl::TlsV1_2)
{
}

void TLSConfig::setCertificate(const QSslCertificate& cert) {
    certificate_ = cert;
}

void TLSConfig::setPrivateKey(const QSslKey& key) {
    privateKey_ = key;
}

bool TLSConfig::loadFromFiles(const QString& certPath, const QString& keyPath,
                              const QByteArray& keyPassphrase) {
    LOG_INFO(QString("Loading TLS configuration from files: cert=%1, key=%2")
        .arg(certPath).arg(keyPath));

    // 检查文件是否存在
    if (!QFile::exists(certPath)) {
        LOG_ERROR(QString("Certificate file not found: %1").arg(certPath));
        return false;
    }
    if (!QFile::exists(keyPath)) {
        LOG_ERROR(QString("Private key file not found: %1").arg(keyPath));
        return false;
    }

    // 加载证书
    certificate_ = CertificateGenerator::loadCertificate(certPath);
    if (certificate_.isNull()) {
        LOG_ERROR("Failed to load certificate");
        return false;
    }

    // 加载私钥
    privateKey_ = CertificateGenerator::loadPrivateKey(keyPath, keyPassphrase);
    if (privateKey_.isNull()) {
        LOG_ERROR("Failed to load private key");
        return false;
    }

    LOG_INFO(QString("TLS configuration loaded successfully (fingerprint: %1)")
        .arg(certificateFingerprint()));
    return true;
}

bool TLSConfig::saveToFiles(const QString& certPath, const QString& keyPath) const {
    if (!isValid()) {
        LOG_ERROR("Cannot save invalid TLS configuration");
        return false;
    }

    if (!CertificateGenerator::saveCertificate(certificate_, certPath)) {
        return false;
    }

    if (!CertificateGenerator::savePrivateKey(privateKey_, keyPath)) {
        return false;
    }

    LOG_INFO(QString("TLS configuration saved to: cert=%1, key=%2")
        .arg(certPath).arg(keyPath));
    return true;
}

bool TLSConfig::generateSelfSigned(const QString& commonName,
                                   const QString& organization,
                                   int validityDays) {
    LOG_INFO(QString("Generating self-signed certificate: CN=%1, O=%2, validity=%3 days")
        .arg(commonName).arg(organization).arg(validityDays));

    auto certKeyPair = CertificateGenerator::generateSelfSignedCertificate(
        commonName, organization, validityDays);

    if (certKeyPair.first.isNull() || certKeyPair.second.isNull()) {
        LOG_ERROR("Failed to generate self-signed certificate");
        return false;
    }

    certificate_ = certKeyPair.first;
    privateKey_ = certKeyPair.second;

    LOG_INFO(QString("Self-signed certificate generated (fingerprint: %1)")
        .arg(certificateFingerprint()));
    return true;
}

void TLSConfig::setVerifyMode(TLSVerifyMode mode) {
    verifyMode_ = mode;
    LOG_DEBUG(QString("TLS verify mode set to: %1").arg(static_cast<int>(mode)));
}

bool TLSConfig::isValid() const {
    return !certificate_.isNull() && !privateKey_.isNull();
}

QSslConfiguration TLSConfig::createSslConfiguration(bool isServer) const {
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();

    // 设置证书和私钥
    config.setLocalCertificate(certificate_);
    config.setPrivateKey(privateKey_);

    // 设置协议版本
    config.setProtocol(QSsl::SecureProtocols);

    // 设置验证模式
    if (isServer) {
        // 服务器端通常不验证客户端证书
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
    } else {
        // 客户端根据配置验证服务器证书
        switch (verifyMode_) {
            case TLSVerifyMode::NONE:
                config.setPeerVerifyMode(QSslSocket::VerifyNone);
                break;
            case TLSVerifyMode::OPTIONAL:
                config.setPeerVerifyMode(QSslSocket::QueryPeer);
                break;
            case TLSVerifyMode::REQUIRED:
            case TLSVerifyMode::FINGERPRINT:
                config.setPeerVerifyMode(QSslSocket::VerifyPeer);
                break;
        }
    }

    return config;
}

QString TLSConfig::certificateFingerprint() const {
    if (certificate_.isNull()) {
        return QString();
    }
    return CertificateGenerator::getCertificateFingerprint(certificate_);
}

} // namespace qindb
