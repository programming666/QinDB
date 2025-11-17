#ifndef QINDB_PERMISSION_MANAGER_H  // 防止重复包含的头文件保护宏
#define QINDB_PERMISSION_MANAGER_H

#include "common.h"          // 包含公共定义和类型
#include "buffer_pool_manager.h"  // 包含缓冲池管理器相关定义
#include "catalog.h"         // 包含目录管理相关定义
#include <QDateTime>         // Qt日期时间类
#include <QMutex>            // Qt互斥锁类
#include <QString>
#include <QStringList>       // Qt字符串列表类
#include <QVariant>          // Qt变体类型类
#include <QVector>           // Qt向量容器类
#include <vector>            // C++标准向量容器
#include <optional>          // C++17可选值类型

namespace qindb {  // 定义qindb命名空间

/**
 * @brief 权限记录结构
 * 
 * 该结构体用于存储权限相关的所有信息，包括权限ID、用户名、数据库名、
 * 表名、权限类型、是否可授予他人、授权时间和授权者等信息。
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
