#include "qindb/password_hasher.h"  // 包含密码哈希器类的头文件
#include "qindb/argon2id.h"         // 包含Argon2id实现
#include <QCryptographicHash>       // 包含Qt加密哈希功能（用于向后兼容）
#include <QRandomGenerator>         // 包含Qt随机数生成器
#include <QRegularExpression>       // 包含Qt正则表达式支持

namespace qindb {  // 定义qindb命名空间

/**
 * @brief 对密码进行哈希处理（使用Argon2id）
 * @param password 要哈希的原始密码
 * @return 返回Argon2id编码的哈希字符串
 */
QString PasswordHasher::hashPassword(const QString& password) {
    return hashPasswordWithParams(password, DEFAULT_MEMORY_COST,
                                 DEFAULT_TIME_COST, DEFAULT_PARALLELISM);
}

QString PasswordHasher::hashPasswordWithParams(const QString& password,
                                              uint32_t memoryCostKB,
                                              uint32_t timeCost,
                                              uint32_t parallelism) {
    // 生成随机Salt
    QByteArray salt = generateSalt(SALT_LENGTH);
    
    // 配置Argon2id参数
    Argon2id::Parameters params;
    params.memoryCost = memoryCostKB;
    params.timeCost = timeCost;
    params.parallelism = parallelism;
    params.hashLength = 32;
    params.saltLength = SALT_LENGTH;
    
    // 使用Argon2id生成哈希
    QByteArray passwordBytes = password.toUtf8();
    return Argon2id::hashEncoded(passwordBytes, salt, params);
}

bool PasswordHasher::verifyPassword(const QString& password, const QString& storedHash) {
    // 检查是否是Argon2id格式
    if (storedHash.startsWith("$argon2id$")) {
        // 使用Argon2id验证
        QByteArray passwordBytes = password.toUtf8();
        return Argon2id::verify(passwordBytes, storedHash);
    }
    
    // 向后兼容：支持旧的SHA-256格式
    // 这允许平滑迁移，旧密码仍可验证
    QByteArray stored = QByteArray::fromBase64(storedHash.toLatin1());
    
    // 验证旧格式（48字节：32字节hash + 16字节salt）
    if (stored.size() == 48) {
        QByteArray hash = stored.left(32);
        QByteArray salt = stored.mid(32, 16);
        
        // 重新计算SHA-256哈希
        QByteArray passwordBytes = password.toUtf8();
        QByteArray combined = passwordBytes + salt;
        QByteArray computedHash = QCryptographicHash::hash(combined, QCryptographicHash::Sha256);
        
        return hash == computedHash;
    }
    
    return false;
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

} // namespace qindb
