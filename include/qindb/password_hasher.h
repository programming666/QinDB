#ifndef QINDB_PASSWORD_HASHER_H
#define QINDB_PASSWORD_HASHER_H

#include <QString>
#include <QByteArray>

namespace qindb {

/**
 * @brief 密码哈希工具类
 *
 * 使用 SHA-256 + Salt 方案存储密码。
 * 存储格式: Base64(SHA256(password + salt) + salt)
 */
class PasswordHasher {
public:
    /**
     * @brief 生成密码哈希
     * @param password 明文密码
     * @return Base64编码的哈希字符串（64字符）
     *
     * 存储格式: hash(32字节) + salt(16字节) = 48字节
     * Base64编码后: 64字符
     */
    static QString hashPassword(const QString& password);

    /**
     * @brief 验证密码
     * @param password 待验证的明文密码
     * @param storedHash 存储的哈希字符串（来自数据库）
     * @return 密码是否匹配
     */
    static bool verifyPassword(const QString& password, const QString& storedHash);

    /**
     * @brief 验证密码强度
     * @param password 待验证的密码
     * @return 是否满足强度要求
     *
     * 强度要求:
     * - 最小长度: 8字符
     * - 包含大写、小写、数字、特殊字符中的至少3种
     */
    static bool isPasswordStrong(const QString& password);

    /**
     * @brief 获取密码强度描述
     * @param password 待评估的密码
     * @return 强度描述（弱/中/强）
     */
    static QString getPasswordStrength(const QString& password);

private:
    /**
     * @brief 生成随机Salt
     * @param length Salt长度（字节）
     * @return 随机字节数组
     */
    static QByteArray generateSalt(int length);

    /**
     * @brief 计算SHA-256哈希
     * @param data 待哈希的数据
     * @return 哈希结果（32字节）
     */
    static QByteArray computeHash(const QByteArray& data);

    static constexpr int SALT_LENGTH = 16;      // Salt长度（字节）
    static constexpr int HASH_LENGTH = 32;      // SHA-256哈希长度（字节）
    static constexpr int MIN_PASSWORD_LENGTH = 8;  // 最小密码长度
};

} // namespace qindb

#endif // QINDB_PASSWORD_HASHER_H
