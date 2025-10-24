#ifndef QINDB_SYSTEM_TABLES_H
#define QINDB_SYSTEM_TABLES_H

#include <QString>

namespace qindb {

/**
 * @brief 系统表定义
 *
 * 这些表用于在数据库内部存储元数据和WAL日志，
 * 作为catalog.json和wal文件的替代方案
 */

// 系统表名称常量
namespace SystemTables {
    const QString SYS_TABLES = "sys_tables";
    const QString SYS_COLUMNS = "sys_columns";
    const QString SYS_INDEXES = "sys_indexes";
    const QString SYS_WAL_LOGS = "sys_wal_logs";
    const QString SYS_WAL_META = "sys_wal_meta";
    const QString SYS_PERMISSIONS = "sys_permissions";  // 权限表
}

/**
 * @brief sys_tables 表结构
 * 存储表的元数据
 *
 * CREATE TABLE sys_tables (
 *     table_name VARCHAR(255) PRIMARY KEY,
 *     first_page_id BIGINT,
 *     next_row_id BIGINT
 * );
 */
namespace SysTablesColumns {
    const QString TABLE_NAME = "table_name";
    const QString FIRST_PAGE_ID = "first_page_id";
    const QString NEXT_ROW_ID = "next_row_id";
}

/**
 * @brief sys_columns 表结构
 * 存储列的元数据
 *
 * CREATE TABLE sys_columns (
 *     table_name VARCHAR(255),
 *     column_name VARCHAR(255),
 *     column_order INT,
 *     data_type INT,
 *     length INT,
 *     not_null INT,
 *     primary_key INT,
 *     auto_increment INT,
 *     PRIMARY KEY (table_name, column_name)
 * );
 */
namespace SysColumnsColumns {
    const QString TABLE_NAME = "table_name";
    const QString COLUMN_NAME = "column_name";
    const QString COLUMN_ORDER = "column_order";
    const QString DATA_TYPE = "data_type";
    const QString LENGTH = "length";
    const QString NOT_NULL = "not_null";
    const QString PRIMARY_KEY = "primary_key";
    const QString AUTO_INCREMENT = "auto_increment";
}

/**
 * @brief sys_indexes 表结构
 * 存储索引的元数据
 *
 * CREATE TABLE sys_indexes (
 *     index_name VARCHAR(255) PRIMARY KEY,
 *     table_name VARCHAR(255),
 *     index_type INT,
 *     key_type INT,
 *     is_unique INT,
 *     auto_created INT,
 *     root_page_id BIGINT,
 *     columns VARCHAR(1024)
 * );
 */
namespace SysIndexesColumns {
    const QString INDEX_NAME = "index_name";
    const QString TABLE_NAME = "table_name";
    const QString INDEX_TYPE = "index_type";
    const QString KEY_TYPE = "key_type";
    const QString IS_UNIQUE = "is_unique";
    const QString AUTO_CREATED = "auto_created";
    const QString ROOT_PAGE_ID = "root_page_id";
    const QString COLUMNS = "columns";
}

/**
 * @brief sys_wal_logs 表结构
 * 存储WAL日志记录
 *
 * CREATE TABLE sys_wal_logs (
 *     lsn BIGINT PRIMARY KEY,
 *     record_type INT,
 *     txn_id BIGINT,
 *     checksum BIGINT,
 *     data_size INT,
 *     data VARCHAR(8192)
 * );
 */
namespace SysWalLogsColumns {
    const QString LSN = "lsn";
    const QString RECORD_TYPE = "record_type";
    const QString TXN_ID = "txn_id";
    const QString CHECKSUM = "checksum";
    const QString DATA_SIZE = "data_size";
    const QString DATA = "data";
}

/**
 * @brief sys_wal_meta 表结构
 * 存储WAL元数据（如当前LSN）
 *
 * CREATE TABLE sys_wal_meta (
 *     key VARCHAR(255) PRIMARY KEY,
 *     value BIGINT
 * );
 */
namespace SysWalMetaColumns {
    const QString KEY = "key";
    const QString VALUE = "value";
}

// WAL元数据键
namespace WalMetaKeys {
    const QString CURRENT_LSN = "current_lsn";
}

/**
 * @brief sys_permissions 表结构
 * 存储用户权限信息
 *
 * CREATE TABLE sys_permissions (
 *     id BIGINT PRIMARY KEY AUTO_INCREMENT,
 *     username VARCHAR(255),
 *     database_name VARCHAR(255),
 *     table_name VARCHAR(255),      -- NULL表示数据库级权限
 *     privilege_type VARCHAR(50),   -- SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, ALTER, ALL
 *     granted_at BIGINT,
 *     granted_by VARCHAR(255)
 * );
 */
namespace SysPermissionsColumns {
    const QString ID = "id";
    const QString USERNAME = "username";
    const QString DATABASE_NAME = "database_name";
    const QString TABLE_NAME = "table_name";
    const QString PRIVILEGE_TYPE = "privilege_type";
    const QString GRANTED_AT = "granted_at";
    const QString GRANTED_BY = "granted_by";
}

// 权限类型常量
namespace PrivilegeTypes {
    const QString SELECT = "SELECT";
    const QString INSERT = "INSERT";
    const QString UPDATE = "UPDATE";
    const QString DELETE_PRIV = "DELETE";  // DELETE是C++关键字，加_PRIV后缀
    const QString CREATE = "CREATE";
    const QString DROP = "DROP";
    const QString ALTER = "ALTER";
    const QString ALL = "ALL";
}

} // namespace qindb

#endif // QINDB_SYSTEM_TABLES_H
