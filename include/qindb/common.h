#ifndef QINDB_COMMON_H
#define QINDB_COMMON_H

#include <QString>
#include <QByteArray>
#include <QVector>
#include <QHash>
#include <QVariant>
#include <memory>
#include <optional>
#include <variant>
#include <cstdint>

namespace qindb {

// 数据库版本
constexpr int QINDB_VERSION_MAJOR = 1;
constexpr int QINDB_VERSION_MINOR = 3;
constexpr int QINDB_VERSION_PATCH = 0;

// 数据库魔数（用于识别持久化模式）
// 魔数格式: "QINDB" (5字节) + 模式标识（1字节） + 版本（2字节）
// 模式标识: bit1=Catalog模式, bit0=WAL模式 (0=文件, 1=数据库)
constexpr uint64_t DB_MAGIC_BASE       = 0x0000005144424E4951ULL;  // "QINDB" 左对齐
constexpr uint64_t DB_MAGIC_MODE_00    = 0x0000005144424E4951ULL;  // Catalog=文件, WAL=文件 (默认)
constexpr uint64_t DB_MAGIC_MODE_01    = 0x0001005144424E4951ULL;  // Catalog=文件, WAL=数据库
constexpr uint64_t DB_MAGIC_MODE_10    = 0x0002005144424E4951ULL;  // Catalog=数据库, WAL=文件
constexpr uint64_t DB_MAGIC_MODE_11    = 0x0003005144424E4951ULL;  // Catalog=数据库, WAL=数据库

// 计算魔数的辅助函数
inline uint64_t calculateDbMagic(bool catalogUseDb, bool walUseDb) {
    uint64_t mode = (catalogUseDb ? 0x02ULL : 0x00ULL) | (walUseDb ? 0x01ULL : 0x00ULL);
    return 0x0000005144424E4951ULL | (mode << 56);  // 模式放在最高字节
}

// 从魔数解析模式
inline void parseDbMagic(uint64_t magic, bool& catalogUseDb, bool& walUseDb) {
    uint8_t mode = static_cast<uint8_t>((magic >> 56) & 0xFF);  // 从最高字节提取
    catalogUseDb = (mode & 0x02) != 0;
    walUseDb = (mode & 0x01) != 0;
}

// 验证魔数是否有效
inline bool isValidDbMagic(uint64_t magic) {
    uint64_t base = magic & 0x00FFFFFFFFFFFFFFULL;  // 提取基础部分（去掉最高字节）
    return base == 0x0000005144424E4951ULL;
}

// 页大小 (8KB)
constexpr size_t PAGE_SIZE = 8192;

// 缓冲池默认大小 (128MB / 16K 页)
constexpr size_t DEFAULT_BUFFER_POOL_SIZE = 16384;

// 类型定义
using PageId = uint32_t;
using TransactionId = uint64_t;
using RowId = uint64_t;
using ColumnId = uint32_t;

// 无效值
constexpr PageId INVALID_PAGE_ID = 0;
constexpr TransactionId INVALID_TXN_ID = 0;
constexpr RowId INVALID_ROW_ID = 0;

// 错误码
enum class ErrorCode {
    SUCCESS = 0,
    SYNTAX_ERROR,
    SEMANTIC_ERROR,
    CONSTRAINT_VIOLATION,
    TABLE_NOT_FOUND,
    COLUMN_NOT_FOUND,
    DUPLICATE_KEY,
    INDEX_NOT_FOUND,
    TRANSACTION_ERROR,
    IO_ERROR,
    NETWORK_ERROR,
    AUTH_ERROR,
    PERMISSION_DENIED,
    INTERNAL_ERROR,
    NOT_IMPLEMENTED
};

// SQL 数据类型
enum class DataType {
    // 整数类型
    TINYINT,        // 1 字节整数 (-128 to 127)
    SMALLINT,       // 2 字节整数 (-32768 to 32767)
    MEDIUMINT,      // 3 字节整数 (-8388608 to 8388607)
    INT,            // 4 字节整数
    INTEGER,        // INT 的别名
    BIGINT,         // 8 字节整数
    SERIAL,         // 自增整数（等同于 INT AUTO_INCREMENT）
    BIGSERIAL,      // 自增大整数（等同于 BIGINT AUTO_INCREMENT）

    // 浮点类型
    FLOAT,          // 单精度浮点数（4字节）
    REAL,           // FLOAT 的别名
    DOUBLE,         // 双精度浮点数（8字节）
    DOUBLE_PRECISION, // DOUBLE 的别名
    BINARY_FLOAT,   // Oracle 二进制单精度浮点
    BINARY_DOUBLE,  // Oracle 二进制双精度浮点

    // 定点数类型
    DECIMAL,        // 定点数 DECIMAL(precision, scale)
    NUMERIC,        // DECIMAL 的别名

    // 字符串类型
    CHAR,           // 定长字符串
    VARCHAR,        // 变长字符串
    VARCHAR2,       // Oracle VARCHAR2
    NCHAR,          // Unicode 定长字符串
    NVARCHAR,       // Unicode 变长字符串
    TEXT,           // 长文本
    TINYTEXT,       // 小文本（最多 255 字符）
    MEDIUMTEXT,     // 中等文本（最多 16MB）
    LONGTEXT,       // 长文本（最多 4GB）
    NTEXT,          // Unicode 长文本
    CLOB,           // 字符大对象
    NCLOB,          // Unicode 字符大对象

    // 二进制类型
    BINARY,         // 定长二进制数据
    VARBINARY,      // 变长二进制数据
    BYTEA,          // PostgreSQL 字节数组
    BLOB,           // 二进制大对象
    TINYBLOB,       // 小型 BLOB（最多 255 字节）
    MEDIUMBLOB,     // 中型 BLOB（最多 16MB）
    LONGBLOB,       // 大型 BLOB（最多 4GB）
    IMAGE,          // SQL Server 图像类型

    // 日期时间类型
    DATE,           // 日期（YYYY-MM-DD）
    TIME,           // 时间（HH:MM:SS）
    DATETIME,       // 日期时间（YYYY-MM-DD HH:MM:SS）
    DATETIME2,      // SQL Server 高精度日期时间
    SMALLDATETIME,  // SQL Server 小日期时间
    TIMESTAMP,      // 时间戳
    TIMESTAMP_TZ,   // 带时区的时间戳
    DATETIMEOFFSET, // SQL Server 带时区偏移的日期时间

    // 布尔类型
    BOOLEAN,        // 布尔值
    BOOL,           // BOOLEAN 的别名

    // JSON 类型
    JSON,           // JSON 数据
    JSONB,          // 二进制 JSON（PostgreSQL）

    // XML 类型
    XML,            // XML 数据

    // 特殊类型
    UUID,           // 通用唯一标识符
    UNIQUEIDENTIFIER, // SQL Server UUID
    ROWID,          // Oracle ROWID
    GEOMETRY,       // 几何类型
    GEOGRAPHY,      // 地理类型
    HIERARCHYID,    // SQL Server 层次结构ID

    // 其他
    NULL_TYPE       // NULL 类型
};

/**
 * @brief 索引类型
 */
enum class IndexType : uint8_t {
    BTREE = 0,         // B+树（默认，支持范围查询和等值查询）
    HASH = 1,          // 哈希索引（仅支持等值查询，O(1)性能）
    TRIE = 2,          // TRIE树（字符串前缀查询）
    INVERTED = 3,      // 倒排索引（全文搜索）
    RTREE = 4          // R-树（空间索引）
};

/**
 * @brief 获取索引类型的字符串名称
 */
inline QString getIndexTypeName(IndexType type) {
    switch (type) {
        case IndexType::BTREE: return "BTREE";
        case IndexType::HASH: return "HASH";
        case IndexType::TRIE: return "TRIE";
        case IndexType::INVERTED: return "INVERTED";
        case IndexType::RTREE: return "RTREE";
        default: return "UNKNOWN";
    }
}

/**
 * @brief 获取数据类型的固定大小（对于定长类型）
 * @param type 数据类型
 * @return 字节数，如果是变长类型返回 0
 */
inline size_t getFixedTypeSize(DataType type) {
    switch (type) {
        case DataType::TINYINT: return 1;
        case DataType::SMALLINT: return 2;
        case DataType::MEDIUMINT: return 3;
        case DataType::INT:
        case DataType::INTEGER:
        case DataType::SERIAL: return 4;
        case DataType::BIGINT:
        case DataType::BIGSERIAL: return 8;
        case DataType::FLOAT:
        case DataType::REAL:
        case DataType::BINARY_FLOAT: return 4;
        case DataType::DOUBLE:
        case DataType::DOUBLE_PRECISION:
        case DataType::BINARY_DOUBLE: return 8;
        case DataType::DATE: return 4;      // 存储为天数
        case DataType::TIME: return 4;      // 存储为秒数
        case DataType::DATETIME:
        case DataType::DATETIME2:
        case DataType::TIMESTAMP:
        case DataType::TIMESTAMP_TZ: return 8;  // 存储为微秒时间戳
        case DataType::SMALLDATETIME: return 4;
        case DataType::BOOLEAN:
        case DataType::BOOL: return 1;
        case DataType::UUID:
        case DataType::UNIQUEIDENTIFIER: return 16;  // 128 位
        default: return 0;  // 变长类型
    }
}

/**
 * @brief 判断数据类型是否为整数类型
 */
inline bool isIntegerType(DataType type) {
    return type == DataType::TINYINT || type == DataType::SMALLINT ||
           type == DataType::MEDIUMINT || type == DataType::INT ||
           type == DataType::INTEGER || type == DataType::BIGINT ||
           type == DataType::SERIAL || type == DataType::BIGSERIAL;
}

/**
 * @brief 判断数据类型是否为浮点类型
 */
inline bool isFloatType(DataType type) {
    return type == DataType::FLOAT || type == DataType::REAL ||
           type == DataType::DOUBLE || type == DataType::DOUBLE_PRECISION ||
           type == DataType::BINARY_FLOAT || type == DataType::BINARY_DOUBLE;
}

/**
 * @brief 判断数据类型是否为数值类型
 */
inline bool isNumericType(DataType type) {
    return isIntegerType(type) || isFloatType(type) ||
           type == DataType::DECIMAL || type == DataType::NUMERIC;
}

/**
 * @brief 判断数据类型是否为字符串类型
 */
inline bool isStringType(DataType type) {
    return type == DataType::CHAR || type == DataType::VARCHAR ||
           type == DataType::VARCHAR2 || type == DataType::NCHAR ||
           type == DataType::NVARCHAR || type == DataType::TEXT ||
           type == DataType::TINYTEXT || type == DataType::MEDIUMTEXT ||
           type == DataType::LONGTEXT || type == DataType::NTEXT ||
           type == DataType::CLOB || type == DataType::NCLOB;
}

/**
 * @brief 判断数据类型是否为二进制类型
 */
inline bool isBinaryType(DataType type) {
    return type == DataType::BINARY || type == DataType::VARBINARY ||
           type == DataType::BYTEA || type == DataType::BLOB ||
           type == DataType::TINYBLOB || type == DataType::MEDIUMBLOB ||
           type == DataType::LONGBLOB || type == DataType::IMAGE;
}

/**
 * @brief 判断数据类型是否为日期时间类型
 */
inline bool isDateTimeType(DataType type) {
    return type == DataType::DATE || type == DataType::TIME ||
           type == DataType::DATETIME || type == DataType::DATETIME2 ||
           type == DataType::SMALLDATETIME || type == DataType::TIMESTAMP ||
           type == DataType::TIMESTAMP_TZ || type == DataType::DATETIMEOFFSET;
}

/**
 * @brief 获取数据类型的字符串名称
 */
inline QString getDataTypeName(DataType type) {
    switch (type) {
        case DataType::TINYINT: return "TINYINT";
        case DataType::SMALLINT: return "SMALLINT";
        case DataType::MEDIUMINT: return "MEDIUMINT";
        case DataType::INT: return "INT";
        case DataType::INTEGER: return "INTEGER";
        case DataType::BIGINT: return "BIGINT";
        case DataType::SERIAL: return "SERIAL";
        case DataType::BIGSERIAL: return "BIGSERIAL";
        case DataType::FLOAT: return "FLOAT";
        case DataType::REAL: return "REAL";
        case DataType::DOUBLE: return "DOUBLE";
        case DataType::DOUBLE_PRECISION: return "DOUBLE PRECISION";
        case DataType::BINARY_FLOAT: return "BINARY_FLOAT";
        case DataType::BINARY_DOUBLE: return "BINARY_DOUBLE";
        case DataType::DECIMAL: return "DECIMAL";
        case DataType::NUMERIC: return "NUMERIC";
        case DataType::CHAR: return "CHAR";
        case DataType::VARCHAR: return "VARCHAR";
        case DataType::VARCHAR2: return "VARCHAR2";
        case DataType::NCHAR: return "NCHAR";
        case DataType::NVARCHAR: return "NVARCHAR";
        case DataType::TEXT: return "TEXT";
        case DataType::TINYTEXT: return "TINYTEXT";
        case DataType::MEDIUMTEXT: return "MEDIUMTEXT";
        case DataType::LONGTEXT: return "LONGTEXT";
        case DataType::NTEXT: return "NTEXT";
        case DataType::CLOB: return "CLOB";
        case DataType::NCLOB: return "NCLOB";
        case DataType::BINARY: return "BINARY";
        case DataType::VARBINARY: return "VARBINARY";
        case DataType::BYTEA: return "BYTEA";
        case DataType::BLOB: return "BLOB";
        case DataType::TINYBLOB: return "TINYBLOB";
        case DataType::MEDIUMBLOB: return "MEDIUMBLOB";
        case DataType::LONGBLOB: return "LONGBLOB";
        case DataType::IMAGE: return "IMAGE";
        case DataType::DATE: return "DATE";
        case DataType::TIME: return "TIME";
        case DataType::DATETIME: return "DATETIME";
        case DataType::DATETIME2: return "DATETIME2";
        case DataType::SMALLDATETIME: return "SMALLDATETIME";
        case DataType::TIMESTAMP: return "TIMESTAMP";
        case DataType::TIMESTAMP_TZ: return "TIMESTAMP WITH TIME ZONE";
        case DataType::DATETIMEOFFSET: return "DATETIMEOFFSET";
        case DataType::BOOLEAN: return "BOOLEAN";
        case DataType::BOOL: return "BOOL";
        case DataType::JSON: return "JSON";
        case DataType::JSONB: return "JSONB";
        case DataType::XML: return "XML";
        case DataType::UUID: return "UUID";
        case DataType::UNIQUEIDENTIFIER: return "UNIQUEIDENTIFIER";
        case DataType::ROWID: return "ROWID";
        case DataType::GEOMETRY: return "GEOMETRY";
        case DataType::GEOGRAPHY: return "GEOGRAPHY";
        case DataType::HIERARCHYID: return "HIERARCHYID";
        case DataType::NULL_TYPE: return "NULL";
        default: return "UNKNOWN";
    }
}

// 权限类型枚举
enum class PermissionType {
    SELECT = 0x01,      // 查询权限
    INSERT = 0x02,      // 插入权限
    UPDATE = 0x04,      // 更新权限
    DELETE = 0x08,      // 删除权限
    CREATE = 0x10,      // 创建表权限
    DROP = 0x20,        // 删除表权限
    ALTER = 0x40,       // 修改表权限
    INDEX = 0x80,       // 索引权限
    ALL = 0xFF          // 所有权限
};

// 权限操作符重载（支持位运算）
inline PermissionType operator|(PermissionType a, PermissionType b) {
    return static_cast<PermissionType>(static_cast<int>(a) | static_cast<int>(b));
}

inline PermissionType operator&(PermissionType a, PermissionType b) {
    return static_cast<PermissionType>(static_cast<int>(a) & static_cast<int>(b));
}

inline bool hasPermission(PermissionType userPerms, PermissionType requiredPerm) {
    return (static_cast<int>(userPerms) & static_cast<int>(requiredPerm)) != 0;
}

// 权限类型转字符串
inline QString permissionTypeToString(PermissionType perm) {
    QStringList perms;
    if (hasPermission(perm, PermissionType::SELECT)) perms << "SELECT";
    if (hasPermission(perm, PermissionType::INSERT)) perms << "INSERT";
    if (hasPermission(perm, PermissionType::UPDATE)) perms << "UPDATE";
    if (hasPermission(perm, PermissionType::DELETE)) perms << "DELETE";
    if (hasPermission(perm, PermissionType::CREATE)) perms << "CREATE";
    if (hasPermission(perm, PermissionType::DROP)) perms << "DROP";
    if (hasPermission(perm, PermissionType::ALTER)) perms << "ALTER";
    if (hasPermission(perm, PermissionType::INDEX)) perms << "INDEX";
    if (perms.isEmpty()) return "NONE";
    if (static_cast<int>(perm) == static_cast<int>(PermissionType::ALL)) return "ALL";
    return perms.join(", ");
}

// 错误信息结构
struct Error {
    ErrorCode code;
    QString message;
    QString detail;

    Error(ErrorCode c = ErrorCode::SUCCESS,
          const QString& msg = "",
          const QString& det = "")
        : code(c), message(msg), detail(det) {}

    bool isSuccess() const { return code == ErrorCode::SUCCESS; }
};

// 数据库结果值类型
using Value = QVariant;

} // namespace qindb

#endif // QINDB_COMMON_H
