#include "qindb/certificate_generator.h"
#include "qindb/logger.h"
#include <QFile>
#include <QCryptographicHash>
#include <QProcess>
#include <QTemporaryFile>
#include <QDateTime>

namespace qindb {

QPair<QSslCertificate, QSslKey> CertificateGenerator::generateSelfSignedCertificate(
    const QString& commonName,
    const QString& organization,
    int validityDays)
{
    LOG_INFO(QString("Generating self-signed certificate for CN=%1, O=%2, validity=%3 days")
        .arg(commonName).arg(organization).arg(validityDays));

    // 生成私钥
    QSslKey privateKey = generateRSAKey();
    if (privateKey.isNull()) {
        LOG_ERROR("Failed to generate RSA key");
        return QPair<QSslCertificate, QSslKey>();
    }

    // 创建证书
    QSslCertificate cert = createX509Certificate(privateKey, privateKey, commonName, organization, validityDays);
    if (cert.isNull()) {
        LOG_ERROR("Failed to create X.509 certificate");
        return QPair<QSslCertificate, QSslKey>();
    }

    LOG_INFO("Self-signed certificate generated successfully");
    LOG_INFO(QString("Certificate fingerprint: %1").arg(getCertificateFingerprint(cert)));

    return QPair<QSslCertificate, QSslKey>(cert, privateKey);
}

QSslKey CertificateGenerator::generateRSAKey(int keySize) {
    LOG_INFO(QString("Generating RSA key pair (size=%1)").arg(keySize));

    // 使用openssl命令生成密钥
    QProcess process;
    process.start("openssl", QStringList()
        << "genrsa"
        << QString::number(keySize));

    if (!process.waitForStarted()) {
        LOG_WARN("OpenSSL not found, trying alternative method...");
        // 后备方案：返回空密钥，调用者需要使用预生成的证书
        return QSslKey();
    }

    if (!process.waitForFinished(30000)) { // 30秒超时
        LOG_ERROR("RSA key generation timed out");
        return QSslKey();
    }

    QByteArray keyData = process.readAllStandardOutput();
    QSslKey key(keyData, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);

    if (key.isNull()) {
        LOG_ERROR("Failed to parse generated RSA key");
    } else {
        LOG_INFO("RSA key generated successfully");
    }

    return key;
}

QSslCertificate CertificateGenerator::createX509Certificate(
    const QSslKey& publicKey,
    const QSslKey& privateKey,
    const QString& commonName,
    const QString& organization,
    int validityDays)
{
    LOG_INFO("Creating X.509 certificate");

    // 保存私钥到临时文件
    QTemporaryFile keyFile;
    if (!keyFile.open()) {
        LOG_ERROR("Failed to create temporary key file");
        return QSslCertificate();
    }
    keyFile.write(privateKey.toPem());
    keyFile.flush();

    // 创建配置文件
    QTemporaryFile configFile;
    if (!configFile.open()) {
        LOG_ERROR("Failed to create temporary config file");
        return QSslCertificate();
    }

    QString config = QString(
        "[ req ]\n"
        "default_bits = 2048\n"
        "distinguished_name = req_distinguished_name\n"
        "x509_extensions = v3_ca\n"
        "prompt = no\n"
        "\n"
        "[ req_distinguished_name ]\n"
        "C = CN\n"
        "ST = Beijing\n"
        "L = Beijing\n"
        "O = %1\n"
        "CN = %2\n"
        "\n"
        "[ v3_ca ]\n"
        "subjectKeyIdentifier = hash\n"
        "authorityKeyIdentifier = keyid:always,issuer\n"
        "basicConstraints = CA:true\n"
        "keyUsage = digitalSignature, keyEncipherment\n"
    ).arg(organization).arg(commonName);

    configFile.write(config.toUtf8());
    configFile.flush();

    // 创建临时证书文件
    QTemporaryFile certFile;
    if (!certFile.open()) {
        LOG_ERROR("Failed to create temporary cert file");
        return QSslCertificate();
    }
    certFile.close();  // 关闭文件，让openssl写入

    // 使用openssl生成自签名证书
    QProcess process;
    QStringList args;
    args << "req" << "-new" << "-x509"
         << "-key" << keyFile.fileName()
         << "-out" << certFile.fileName()  // 输出到临时文件
         << "-days" << QString::number(validityDays)
         << "-config" << configFile.fileName();

    process.start("openssl", args);

    if (!process.waitForStarted()) {
        LOG_ERROR("Failed to start openssl process");
        return QSslCertificate();
    }

    if (!process.waitForFinished(30000)) {
        LOG_ERROR("Certificate generation timed out");
        return QSslCertificate();
    }

    // 读取生成的证书
    if (!certFile.open()) {
        LOG_ERROR("Failed to open generated certificate file");
        QString errorMsg = QString::fromUtf8(process.readAllStandardError());
        LOG_ERROR(QString("OpenSSL error: %1").arg(errorMsg));
        return QSslCertificate();
    }

    QByteArray certData = certFile.readAll();
    QSslCertificate cert(certData, QSsl::Pem);

    if (cert.isNull()) {
        LOG_ERROR("Failed to parse generated certificate");
        QString errorMsg = QString::fromUtf8(process.readAllStandardError());
        LOG_ERROR(QString("OpenSSL error: %1").arg(errorMsg));
    } else {
        LOG_INFO("X.509 certificate created successfully");
    }

    return cert;
}

bool CertificateGenerator::saveCertificate(const QSslCertificate& cert, const QString& certPath) {
    QFile file(certPath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR(QString("Failed to open certificate file for writing: %1").arg(certPath));
        return false;
    }

    file.write(cert.toPem());
    file.close();

    LOG_INFO(QString("Certificate saved to: %1").arg(certPath));
    return true;
}

bool CertificateGenerator::savePrivateKey(const QSslKey& key, const QString& keyPath,
                                         const QByteArray& passphrase) {
    QFile file(keyPath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR(QString("Failed to open key file for writing: %1").arg(keyPath));
        return false;
    }

    // TODO: 支持密码加密
    if (!passphrase.isEmpty()) {
        LOG_WARN("Passphrase encryption not yet implemented, saving unencrypted key");
    }

    file.write(key.toPem());
    file.close();

    LOG_INFO(QString("Private key saved to: %1").arg(keyPath));
    return true;
}

QSslCertificate CertificateGenerator::loadCertificate(const QString& certPath) {
    QFile file(certPath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR(QString("Failed to open certificate file: %1").arg(certPath));
        return QSslCertificate();
    }

    QByteArray certData = file.readAll();
    file.close();

    QSslCertificate cert(certData, QSsl::Pem);
    if (cert.isNull()) {
        LOG_ERROR(QString("Failed to parse certificate from: %1").arg(certPath));
    } else {
        LOG_INFO(QString("Certificate loaded from: %1").arg(certPath));
    }

    return cert;
}

QSslKey CertificateGenerator::loadPrivateKey(const QString& keyPath,
                                            const QByteArray& passphrase) {
    QFile file(keyPath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR(QString("Failed to open key file: %1").arg(keyPath));
        return QSslKey();
    }

    QByteArray keyData = file.readAll();
    file.close();

    QSslKey key(keyData, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey, passphrase);
    if (key.isNull()) {
        LOG_ERROR(QString("Failed to parse private key from: %1").arg(keyPath));
    } else {
        LOG_INFO(QString("Private key loaded from: %1").arg(keyPath));
    }

    return key;
}

QString CertificateGenerator::getCertificateFingerprint(const QSslCertificate& cert) {
    // 使用SHA256计算指纹
    QByteArray digest = cert.digest(QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

QString CertificateGenerator::formatFingerprint(const QString& fingerprint) {
    QString formatted;
    for (int i = 0; i < fingerprint.length(); i += 2) {
        if (i > 0) {
            formatted += ":";
        }
        formatted += fingerprint.mid(i, 2).toUpper();
    }
    return formatted;
}

} // namespace qindb
