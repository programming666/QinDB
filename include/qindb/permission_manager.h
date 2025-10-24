#ifndef QINDB_PERMISSION_MANAGER_H
#define QINDB_PERMISSION_MANAGER_H

#include "common.h"
#include "buffer_pool_manager.h"
#include "catalog.h"
#include <QDateTime>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <vector>
#include <optional>

namespace qindb {

/**
 * @brief 权限记录结构
 */
struct Permission {
    uint64_t id;                    // 权限ID
    QString username;               // 用户名
    QString databaseName;           // 数据库名
    QString tableName;              // 表名（空表示数据库级权限）
    PermissionType permissionType;  // 权限类型
    bool withGrantOption;           // 是否可以授予他人
    QDateTime grantedAt;            // 授权时间
    QString grantedBy;              // 授权者

    Permission()
        : id(0)
        , permissionType(PermissionType::SELECT)
        , withGrantOption(false) {}
};

/**
 * @brief 权限管理器
 *
 * 负责管理用户权限，包括授予、撤销、检查权限等操作。
 * 权限存储在系统表 sys_permissions 中。
 */
class PermissionManager {
public:
    PermissionManager(BufferPoolManager* bufferPool,
                      Catalog* catalog,
                      const QString& dbName);

    bool initializePermissionSystem();

    bool grantPermission(const QString& username,
                         const QString& databaseName,
                         const QString& tableName,
                         PermissionType permType,
                         bool withGrantOption = false,
                         const QString& grantedBy = QString());

    bool revokePermission(const QString& username,
                          const QString& databaseName,
                          const QString& tableName,
                          PermissionType permType);

    bool revokeAllPermissions(const QString& username);

    bool hasPermission(const QString& username,
                       const QString& databaseName,
                       const QString& tableName,
                       PermissionType permType) const;

    bool hasGrantOption(const QString& username,
                        const QString& databaseName,
                        const QString& tableName,
                        PermissionType permType) const;

    std::vector<Permission> getUserPermissions(const QString& username,
                                               const QString& databaseName) const;

    std::vector<Permission> getTablePermissions(const QString& username,
                                                const QString& databaseName,
                                                const QString& tableName) const;

    std::vector<Permission> getAllPermissions() const;

    static QString permissionTypeToString(PermissionType type);
    static PermissionType stringToPermissionType(const QString& str);

private:
    bool createPermissionsTable();
    const TableDef* getPermissionsTable() const;
    uint64_t getNextPermissionId() const;
    std::optional<Permission> findPermission(const QString& username,
                                             const QString& databaseName,
                                             const QString& tableName,
                                             PermissionType permType) const;
    bool insertPermissionRecord(const TableDef* tableDef,
                                const QVector<QVariant>& recordValues,
                                RowId rowId);

    BufferPoolManager* bufferPool_;  // 缓冲池管理器
    Catalog* catalog_;               // 元数据目录
    QString databaseName_;           // 数据库名称
    mutable QMutex mutex_;           // 线程安全互斥锁

    static constexpr const char* PERMISSIONS_TABLE_NAME = "sys_permissions";
};

} // namespace qindb

#endif // QINDB_PERMISSION_MANAGER_H
