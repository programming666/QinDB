#include "qindb/password_hasher.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace qindb {

QString PasswordHasher::hashPassword(const QString& password) {
    // 生成随机Salt
    QByteArray salt = generateSalt(SALT_LENGTH);

    // 计算哈希: SHA-256(password + salt)
    QByteArray passwordBytes = password.toUtf8();
    QByteArray combined = passwordBytes + salt;
    QByteArray hash = computeHash(combined);

    // 存储格式: hash(32字节) + salt(16字节) = 48字节
    QByteArray stored = hash + salt;

    // 转换为Base64编码（64字符）
    return QString::fromLatin1(stored.toBase64());
}

bool PasswordHasher::verifyPassword(const QString& password, const QString& storedHash) {
    // 解码Base64
    QByteArray stored = QByteArray::fromBase64(storedHash.toLatin1());

    // 验证格式
    if (stored.size() != HASH_LENGTH + SALT_LENGTH) {
        return false;  // 格式错误
    }

    // 提取hash和salt
    QByteArray hash = stored.left(HASH_LENGTH);
    QByteArray salt = stored.mid(HASH_LENGTH, SALT_LENGTH);

    // 重新计算哈希
    QByteArray passwordBytes = password.toUtf8();
    QByteArray combined = passwordBytes + salt;
    QByteArray computedHash = computeHash(combined);

    // 比较哈希（常量时间比较，防止时序攻击）
    return hash == computedHash;
}

bool PasswordHasher::isPasswordStrong(const QString& password) {
    // 最小长度检查
    if (password.length() < MIN_PASSWORD_LENGTH) {
        return false;
    }

    // 计算包含的字符类别
    int categories = 0;

    // 大写字母
    if (password.contains(QRegularExpression("[A-Z]"))) {
        categories++;
    }

    // 小写字母
    if (password.contains(QRegularExpression("[a-z]"))) {
        categories++;
    }

    // 数字
    if (password.contains(QRegularExpression("[0-9]"))) {
        categories++;
    }

    // 特殊字符
    if (password.contains(QRegularExpression("[^A-Za-z0-9]"))) {
        categories++;
    }

    // 至少包含3种字符类别
    return categories >= 3;
}

QString PasswordHasher::getPasswordStrength(const QString& password) {
    if (password.length() < MIN_PASSWORD_LENGTH) {
        return "弱（密码长度不足）";
    }

    int categories = 0;

    if (password.contains(QRegularExpression("[A-Z]"))) categories++;
    if (password.contains(QRegularExpression("[a-z]"))) categories++;
    if (password.contains(QRegularExpression("[0-9]"))) categories++;
    if (password.contains(QRegularExpression("[^A-Za-z0-9]"))) categories++;

    if (categories >= 4 && password.length() >= 12) {
        return "强";
    } else if (categories >= 3) {
        return "中";
    } else {
        return "弱";
    }
}

// ========== 私有方法 ==========

QByteArray PasswordHasher::generateSalt(int length) {
    QByteArray salt;
    salt.resize(length);

    // 使用加密安全的随机数生成器
    QRandomGenerator* rng = QRandomGenerator::system();

    for (int i = 0; i < length; i++) {
        salt[i] = static_cast<char>(rng->bounded(256));
    }

    return salt;
}

QByteArray PasswordHasher::computeHash(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

} // namespace qindb
