#ifndef QINDB_FINGERPRINT_MANAGER_H  // 防止头文件重复包含
#define QINDB_FINGERPRINT_MANAGER_H

#include <QString>      // QString类头文件，用于处理字符串
#include <QHash>        // QHash容器头文件，用于存储键值对
#include <QMutex>       // QMutex互斥锁头文件，用于线程同步
#include <QSslCertificate> // QSslCertificate类头文件，用于处理SSL证书
#include <functional>   // functional头文件，用于支持函数对象

namespace qindb {  // 命名空间声明，避免命名冲突

/**
 * @brief 指纹验证结果枚举类
 * 定义了证书指纹验证的四种可能状态
 */
enum class FingerprintStatus {
    TRUSTED,            // 已信任的指纹 - 之前验证并确认过的指纹
    UNKNOWN,            // 未知的指纹(需要用户确认)
    MISMATCH,           // 指纹不匹配(可能是MITM攻击)
    ERROR               // 错误
};

/**
 * @brief 指纹管理器 - 实现类似SSH的指纹验证机制
 */
class FingerprintManager {
public:
    /**
     * @brief 用户确认回调函数类型
     * @param host 主机地址
     * @param port 端口
     * @param fingerprint 证书指纹
     * @param formattedFingerprint 格式化的指纹(用于显示)
     * @return true表示用户接受该指纹, false表示拒绝
     */
    using ConfirmationCallback = std::function<bool(
        const QString& host,
        uint16_t port,
        const QString& fingerprint,
        const QString& formattedFingerprint
    )>;

    explicit FingerprintManager(const QString& knownHostsPath = QString());
    ~FingerprintManager();

    /**
     * @brief 验证证书指纹
     * @param host 主机地址
     * @param port 端口
     * @param cert 证书
     * @return 验证状态
     */
    FingerprintStatus verifyFingerprint(const QString& host, uint16_t port,
                                       const QSslCertificate& cert);

    /**
     * @brief 添加信任的指纹
     * @param host 主机地址
     * @param port 端口
     * @param fingerprint 指纹
     * @return 是否成功
     */
    bool trustFingerprint(const QString& host, uint16_t port,
                         const QString& fingerprint);

    /**
     * @brief 移除指纹
     * @param host 主机地址
     * @param port 端口
     * @return 是否成功
     */
    bool removeFingerprint(const QString& host, uint16_t port);

    /**
     * @brief 清除所有指纹
     */
    void clearAllFingerprints();

    /**
     * @brief 设置用户确认回调函数
     * @param callback 回调函数
     */
    void setConfirmationCallback(ConfirmationCallback callback);

    /**
     * @brief 获取已知主机文件路径
     * @return 文件路径
     */
    QString getKnownHostsPath() const { return knownHostsPath_; }

    /**
     * @brief 保存已知主机到文件
     * @return 是否成功
     */
    bool save();

    /**
     * @brief 从文件加载已知主机
     * @return 是否成功
     */
    bool load();

    /**
     * @brief 获取主机的指纹
     * @param host 主机地址
     * @param port 端口
     * @return 指纹(如果不存在返回空字符串)
     */
    QString getFingerprint(const QString& host, uint16_t port) const;

private:
    QString makeKey(const QString& host, uint16_t port) const;

    mutable QMutex mutex_;
    QString knownHostsPath_;
    QHash<QString, QString> knownFingerprints_;  // key: "host:port", value: fingerprint
    ConfirmationCallback confirmationCallback_;
};

} // namespace qindb

#endif // QINDB_FINGERPRINT_MANAGER_H
