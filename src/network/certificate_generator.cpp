#include "qindb/certificate_generator.h"
#include "qindb/logger.h"
#include <QFile>
#include <QCryptographicHash>
#include <QDateTime>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <QUuid>
#elif defined(__linux__)
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/bn.h>
#endif

namespace qindb {

QPair<QSslCertificate, QSslKey> CertificateGenerator::generateSelfSignedCertificate(
    const QString& commonName,
    const QString& organization,
    int validityDays)
{
    LOG_INFO(QString("Generating self-signed certificate for CN=%1, O=%2, validity=%3 days")
        .arg(commonName).arg(organization).arg(validityDays));

#ifdef _WIN32
    auto toW = [](const QString& s) { return std::wstring(s.toStdWString()); };

    HCRYPTPROV hProv = NULL;
    HCRYPTKEY hKey = NULL;
    PCCERT_CONTEXT pCert = NULL;

    QString container = QUuid::createUuid().toString(QUuid::WithoutBraces);
    std::wstring wContainer = toW(container);

    if (!CryptAcquireContextW(&hProv, wContainer.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_NEWKEYSET)) {
        LOG_ERROR("CryptAcquireContext failed");
        return QPair<QSslCertificate, QSslKey>();
    }

    if (!CryptGenKey(hProv, CALG_RSA_SIGN, (2048 << 16) | CRYPT_EXPORTABLE, &hKey)) {
        LOG_ERROR("CryptGenKey failed");
        CryptAcquireContextW(&hProv, wContainer.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
        return QPair<QSslCertificate, QSslKey>();
    }

    QString x500 = QString("CN=%1, O=%2").arg(commonName).arg(organization);
    std::wstring wx500 = toW(x500);
    DWORD nameLen = 0;
    if (!CertStrToNameW(X509_ASN_ENCODING, wx500.c_str(), CERT_OID_NAME_STR, NULL, nullptr, &nameLen, nullptr)) {
        LOG_ERROR("CertStrToName size query failed");
        CryptDestroyKey(hKey);
        CryptAcquireContextW(&hProv, wContainer.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
        return QPair<QSslCertificate, QSslKey>();
    }
    QByteArray nameBuf;
    nameBuf.resize(nameLen);
    if (!CertStrToNameW(X509_ASN_ENCODING, wx500.c_str(), CERT_OID_NAME_STR, NULL, reinterpret_cast<BYTE*>(nameBuf.data()), &nameLen, nullptr)) {
        LOG_ERROR("CertStrToName encode failed");
        CryptDestroyKey(hKey);
        CryptAcquireContextW(&hProv, wContainer.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
        return QPair<QSslCertificate, QSslKey>();
    }
    CERT_NAME_BLOB subject = { nameLen, reinterpret_cast<BYTE*>(nameBuf.data()) };

    CRYPT_KEY_PROV_INFO keyProvInfo = {};
    keyProvInfo.pwszContainerName = const_cast<wchar_t*>(wContainer.c_str());
    keyProvInfo.pwszProvName = const_cast<wchar_t*>(MS_ENH_RSA_AES_PROV_W);
    keyProvInfo.dwProvType = PROV_RSA_AES;
    keyProvInfo.dwKeySpec = AT_SIGNATURE;

    CRYPT_ALGORITHM_IDENTIFIER sigAlg = {};
    sigAlg.pszObjId = const_cast<char*>("1.2.840.113549.1.1.11");

    SYSTEMTIME startTime = {};
    SYSTEMTIME endTime = {};
    QDateTime now = QDateTime::currentDateTimeUtc();
    auto wStart = now.addSecs(-60).toUTC().toString("yyyy-MM-ddTHH:mm:ss");
    auto wEnd = now.addDays(validityDays).toUTC().toString("yyyy-MM-ddTHH:mm:ss");
    FILETIME ftStart = {};
    FILETIME ftEnd = {};
    QDateTime startQt = now.addSecs(-60);
    QDateTime endQt = now.addDays(validityDays);
    ULONGLONG startTicks = 116444736000000000ULL + static_cast<ULONGLONG>(startQt.toSecsSinceEpoch()) * 10000000ULL;
    ULONGLONG endTicks = 116444736000000000ULL + static_cast<ULONGLONG>(endQt.toSecsSinceEpoch()) * 10000000ULL;
    ftStart.dwLowDateTime = static_cast<DWORD>(startTicks & 0xFFFFFFFFULL);
    ftStart.dwHighDateTime = static_cast<DWORD>((startTicks >> 32) & 0xFFFFFFFFULL);
    ftEnd.dwLowDateTime = static_cast<DWORD>(endTicks & 0xFFFFFFFFULL);
    ftEnd.dwHighDateTime = static_cast<DWORD>((endTicks >> 32) & 0xFFFFFFFFULL);
    FileTimeToSystemTime(&ftStart, &startTime);
    FileTimeToSystemTime(&ftEnd, &endTime);

    pCert = CertCreateSelfSignCertificate(hProv, &subject, 0, &keyProvInfo, &sigAlg, &startTime, &endTime, nullptr);
    if (!pCert) {
        LOG_ERROR("CertCreateSelfSignCertificate failed");
        CryptDestroyKey(hKey);
        CryptAcquireContextW(&hProv, wContainer.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
        return QPair<QSslCertificate, QSslKey>();
    }

    DWORD blobLen = 0;
    if (!CryptExportKey(hKey, 0, PRIVATEKEYBLOB, 0, nullptr, &blobLen)) {
        LOG_ERROR("CryptExportKey size query failed");
        CertFreeCertificateContext(pCert);
        CryptDestroyKey(hKey);
        CryptAcquireContextW(&hProv, wContainer.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
        return QPair<QSslCertificate, QSslKey>();
    }
    QByteArray blob;
    blob.resize(blobLen);
    if (!CryptExportKey(hKey, 0, PRIVATEKEYBLOB, 0, reinterpret_cast<BYTE*>(blob.data()), &blobLen)) {
        LOG_ERROR("CryptExportKey failed");
        CertFreeCertificateContext(pCert);
        CryptDestroyKey(hKey);
        CryptAcquireContextW(&hProv, wContainer.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
        return QPair<QSslCertificate, QSslKey>();
    }

    auto be = [](const QByteArray& le) {
        QByteArray r = le;
        std::reverse(r.begin(), r.end());
        int i = 0;
        while (i < r.size() && r[i] == '\0') i++;
        return r.mid(i);
    };
    auto derLen = [](int len) {
        QByteArray out;
        if (len < 128) { out.append(char(len)); return out; }
        QByteArray buf;
        int t = len;
        while (t > 0) { buf.prepend(char(t & 0xFF)); t >>= 8; }
        out.append(char(0x80 | buf.size()));
        out += buf;
        return out;
    };
    auto derInt = [&](const QByteArray& in) {
        QByteArray v = in;
        int i = 0;
        while (i < v.size() && v[i] == '\0') i++;
        v = v.mid(i);
        if (v.isEmpty()) v = QByteArray(1, '\0');
        if ((static_cast<unsigned char>(v[0]) & 0x80) != 0) v.prepend('\0');
        QByteArray out;
        out.append(char(0x02));
        out += derLen(v.size());
        out += v;
        return out;
    };
    auto derSeq = [&](const QList<QByteArray>& elems) {
        QByteArray content;
        for (const auto& e : elems) content += e;
        QByteArray out;
        out.append(char(0x30));
        out += derLen(content.size());
        out += content;
        return out;
    };
    auto intFromDword = [&](DWORD v) {
        QByteArray b;
        DWORD t = v;
        while (t > 0) { b.prepend(char(t & 0xFF)); t >>= 8; }
        if (b.isEmpty()) b.append(char(0));
        return b;
    };

    const BYTE* p = reinterpret_cast<const BYTE*>(blob.constData());
    const BLOBHEADER* hdr = reinterpret_cast<const BLOBHEADER*>(p);
    p += sizeof(BLOBHEADER);
    const RSAPUBKEY* rsa = reinterpret_cast<const RSAPUBKEY*>(p);
    p += sizeof(RSAPUBKEY);
    int len = rsa->bitlen / 8;
    QByteArray modulus(reinterpret_cast<const char*>(p), len); p += len;
    QByteArray prime1(reinterpret_cast<const char*>(p), len/2); p += len/2;
    QByteArray prime2(reinterpret_cast<const char*>(p), len/2); p += len/2;
    QByteArray exp1(reinterpret_cast<const char*>(p), len/2); p += len/2;
    QByteArray exp2(reinterpret_cast<const char*>(p), len/2); p += len/2;
    QByteArray coeff(reinterpret_cast<const char*>(p), len/2); p += len/2;
    QByteArray privExp(reinterpret_cast<const char*>(p), len);

    QByteArray der = derSeq({
        derInt(QByteArray(1, '\0')),
        derInt(be(modulus)),
        derInt(intFromDword(rsa->pubexp)),
        derInt(be(privExp)),
        derInt(be(prime1)),
        derInt(be(prime2)),
        derInt(be(exp1)),
        derInt(be(exp2)),
        derInt(be(coeff))
    });

    QSslKey privateKey(der, QSsl::Rsa, QSsl::Der, QSsl::PrivateKey);
    QSslCertificate cert(QByteArray(reinterpret_cast<const char*>(pCert->pbCertEncoded), pCert->cbCertEncoded), QSsl::Der);

    CertFreeCertificateContext(pCert);
    CryptDestroyKey(hKey);
    CryptAcquireContextW(&hProv, wContainer.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);

    if (privateKey.isNull() || cert.isNull()) {
        LOG_ERROR("Generated certificate or key is null");
        return QPair<QSslCertificate, QSslKey>();
    }

    LOG_INFO("Self-signed certificate generated successfully");
    LOG_INFO(QString("Certificate fingerprint: %1").arg(getCertificateFingerprint(cert)));

    return QPair<QSslCertificate, QSslKey>(cert, privateKey);
#elif defined(__linux__)
    EVP_PKEY* pkey = EVP_PKEY_new();
    RSA* rsa = RSA_new();
    BIGNUM* e = BN_new();
    BN_set_word(e, RSA_F4);
    RSA_generate_key_ex(rsa, 2048, e, nullptr);
    EVP_PKEY_set1_RSA(pkey, rsa);

    X509* x509 = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), static_cast<long>(validityDays) * 24L * 60L * 60L);
    X509_set_pubkey(x509, pkey);
    X509_NAME* name = X509_get_subject_name(x509);
    QByteArray cn = commonName.toUtf8();
    QByteArray org = organization.toUtf8();
    X509_NAME_add_entry_by_NID(name, NID_commonName, MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(cn.constData()), cn.size(), -1, 0);
    X509_NAME_add_entry_by_NID(name, NID_organizationName, MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(org.constData()), org.size(), -1, 0);
    X509_set_issuer_name(x509, name);
    X509_sign(x509, pkey, EVP_sha256());

    BIO* certBio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(certBio, x509);
    char* certDataPtr = nullptr;
    long certLen = BIO_get_mem_data(certBio, &certDataPtr);
    QByteArray certPem(certDataPtr, certLen);

    BIO* keyBio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(keyBio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    char* keyDataPtr = nullptr;
    long keyLen = BIO_get_mem_data(keyBio, &keyDataPtr);
    QByteArray keyPem(keyDataPtr, keyLen);

    QSslCertificate cert(certPem, QSsl::Pem);
    QSslKey privateKey(keyPem, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);

    BIO_free(certBio);
    BIO_free(keyBio);
    X509_free(x509);
    EVP_PKEY_free(pkey);
    RSA_free(rsa);
    BN_free(e);

    if (privateKey.isNull() || cert.isNull()) {
        LOG_ERROR("Generated certificate or key is null");
        return QPair<QSslCertificate, QSslKey>();
    }

    LOG_INFO("Self-signed certificate generated successfully");
    LOG_INFO(QString("Certificate fingerprint: %1").arg(getCertificateFingerprint(cert)));

    return QPair<QSslCertificate, QSslKey>(cert, privateKey);
#else
    LOG_ERROR("Certificate generation not supported on this platform without external commands");
    return QPair<QSslCertificate, QSslKey>();
#endif
}

QSslKey CertificateGenerator::generateRSAKey(int keySize) {
    return QSslKey();
}

QSslCertificate CertificateGenerator::createX509Certificate(
    const QSslKey& publicKey,
    const QSslKey& privateKey,
    const QString& commonName,
    const QString& organization,
    int validityDays)
{
    return QSslCertificate();
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
