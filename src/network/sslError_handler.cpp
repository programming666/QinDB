#include "qindb/ssLError_handler.h"
#include <QDateTime>

namespace qindb {

bool SSLErrorHandler::shouldIgnoreError(const QSslError& error, bool allowSelfSigned) {
    if (!allowSelfSigned) {
        return false;
    }

    // 对于自签名证书,忽略以下常见错误
    switch (error.error()) {
        case QSslError::SelfSignedCertificate:
        case QSslError::SelfSignedCertificateInChain:
        case QSslError::CertificateUntrusted:
        case QSslError::UnableToGetLocalIssuerCertificate:
        case QSslError::UnableToVerifyFirstCertificate:
            return true;
        default:
            return false;
    }
}

QList<QSslError> SSLErrorHandler::filterIgnorableErrors(const QList<QSslError>& errors,
                                                       bool allowSelfSigned) {
    QList<QSslError> criticalErrors;
    for (const auto& error : errors) {
        if (!shouldIgnoreError(error, allowSelfSigned)) {
            criticalErrors.append(error);
        }
    }
    return criticalErrors;
}

SSLErrorHandler::ErrorSeverity SSLErrorHandler::getErrorSeverity(const QSslError& error, bool allowSelfSigned) {
    // 严重错误 - 无论如何都不应该忽略
    switch (error.error()) {
        case QSslError::CertificateRevoked:
        case QSslError::CertificateRejected:
        case QSslError::CertificateBlacklisted:
        case QSslError::InvalidNotBeforeField:
        case QSslError::InvalidNotAfterField:
        case QSslError::CertificateExpired:
        case QSslError::CertificateNotYetValid:
        case QSslError::InvalidCaCertificate:
            return ErrorSeverity::CRITICAL;
        default:
            break;
    }

    // 可忽略的错误(如果允许自签名)
    if (shouldIgnoreError(error, allowSelfSigned)) {
        return ErrorSeverity::IGNORABLE;
    }

    // 其他错误视为警告
    return ErrorSeverity::WARNING;
}

QString SSLErrorHandler::getErrorDescription(const QSslError& error) {
    switch (error.error()) {
        case QSslError::NoError:
            return "No error";
        case QSslError::UnableToGetIssuerCertificate:
            return "Unable to get issuer certificate";
        case QSslError::UnableToDecryptCertificateSignature:
            return "Unable to decrypt certificate signature";
        case QSslError::UnableToDecodeIssuerPublicKey:
            return "Unable to decode issuer public key";
        case QSslError::CertificateSignatureFailed:
            return "Certificate signature failed";
        case QSslError::CertificateNotYetValid:
            return "Certificate not yet valid";
        case QSslError::CertificateExpired:
            return "Certificate expired";
        case QSslError::InvalidNotBeforeField:
            return "Invalid not before field";
        case QSslError::InvalidNotAfterField:
            return "Invalid not after field";
        case QSslError::SelfSignedCertificate:
            return "Self-signed certificate";
        case QSslError::SelfSignedCertificateInChain:
            return "Self-signed certificate in chain";
        case QSslError::UnableToGetLocalIssuerCertificate:
            return "Unable to get local issuer certificate";
        case QSslError::UnableToVerifyFirstCertificate:
            return "Unable to verify first certificate";
        case QSslError::CertificateRevoked:
            return "Certificate revoked";
        case QSslError::InvalidCaCertificate:
            return "Invalid CA certificate";
        case QSslError::PathLengthExceeded:
            return "Path length exceeded";
        case QSslError::CertificateRejected:
            return "Certificate rejected";
        case QSslError::SubjectIssuerMismatch:
            return "Subject issuer mismatch";
        case QSslError::AuthorityIssuerSerialNumberMismatch:
            return "Authority issuer serial number mismatch";
        case QSslError::NoPeerCertificate:
            return "No peer certificate";
        case QSslError::HostNameMismatch:
            return "Host name mismatch";
        case QSslError::UnspecifiedError:
            return "Unspecified error";
        case QSslError::CertificateBlacklisted:
            return "Certificate blacklisted";
        case QSslError::CertificateUntrusted:
            return "Certificate untrusted";
        default:
            return "Unknown SSL error";
    }
}

bool SSLErrorHandler::isSelfSignedError(const QSslError& error) {
    switch (error.error()) {
        case QSslError::SelfSignedCertificate:
        case QSslError::SelfSignedCertificateInChain:
        case QSslError::CertificateUntrusted:
        case QSslError::UnableToGetLocalIssuerCertificate:
        case QSslError::UnableToVerifyFirstCertificate:
            return true;
        default:
            return false;
    }
}

bool SSLErrorHandler::isCriticalError(const QSslError& error) {
    return getErrorSeverity(error, false) == ErrorSeverity::CRITICAL;
}

bool SSLErrorHandler::validateCertificateValidity(const QSslCertificate& cert, QString* errorMessage) {
    if (cert.isNull()) {
        if (errorMessage) *errorMessage = "Certificate is null";
        return false;
    }

    QDateTime currentTime = QDateTime::currentDateTime();

    // 检查证书是否过期
    if (cert.expiryDate() < currentTime) {
        if (errorMessage) *errorMessage = "Certificate has expired";
        return false;
    }

    // 检查证书是否有效
    if (cert.effectiveDate() > currentTime) {
        if (errorMessage) *errorMessage = "Certificate is not yet valid";
        return false;
    }

    return true;
}

QString SSLErrorHandler::getCertificateValidationError(const QSslCertificate& cert) {
    QString errorMsg;
    if (!validateCertificateValidity(cert, &errorMsg)) {
        return errorMsg;
    }

    // 可以添加更多的证书验证逻辑
    return QString();
}

QString SSLErrorHandler::getSuggestedAction(const QSslError& error) {
    switch (error.error()) {
        case QSslError::SelfSignedCertificate:
        case QSslError::SelfSignedCertificateInChain:
            return "Consider adding the certificate to trusted certificates or enable self-signed certificate support";

        case QSslError::CertificateExpired:
            return "Renew the certificate with a valid expiration date";

        case QSslError::CertificateNotYetValid:
            return "Check system time or wait until certificate becomes valid";

        case QSslError::CertificateRevoked:
            return "Certificate has been revoked, obtain a new certificate";

        case QSslError::CertificateUntrusted:
            return "Add the certificate authority to trusted CAs";

        case QSslError::HostNameMismatch:
            return "Ensure the certificate's Common Name matches the server hostname";

        case QSslError::UnableToGetLocalIssuerCertificate:
        case QSslError::UnableToVerifyFirstCertificate:
            return "Install the certificate chain or enable self-signed certificate support";

        case QSslError::InvalidCaCertificate:
            return "Check the CA certificate validity and install proper CA certificates";

        case QSslError::NoPeerCertificate:
            return "Server must provide a valid certificate";

        default:
            return "Review SSL/TLS configuration and certificate setup";
    }
}

} // namespace qindb