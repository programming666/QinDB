#include "qindb/fingerprint_manager.h"
#include "qindb/certificate_generator.h"
#include "qindb/logger.h"
#include <QFile>
#include <QTextStream>
#include <QMutexLocker>
#include <QDir>

namespace qindb {

FingerprintManager::FingerprintManager(const QString& knownHostsPath)
    : knownHostsPath_(knownHostsPath)
{
    if (knownHostsPath_.isEmpty()) {
        // 默认路径: ~/.qindb/known_hosts
        QString homeDir = QDir::homePath();
        knownHostsPath_ = homeDir + "/.qindb/known_hosts";
    }

    // 确保目录存在
    QFileInfo fileInfo(knownHostsPath_);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // 加载已知指纹
    load();
}

FingerprintManager::~FingerprintManager() {
    save();
}

QString FingerprintManager::makeKey(const QString& host, uint16_t port) const {
    return QString("%1:%2").arg(host).arg(port);
}

FingerprintStatus FingerprintManager::verifyFingerprint(
    const QString& host,
    uint16_t port,
    const QSslCertificate& cert)
{
    if (cert.isNull()) {
        LOG_ERROR("Cannot verify null certificate");
        return FingerprintStatus::ERROR;
    }

    QString fingerprint = CertificateGenerator::getCertificateFingerprint(cert);
    QString key = makeKey(host, port);

    QMutexLocker locker(&mutex_);

    if (knownFingerprints_.contains(key)) {
        QString knownFingerprint = knownFingerprints_[key];
        if (knownFingerprint == fingerprint) {
            LOG_INFO(QString("Certificate fingerprint matched for %1").arg(key));
            return FingerprintStatus::TRUSTED;
        } else {
            LOG_WARN(QString("Certificate fingerprint MISMATCH for %1!").arg(key));
            LOG_WARN(QString("  Known:   %1").arg(
                CertificateGenerator::formatFingerprint(knownFingerprint)));
            LOG_WARN(QString("  Received: %1").arg(
                CertificateGenerator::formatFingerprint(fingerprint)));
            return FingerprintStatus::MISMATCH;
        }
    }

    // 未知指纹 - 需要用户确认
    LOG_INFO(QString("Unknown fingerprint for %1").arg(key));
    LOG_INFO(QString("  Fingerprint: %1").arg(
        CertificateGenerator::formatFingerprint(fingerprint)));

    // 如果设置了确认回调,调用它
    if (confirmationCallback_) {
        locker.unlock();  // 释放锁以避免死锁
        bool accepted = confirmationCallback_(
            host, port, fingerprint,
            CertificateGenerator::formatFingerprint(fingerprint)
        );
        locker.relock();

        if (accepted) {
            // 用户接受了指纹,保存它
            knownFingerprints_[key] = fingerprint;
            save();
            LOG_INFO(QString("Fingerprint accepted and saved for %1").arg(key));
            return FingerprintStatus::TRUSTED;
        } else {
            LOG_INFO(QString("Fingerprint rejected by user for %1").arg(key));
            return FingerprintStatus::UNKNOWN;
        }
    }

    return FingerprintStatus::UNKNOWN;
}

bool FingerprintManager::trustFingerprint(
    const QString& host,
    uint16_t port,
    const QString& fingerprint)
{
    QString key = makeKey(host, port);

    QMutexLocker locker(&mutex_);
    knownFingerprints_[key] = fingerprint;

    LOG_INFO(QString("Trusted fingerprint for %1: %2").arg(key).arg(
        CertificateGenerator::formatFingerprint(fingerprint)));

    return save();
}

bool FingerprintManager::removeFingerprint(const QString& host, uint16_t port) {
    QString key = makeKey(host, port);

    QMutexLocker locker(&mutex_);
    bool removed = knownFingerprints_.remove(key) > 0;

    if (removed) {
        LOG_INFO(QString("Removed fingerprint for %1").arg(key));
        save();
    }

    return removed;
}

void FingerprintManager::clearAllFingerprints() {
    QMutexLocker locker(&mutex_);
    knownFingerprints_.clear();
    save();
    LOG_INFO("Cleared all fingerprints");
}

void FingerprintManager::setConfirmationCallback(ConfirmationCallback callback) {
    QMutexLocker locker(&mutex_);
    confirmationCallback_ = callback;
}

bool FingerprintManager::save() {
    QFile file(knownHostsPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_ERROR(QString("Failed to open known_hosts file for writing: %1")
            .arg(knownHostsPath_));
        return false;
    }

    QTextStream out(&file);
    out << "# QinDB Known Hosts File\n";
    out << "# Format: host:port fingerprint\n";
    out << "#\n";

    for (auto it = knownFingerprints_.constBegin();
         it != knownFingerprints_.constEnd(); ++it)
    {
        out << it.key() << " " << it.value() << "\n";
    }

    file.close();
    LOG_DEBUG(QString("Saved %1 fingerprints to %2")
        .arg(knownFingerprints_.size()).arg(knownHostsPath_));

    return true;
}

bool FingerprintManager::load() {
    QFile file(knownHostsPath_);
    if (!file.exists()) {
        LOG_INFO(QString("Known hosts file does not exist: %1").arg(knownHostsPath_));
        return true;  // 不是错误,只是文件不存在
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_ERROR(QString("Failed to open known_hosts file for reading: %1")
            .arg(knownHostsPath_));
        return false;
    }

    QMutexLocker locker(&mutex_);
    knownFingerprints_.clear();

    QTextStream in(&file);
    int lineNumber = 0;
    while (!in.atEnd()) {
        lineNumber++;
        QString line = in.readLine().trimmed();

        // 跳过注释和空行
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        // 解析行: "host:port fingerprint"
        QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.size() != 2) {
            LOG_WARN(QString("Invalid line %1 in known_hosts file: %2")
                .arg(lineNumber).arg(line));
            continue;
        }

        QString key = parts[0];
        QString fingerprint = parts[1];
        knownFingerprints_[key] = fingerprint;
    }

    file.close();
    LOG_INFO(QString("Loaded %1 fingerprints from %2")
        .arg(knownFingerprints_.size()).arg(knownHostsPath_));

    return true;
}

QString FingerprintManager::getFingerprint(const QString& host, uint16_t port) const {
    QString key = makeKey(host, port);
    QMutexLocker locker(&mutex_);
    return knownFingerprints_.value(key, QString());
}

} // namespace qindb
