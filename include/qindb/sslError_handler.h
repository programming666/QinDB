#ifndef QINDB_SSLERROR_HANDLER_H  // 防止头文件重复包含
#define QINDB_SSLERROR_HANDLER_H

#include <QSslError>      // Qt SSL错误相关类
#include <QSslCertificate> // Qt SSL证书相关类
#include <QString>         // Qt字符串类
#include <QList>          // Qt列表容器类

namespace qindb {  // 定义命名空间qindb

/**
 * @brief SSL错误处理器 - 统一管理SSL错误处理逻辑
 * 该类提供了SSL证书验证过程中的错误处理功能，包括错误分类、过滤和描述等
 */
class SSLErrorHandler {
public:
    /**
     * @brief SSL错误严重级别枚举
     * 定义了三种错误级别：可忽略、警告和严重
     */
    enum class ErrorSeverity {
        IGNORABLE,      // 可忽略(如自签名)
        WARNING,        // 警告(如证书即将过期)
        CRITICAL        // 严重(如证书无效)
    };

    /**
     * @brief 判断是否应该忽略SSL错误
     * @param error SSL错误
     * @param allowSelfSigned 是否允许自签名证书
     * @return true表示可以忽略该错误
     */
    static bool shouldIgnoreError(const QSslError& error, bool allowSelfSigned);

    /**
     * @brief 过滤可忽略的SSL错误
     * @param errors 错误列表
     * @param allowSelfSigned 是否允许自签名证书
     * @return 过滤后的错误列表(只包含严重错误)
     */
    static QList<QSslError> filterIgnorableErrors(const QList<QSslError>& errors,
                                                   bool allowSelfSigned);

    /**
     * @brief 获取错误的严重级别
     * @param error SSL错误
     * @param allowSelfSigned 是否允许自签名证书
     * @return 错误严重级别
     */
    static ErrorSeverity getErrorSeverity(const QSslError& error, bool allowSelfSigned);

    /**
     * @brief 获取错误类型的描述(友好的错误消息)
     * @param error SSL错误
     * @return 错误描述
     */
    static QString getErrorDescription(const QSslError& error);

    /**
     * @brief 获取错误的建议操作
     * @param error SSL错误
     * @return 建议的操作描述
     */
    static QString getSuggestedAction(const QSslError& error);

    /**
     * @brief 检查是否为自签名证书相关错误
     */
    static bool isSelfSignedError(const QSslError& error);

    /**
     * @brief 检查是否为严重错误
     */
    static bool isCriticalError(const QSslError& error);

    /**
     * @brief 验证证书有效期
     */
    static bool validateCertificateValidity(const QSslCertificate& cert, QString* errorMessage = nullptr);

    /**
     * @brief 获取证书验证错误描述
     */
    static QString getCertificateValidationError(const QSslCertificate& cert);
};

} // namespace qindb

#endif // QINDB_SSLERROR_HANDLER_H