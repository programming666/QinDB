#include "qindb/permission_manager.h"
#include "qindb/logger.h"
#include "qindb/table_page.h"
#include <QDateTime>
#include <QMutexLocker>
#include <algorithm>

namespace qindb {

PermissionManager::PermissionManager(BufferPoolManager* bufferPool,
                                     Catalog* catalog,
                                     const QString& dbName)
    : bufferPool_(bufferPool)
    , catalog_(catalog)
    , databaseName_(dbName) {}

bool PermissionManager::initializePermissionSystem() {
    QMutexLocker locker(&mutex_);

    LOG_INFO(QString("Initializing permission system for database '%1'")
                 .arg(databaseName_));

    if (catalog_->tableExists(PERMISSIONS_TABLE_NAME)) {
        LOG_INFO("Permission system already initialized");
        return true;
    }

    if (!createPermissionsTable()) {
        LOG_ERROR("Failed to create sys_permissions table");
        return false;
    }

    locker.unlock();

    // 为默认管理员授予全库权限，确保管理操作可用
    grantPermission("admin", databaseName_, QString(), PermissionType::ALL, true, "system");

    LOG_INFO("Permission system initialized successfully");
    return true;
}

bool PermissionManager::grantPermission(const QString& username,
                                         const QString& databaseName,
                                         const QString& tableName,
                                         PermissionType permType,
                                         bool withGrantOption,
                                         const QString& grantedBy) {
    QMutexLocker locker(&mutex_);

    const TableDef* tableDef = getPermissionsTable();
    if (!tableDef) {
        LOG_ERROR("sys_permissions table is not available");
        return false;
    }

    if (findPermission(username, databaseName, tableName, permType).has_value()) {
        LOG_WARN(QString("Permission already exists for user '%1' on %2.%3")
                     .arg(username)
                     .arg(databaseName)
                     .arg(tableName.isEmpty() ? "*" : tableName));
        return true;
    }

    uint64_t newId = getNextPermissionId();
    const QString privilege = permissionTypeToString(permType);
    const QString grantor = grantedBy.isEmpty() ? QStringLiteral("system") : grantedBy;
    const QString dbName = databaseName.isEmpty() ? databaseName_ : databaseName;
    const QString table = tableName;
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QVector<QVariant> record;
    record.append(QVariant::fromValue(static_cast<qulonglong>(newId)));
    record.append(username);
    record.append(dbName);
    record.append(table.isEmpty() ? QVariant() : QVariant(table));
    record.append(privilege);
    record.append(withGrantOption);
    record.append(timestamp);
    record.append(grantor);

    if (!insertPermissionRecord(tableDef, record, newId)) {
        LOG_ERROR("Failed to persist permission record");
        return false;
    }

    LOG_INFO(QString("Granted %1 on %2.%3 to '%4'")
                 .arg(privilege)
                 .arg(dbName)
                 .arg(table.isEmpty() ? "*" : table)
                 .arg(username));

    return true;
}

bool PermissionManager::revokePermission(const QString& username,
                                         const QString& databaseName,
                                         const QString& tableName,
                                         PermissionType permType) {
    QMutexLocker locker(&mutex_);

    const TableDef* tableDef = getPermissionsTable();
    if (!tableDef) {
        return false;
    }

    const QString targetPrivilege = permissionTypeToString(permType);
    bool removed = false;
    PageId pageId = tableDef->firstPageId;

    while (pageId != INVALID_PAGE_ID) {
        Page* page = bufferPool_->fetchPage(pageId);
        if (!page) {
            LOG_ERROR(QString("Failed to fetch page %1 while revoking permission").arg(pageId));
            break;
        }

        QVector<QVector<QVariant>> records;
        QVector<RecordHeader> headers;
        TablePage::getAllRecords(page, tableDef, records, headers);

        for (int slot = 0; slot < records.size(); ++slot) {
            const auto& record = records[slot];
            if (record.size() < 8) {
                continue;
            }

            const QString recUser = record[1].toString();
            const QString recDb = record[2].toString();
            const QString recTable = record[3].isNull() ? QString() : record[3].toString();
            const QString recPrivilege = record[4].toString();

            if (recUser == username && recDb == databaseName &&
                recTable == tableName && recPrivilege == targetPrivilege) {
                if (TablePage::deleteRecord(page, slot, INVALID_TXN_ID)) {
                    removed = true;
                }
            }
        }

        PageId nextPage = page->getNextPageId();
        bufferPool_->unpinPage(pageId, removed);
        pageId = nextPage;
    }

    if (!removed) {
        LOG_WARN(QString("Permission not found for user '%1' on %2.%3")
                     .arg(username)
                     .arg(databaseName)
                     .arg(tableName.isEmpty() ? "*" : tableName));
    }

    return removed;
}

bool PermissionManager::revokeAllPermissions(const QString& username) {
    QMutexLocker locker(&mutex_);

    const TableDef* tableDef = getPermissionsTable();
    if (!tableDef) {
        return false;
    }

    bool removed = false;
    PageId pageId = tableDef->firstPageId;

    while (pageId != INVALID_PAGE_ID) {
        Page* page = bufferPool_->fetchPage(pageId);
        if (!page) {
            LOG_ERROR(QString("Failed to fetch page %1 while revoking all permissions")
                          .arg(pageId));
            break;
        }

        QVector<QVector<QVariant>> records;
        QVector<RecordHeader> headers;
        TablePage::getAllRecords(page, tableDef, records, headers);

        for (int slot = 0; slot < records.size(); ++slot) {
            const auto& record = records[slot];
            if (record.size() < 8) {
                continue;
            }

            if (record[1].toString() == username) {
                if (TablePage::deleteRecord(page, slot, INVALID_TXN_ID)) {
                    removed = true;
                }
            }
        }

        PageId nextPage = page->getNextPageId();
        bufferPool_->unpinPage(pageId, removed);
        pageId = nextPage;
    }

    return removed;
}

bool PermissionManager::hasPermission(const QString& username,
                                      const QString& databaseName,
                                      const QString& tableName,
                                      PermissionType permType) const {
    QMutexLocker locker(&mutex_);

    // 优先检查全库 ALL 权限
    if (findPermission(username, databaseName, QString(), PermissionType::ALL)) {
        return true;
    }

    if (!tableName.isEmpty() &&
        findPermission(username, databaseName, tableName, PermissionType::ALL)) {
        return true;
    }

    if (findPermission(username, databaseName, QString(), permType)) {
        return true;
    }

    if (!tableName.isEmpty() &&
        findPermission(username, databaseName, tableName, permType)) {
        return true;
    }

    return false;
}

bool PermissionManager::hasGrantOption(const QString& username,
                                       const QString& databaseName,
                                       const QString& tableName,
                                       PermissionType permType) const {
    QMutexLocker locker(&mutex_);
    auto perm = findPermission(username, databaseName, tableName, permType);
    return perm.has_value() && perm->withGrantOption;
}

std::vector<Permission> PermissionManager::getUserPermissions(const QString& username,
                                                              const QString& databaseName) const {
    QMutexLocker locker(&mutex_);
    std::vector<Permission> permissions;

    const TableDef* tableDef = getPermissionsTable();
    if (!tableDef) {
        return permissions;
    }

    PageId pageId = tableDef->firstPageId;
    while (pageId != INVALID_PAGE_ID) {
        Page* page = bufferPool_->fetchPage(pageId);
        if (!page) {
            break;
        }

        QVector<QVector<QVariant>> records;
        TablePage::getAllRecords(page, tableDef, records);

        for (const auto& record : records) {
            if (record.size() < 8) {
                continue;
            }

            if (record[1].toString() == username && record[2].toString() == databaseName) {
                Permission perm;
                perm.id = record[0].toULongLong();
                perm.username = record[1].toString();
                perm.databaseName = record[2].toString();
                perm.tableName = record[3].isNull() ? QString() : record[3].toString();
                perm.permissionType = stringToPermissionType(record[4].toString());
                perm.withGrantOption = record[5].toBool();
                perm.grantedAt = QDateTime::fromString(record[6].toString(), Qt::ISODate);
                perm.grantedBy = record[7].toString();
                permissions.push_back(std::move(perm));
            }
        }

        PageId nextPage = page->getNextPageId();
        bufferPool_->unpinPage(pageId, false);
        pageId = nextPage;
    }

    return permissions;
}

std::vector<Permission> PermissionManager::getTablePermissions(const QString& username,
                                                               const QString& databaseName,
                                                               const QString& tableName) const {
    QMutexLocker locker(&mutex_);
    std::vector<Permission> permissions;

    const TableDef* tableDef = getPermissionsTable();
    if (!tableDef) {
        return permissions;
    }

    PageId pageId = tableDef->firstPageId;
    while (pageId != INVALID_PAGE_ID) {
        Page* page = bufferPool_->fetchPage(pageId);
        if (!page) {
            break;
        }

        QVector<QVector<QVariant>> records;
        TablePage::getAllRecords(page, tableDef, records);

        for (const auto& record : records) {
            if (record.size() < 8) {
                continue;
            }

            if (record[1].toString() == username &&
                record[2].toString() == databaseName &&
                (tableName.isEmpty() ||
                 (record[3].isNull() ? QString() : record[3].toString()) == tableName)) {
                Permission perm;
                perm.id = record[0].toULongLong();
                perm.username = record[1].toString();
                perm.databaseName = record[2].toString();
                perm.tableName = record[3].isNull() ? QString() : record[3].toString();
                perm.permissionType = stringToPermissionType(record[4].toString());
                perm.withGrantOption = record[5].toBool();
                perm.grantedAt = QDateTime::fromString(record[6].toString(), Qt::ISODate);
                perm.grantedBy = record[7].toString();
                permissions.push_back(std::move(perm));
            }
        }

        PageId nextPage = page->getNextPageId();
        bufferPool_->unpinPage(pageId, false);
        pageId = nextPage;
    }

    return permissions;
}

std::vector<Permission> PermissionManager::getAllPermissions() const {
    QMutexLocker locker(&mutex_);
    std::vector<Permission> permissions;

    const TableDef* tableDef = getPermissionsTable();
    if (!tableDef) {
        return permissions;
    }

    PageId pageId = tableDef->firstPageId;
    while (pageId != INVALID_PAGE_ID) {
        Page* page = bufferPool_->fetchPage(pageId);
        if (!page) {
            break;
        }

        QVector<QVector<QVariant>> records;
        TablePage::getAllRecords(page, tableDef, records);

        for (const auto& record : records) {
            if (record.size() < 8) {
                continue;
            }

            Permission perm;
            perm.id = record[0].toULongLong();
            perm.username = record[1].toString();
            perm.databaseName = record[2].toString();
            perm.tableName = record[3].isNull() ? QString() : record[3].toString();
            perm.permissionType = stringToPermissionType(record[4].toString());
            perm.withGrantOption = record[5].toBool();
            perm.grantedAt = QDateTime::fromString(record[6].toString(), Qt::ISODate);
            perm.grantedBy = record[7].toString();
            permissions.push_back(std::move(perm));
        }

        PageId nextPage = page->getNextPageId();
        bufferPool_->unpinPage(pageId, false);
        pageId = nextPage;
    }

    return permissions;
}

QString PermissionManager::permissionTypeToString(PermissionType type) {
    switch (type) {
        case PermissionType::SELECT: return QStringLiteral("SELECT");
        case PermissionType::INSERT: return QStringLiteral("INSERT");
        case PermissionType::UPDATE: return QStringLiteral("UPDATE");
        case PermissionType::DELETE: return QStringLiteral("DELETE");
        case PermissionType::ALL:    return QStringLiteral("ALL");
        default:                     return QStringLiteral("UNKNOWN");
    }
}

PermissionType PermissionManager::stringToPermissionType(const QString& str) {
    const QString upper = str.toUpper();
    if (upper == "SELECT") return PermissionType::SELECT;
    if (upper == "INSERT") return PermissionType::INSERT;
    if (upper == "UPDATE") return PermissionType::UPDATE;
    if (upper == "DELETE") return PermissionType::DELETE;
    if (upper == "ALL")    return PermissionType::ALL;
    return PermissionType::SELECT;
}

bool PermissionManager::createPermissionsTable() {
    LOG_INFO("Creating sys_permissions table");

    TableDef tableDef(PERMISSIONS_TABLE_NAME);

    ColumnDef idCol;
    idCol.name = "id";
    idCol.type = DataType::BIGINT;
    idCol.primaryKey = true;
    idCol.autoIncrement = true;
    tableDef.columns.append(idCol);

    ColumnDef userCol;
    userCol.name = "username";
    userCol.type = DataType::VARCHAR;
    userCol.length = 128;
    userCol.notNull = true;
    tableDef.columns.append(userCol);

    ColumnDef dbCol;
    dbCol.name = "database_name";
    dbCol.type = DataType::VARCHAR;
    dbCol.length = 128;
    dbCol.notNull = true;
    tableDef.columns.append(dbCol);

    ColumnDef tableCol;
    tableCol.name = "table_name";
    tableCol.type = DataType::VARCHAR;
    tableCol.length = 128;
    tableDef.columns.append(tableCol);

    ColumnDef privCol;
    privCol.name = "privilege_type";
    privCol.type = DataType::VARCHAR;
    privCol.length = 32;
    privCol.notNull = true;
    tableDef.columns.append(privCol);

    ColumnDef grantOptCol;
    grantOptCol.name = "with_grant_option";
    grantOptCol.type = DataType::BOOLEAN;
    grantOptCol.notNull = true;
    tableDef.columns.append(grantOptCol);

    ColumnDef grantedAtCol;
    grantedAtCol.name = "granted_at";
    grantedAtCol.type = DataType::DATETIME;
    grantedAtCol.notNull = true;
    tableDef.columns.append(grantedAtCol);

    ColumnDef grantedByCol;
    grantedByCol.name = "granted_by";
    grantedByCol.type = DataType::VARCHAR;
    grantedByCol.length = 128;
    grantedByCol.notNull = true;
    tableDef.columns.append(grantedByCol);

    PageId firstPageId = INVALID_PAGE_ID;
    Page* firstPage = bufferPool_->newPage(&firstPageId);
    if (!firstPage) {
        LOG_ERROR("Failed to allocate first page for sys_permissions");
        return false;
    }

    TablePage::init(firstPage, firstPageId);
    bufferPool_->unpinPage(firstPageId, true);

    tableDef.firstPageId = firstPageId;
    tableDef.nextRowId = 1;

    if (!catalog_->createTable(tableDef)) {
        LOG_ERROR("Failed to register sys_permissions in catalog");
        return false;
    }

    return true;
}

const TableDef* PermissionManager::getPermissionsTable() const {
    return catalog_->getTable(PERMISSIONS_TABLE_NAME);
}

uint64_t PermissionManager::getNextPermissionId() const {
    const TableDef* tableDef = getPermissionsTable();
    if (!tableDef) {
        return 1;
    }

    uint64_t maxId = 0;
    PageId pageId = tableDef->firstPageId;

    while (pageId != INVALID_PAGE_ID) {
        Page* page = bufferPool_->fetchPage(pageId);
        if (!page) {
            break;
        }

        QVector<QVector<QVariant>> records;
        TablePage::getAllRecords(page, tableDef, records);

        for (const auto& record : records) {
            if (record.size() >= 1) {
                maxId = std::max<uint64_t>(maxId, record[0].toULongLong());
            }
        }

        PageId nextPage = page->getNextPageId();
        bufferPool_->unpinPage(pageId, false);
        pageId = nextPage;
    }

    return maxId + 1;
}

std::optional<Permission> PermissionManager::findPermission(const QString& username,
                                                            const QString& databaseName,
                                                            const QString& tableName,
                                                            PermissionType permType) const {
    const TableDef* tableDef = getPermissionsTable();
    if (!tableDef) {
        return std::nullopt;
    }

    const QString targetPrivilege = permissionTypeToString(permType);
    PageId pageId = tableDef->firstPageId;

    while (pageId != INVALID_PAGE_ID) {
        Page* page = bufferPool_->fetchPage(pageId);
        if (!page) {
            break;
        }

        QVector<QVector<QVariant>> records;
        TablePage::getAllRecords(page, tableDef, records);

        for (const auto& record : records) {
            if (record.size() < 8) {
                continue;
            }

            if (record[1].toString() == username &&
                record[2].toString() == databaseName &&
                (record[3].isNull() ? QString() : record[3].toString()) == tableName &&
                record[4].toString() == targetPrivilege) {
                Permission perm;
                perm.id = record[0].toULongLong();
                perm.username = record[1].toString();
                perm.databaseName = record[2].toString();
                perm.tableName = record[3].isNull() ? QString() : record[3].toString();
                perm.permissionType = permType;
                perm.withGrantOption = record[5].toBool();
                perm.grantedAt = QDateTime::fromString(record[6].toString(), Qt::ISODate);
                perm.grantedBy = record[7].toString();
                bufferPool_->unpinPage(pageId, false);
                return perm;
            }
        }

        PageId nextPage = page->getNextPageId();
        bufferPool_->unpinPage(pageId, false);
        pageId = nextPage;
    }

    return std::nullopt;
}

bool PermissionManager::insertPermissionRecord(const TableDef* tableDef,
                                                const QVector<QVariant>& recordValues,
                                                RowId rowId) {
    PageId pageId = tableDef->firstPageId;

    while (pageId != INVALID_PAGE_ID) {
        Page* page = bufferPool_->fetchPage(pageId);
        if (!page) {
            return false;
        }

        if (TablePage::insertRecord(page, tableDef, rowId, recordValues, INVALID_TXN_ID)) {
            bufferPool_->unpinPage(pageId, true);
            return true;
        }

        PageId nextPage = page->getNextPageId();
        bufferPool_->unpinPage(pageId, false);
        pageId = nextPage;
    }

    PageId newPageId = INVALID_PAGE_ID;
    Page* newPage = bufferPool_->newPage(&newPageId);
    if (!newPage) {
        LOG_ERROR("Failed to allocate overflow page for permissions");
        return false;
    }

    TablePage::init(newPage, newPageId);

    if (!TablePage::insertRecord(newPage, tableDef, rowId, recordValues, INVALID_TXN_ID)) {
        bufferPool_->unpinPage(newPageId, false);
        LOG_ERROR("Failed to insert permission into new page");
        return false;
    }

    // 链接到链表尾部
    PageId lastPageId = tableDef->firstPageId;
    if (lastPageId == INVALID_PAGE_ID) {
        bufferPool_->unpinPage(newPageId, true);
        return true;
    }

    while (true) {
        Page* lastPage = bufferPool_->fetchPage(lastPageId);
        if (!lastPage) {
            bufferPool_->unpinPage(newPageId, true);
            return false;
        }

        PageId next = lastPage->getNextPageId();
        if (next == INVALID_PAGE_ID) {
            lastPage->setNextPageId(newPageId);
            bufferPool_->unpinPage(lastPageId, true);
            break;
        }

        bufferPool_->unpinPage(lastPageId, false);
        lastPageId = next;
    }

    bufferPool_->unpinPage(newPageId, true);
    return true;
}

} // namespace qindb
