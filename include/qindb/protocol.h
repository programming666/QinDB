#ifndef QINDB_PROTOCOL_H
#define QINDB_PROTOCOL_H

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QVariant>
#include <QtCore/QVector>
#include <cstdint>

namespace qindb {

/**
 * @brief 网络协议版本
 */
constexpr uint16_t PROTOCOL_VERSION = 1;

/**
 * @brief 默认服务器端口
 */
constexpr uint16_t DEFAULT_PORT = 24678;

/**
 * @brief 默认 SSL 端口
 */
constexpr uint16_t DEFAULT_SSL_PORT = 5433;

/**
 * @brief 消息类型枚举
 */
enum class MessageType : uint8_t {
    // 认证相关
    AUTH_REQUEST       = 0x01,
    AUTH_RESPONSE      = 0x02,

    // 查询相关
    QUERY_REQUEST      = 0x10,
    QUERY_RESPONSE     = 0x11,

    // 错误处理
    ERROR_RESPONSE     = 0x20,

    // 连接管理
    PING               = 0x30,
    PONG               = 0x31,
    DISCONNECT         = 0x32,

    // 事务控制
    BEGIN_TXN          = 0x40,
    COMMIT_TXN         = 0x41,
    ROLLBACK_TXN       = 0x42,

    // 预留
    RESERVED           = 0xFF
};

/**
 * @brief 认证响应状态码
 */
enum class AuthStatus : uint8_t {
    SUCCESS            = 0x00,  // 认证成功
    AUTH_FAILED        = 0x01,  // 认证失败
    DATABASE_NOT_FOUND = 0x02,  // 数据库不存在
    PERMISSION_DENIED  = 0x03   // 权限不足
};

/**
 * @brief 查询响应状态码
 */
enum class QueryStatus : uint8_t {
    SUCCESS            = 0x00,  // 执行成功
    SYNTAX_ERROR       = 0x01,  // 语法错误
    RUNTIME_ERROR      = 0x02,  // 运行时错误
    PERMISSION_ERROR   = 0x03   // 权限错误
};

/**
 * @brief 查询结果类型
 */
enum class ResultType : uint8_t {
    EMPTY              = 0x00,  // 空结果（DDL/DML 无返回）
    TABLE_DATA         = 0x01,  // 表格数据（SELECT）
    SINGLE_VALUE       = 0x02   // 单值（COUNT, SUM 等）
};

/**
 * @brief 列定义
 */
struct ColumnInfo {
    QString name;       // 列名
    uint8_t type;       // 列类型（对应 DataType 枚举）

    ColumnInfo() : type(0) {}
    ColumnInfo(const QString& n, uint8_t t) : name(n), type(t) {}
};

/**
 * @brief 查询响应结果集
 */
struct QueryResponse {
    QueryStatus status;           // 状态码
    ResultType resultType;        // 结果类型
    uint64_t rowsAffected;        // 影响的行数
    QVector<ColumnInfo> columns;  // 列定义
    QVector<QVector<QVariant>> rows;  // 行数据

    QueryResponse()
        : status(QueryStatus::SUCCESS)
        , resultType(ResultType::EMPTY)
        , rowsAffected(0) {}
};

/**
 * @brief AUTH_REQUEST 消息数据
 */
struct AuthRequest {
    uint16_t protocolVersion;  // 协议版本
    QString username;          // 用户名
    QString password;          // 密码
    QString database;          // 数据库名

    AuthRequest()
        : protocolVersion(PROTOCOL_VERSION) {}
};

/**
 * @brief AUTH_RESPONSE 消息数据
 */
struct AuthResponse {
    AuthStatus status;    // 状态码
    uint64_t sessionId;   // 会话 ID
    QString message;      // 消息

    AuthResponse()
        : status(AuthStatus::SUCCESS)
        , sessionId(0) {}
};

/**
 * @brief QUERY_REQUEST 消息数据
 */
struct QueryRequest {
    uint64_t sessionId;   // 会话 ID
    QString sql;          // SQL 语句

    QueryRequest()
        : sessionId(0) {}
};

/**
 * @brief ERROR_RESPONSE 消息数据
 */
struct ErrorResponse {
    uint32_t errorCode;   // 错误码
    QString message;      // 错误消息
    QString detail;       // 详细信息

    ErrorResponse()
        : errorCode(0) {}
};

/**
 * @brief 网络错误码定义
 */
namespace NetworkErrorCode {
    constexpr uint32_t PROTOCOL_ERROR        = 1000;  // 协议格式错误
    constexpr uint32_t VERSION_MISMATCH      = 1001;  // 协议版本不匹配
    constexpr uint32_t INVALID_MESSAGE       = 1002;  // 无效的消息类型
    constexpr uint32_t AUTH_FAILED           = 2000;  // 认证失败
    constexpr uint32_t SESSION_EXPIRED       = 2001;  // 会话过期
    constexpr uint32_t PERMISSION_DENIED     = 2002;  // 权限不足
    constexpr uint32_t SYNTAX_ERROR          = 3000;  // SQL 语法错误
    constexpr uint32_t RUNTIME_ERROR         = 3001;  // SQL 运行时错误
    constexpr uint32_t CONSTRAINT_VIOLATION  = 3002;  // 约束违反
    constexpr uint32_t CONNECTION_LOST       = 4000;  // 连接丢失
    constexpr uint32_t TIMEOUT               = 4001;  // 操作超时
}

} // namespace qindb

#endif // QINDB_PROTOCOL_H
