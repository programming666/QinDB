#ifndef QINDB_PASSWORD_HASHER_H  // 防止重复包含该头文件
#define QINDB_PASSWORD_HASHER_H

#include <QString>    // Qt字符串类头文件
#include <QByteArray> // Qt字节数组类头文件

namespace qindb {  // 定义qindb命名空间

/**
 * @brief 密码哈希工具类
 *
 * 使用 Argon2id 算法存储密码。
 * Argon2id是2015年密码哈希竞赛的获胜者，提供强大的抗GPU和抗侧信道攻击能力。
 *
 * 存储格式: $argon2id$v=19$m=65536,t=3,p=1$salt$hash
 */
class PasswordHasher {
public:
    /**
     * @brief 生成密码哈希
     * @param password 明文密码
     * @return Argon2id编码的哈希字符串
     *
     * 使用默认参数:
     * - 内存成本: 64MB
     * - 时间成本: 3次迭代
     * - 并行度: 1
     * - 哈希长度: 32字节
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

    /**
     * @brief 使用自定义参数生成密码哈希
     * @param password 明文密码
     * @param memoryCostKB 内存成本（KB）
     * @param timeCost 时间成本（迭代次数）
     * @param parallelism 并行度
     * @return Argon2id编码的哈希字符串
     */
    static QString hashPasswordWithParams(const QString& password,
                                         uint32_t memoryCostKB = 65536,
                                         uint32_t timeCost = 3,
                                         uint32_t parallelism = 1);

private:
    /**
     * @brief 生成随机Salt
     * @param length Salt长度（字节）
     * @return 随机字节数组
     */
    static QByteArray generateSalt(int length);

    static constexpr int SALT_LENGTH = 16;             // Salt长度（字节）
    static constexpr int MIN_PASSWORD_LENGTH = 8;     // 最小密码长度
    static constexpr uint32_t DEFAULT_MEMORY_COST = 65536;  // 默认64MB
    static constexpr uint32_t DEFAULT_TIME_COST = 3;        // 默认3次迭代
    static constexpr uint32_t DEFAULT_PARALLELISM = 1;      // 默认并行度1
};

} // namespace qindb

#endif // QINDB_PASSWORD_HASHER_H
