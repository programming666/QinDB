#include "qindb/auth_manager.h"
#include "qindb/password_hasher.h"
#include "qindb/catalog.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include "qindb/table_page.h"
#include "qindb/logger.h"
#include <QDateTime>

namespace qindb {

AuthManager::AuthManager(Catalog* catalog,
                        BufferPoolManager* bufferPool,
                        DiskManager* diskManager)
    : catalog_(catalog)
    , bufferPool_(bufferPool)
    , diskManager_(diskManager) {
}

AuthManager::~AuthManager() = default;

// ========== 系统初始化 ==========

bool AuthManager::initializeUserSystem() {
    LOG_INFO("Initializing user authentication system...");

    // 检查用户表是否已存在
    if (catalog_->tableExists(USERS_TABLE)) {
        LOG_INFO("Users table already exists");

        // 检查是否有管理员用户
        if (getUserCount() == 0) {
            LOG_INFO("Creating default admin user...");
            return createUser("admin", "admin", true);
        }

        return true;
    }

    // 创建用户表结构
    TableDef tableDef;
    tableDef.name = USERS_TABLE;

    // id BIGINT PRIMARY KEY AUTO_INCREMENT
    ColumnDef idCol;
    idCol.name = "id";
    idCol.type = DataType::BIGINT;
    idCol.primaryKey = true;
    idCol.autoIncrement = true;
    tableDef.columns.append(idCol);

    // username VARCHAR(64) UNIQUE NOT NULL
    ColumnDef usernameCol;
    usernameCol.name = "username";
    usernameCol.type = DataType::VARCHAR;
    usernameCol.length = 64;
    usernameCol.notNull = true;
    tableDef.columns.append(usernameCol);

    // password VARCHAR(128) NOT NULL
    ColumnDef passwordCol;
    passwordCol.name = "password";
    passwordCol.type = DataType::VARCHAR;
    passwordCol.length = 128;
    passwordCol.notNull = true;
    tableDef.columns.append(passwordCol);

    // created_at BIGINT (Unix timestamp)
    ColumnDef createdCol;
    createdCol.name = "created_at";
    createdCol.type = DataType::BIGINT;
    tableDef.columns.append(createdCol);

    // updated_at BIGINT (Unix timestamp)
    ColumnDef updatedCol;
    updatedCol.name = "updated_at";
    updatedCol.type = DataType::BIGINT;
    tableDef.columns.append(updatedCol);

    // is_active BOOLEAN (使用 INT 代替,0=false, 1=true)
    ColumnDef activeCol;
    activeCol.name = "is_active";
    activeCol.type = DataType::INT;
    tableDef.columns.append(activeCol);

    // is_admin BOOLEAN (使用 INT 代替,0=false, 1=true)
    ColumnDef adminCol;
    adminCol.name = "is_admin";
    adminCol.type = DataType::INT;
    tableDef.columns.append(adminCol);

    // 分配第一个页面
    PageId firstPageId = diskManager_->allocatePage();
    tableDef.firstPageId = firstPageId;
    tableDef.nextRowId = 1;

    // 注册表到Catalog
    if (!catalog_->createTable(tableDef)) {
        LOG_ERROR("Failed to create users table");
        return false;
    }

    LOG_INFO(QString("Users table created with first page: %1").arg(firstPageId));

    // 创建默认管理员用户
    LOG_INFO("Creating default admin user...");
    if (!createUser("admin", "admin", true)) {
        LOG_ERROR("Failed to create default admin user");
        return false;
    }

    LOG_INFO("User authentication system initialized successfully");
    return true;
}

// ========== 用户管理 ==========

bool AuthManager::createUser(const QString& username,
                             const QString& password,
                             bool isAdmin) {
    LOG_INFO(QString("createUser called: username='%1', isAdmin=%2").arg(username).arg(isAdmin));

    // 验证用户名
    if (username.isEmpty() || username.length() > 64) {
        LOG_ERROR("Invalid username length");
        return false;
    }

    // 检查用户是否已存在
    LOG_INFO("Checking if user exists...");
    if (userExists(username)) {
        LOG_ERROR(QString("User '%1' already exists").arg(username));
        return false;
    }
    LOG_INFO("User does not exist, proceeding...");

    // 验证密码强度（可选，管理员账户除外）
    if (!isAdmin && !PasswordHasher::isPasswordStrong(password)) {
        LOG_WARN(QString("Password for user '%1' is weak: %2")
                    .arg(username)
                    .arg(PasswordHasher::getPasswordStrength(password)));
        // 继续创建，但记录警告
    }

    // 生成密码哈希
    LOG_INFO("Generating password hash...");
    QString passwordHash = PasswordHasher::hashPassword(password);
    LOG_INFO(QString("Password hash generated: %1").arg(passwordHash.left(20) + "..."));

    // 创建用户记录
    LOG_INFO("Getting next user ID...");
    UserRecord user;
    user.id = getNextUserId();
    LOG_INFO(QString("Next user ID: %1").arg(user.id));

    user.username = username;
    user.passwordHash = passwordHash;
    user.createdAt = QDateTime::currentDateTime();
    user.updatedAt = user.createdAt;
    user.isActive = true;
    user.isAdmin = isAdmin;

    // 插入到数据库
    LOG_INFO("Inserting user into database...");
    if (!insertUser(user)) {
        LOG_ERROR(QString("Failed to insert user '%1'").arg(username));
        return false;
    }

    LOG_INFO(QString("User '%1' created successfully (id: %2, admin: %3)")
                .arg(username)
                .arg(user.id)
                .arg(isAdmin ? "true" : "false"));

    return true;
}

bool AuthManager::dropUser(const QString& username) {
    // 防止删除最后一个管理员
    if (isUserAdmin(username)) {
        int adminCount = 0;
        auto users = getAllUsers();
        for (const auto& u : users) {
            if (u.isAdmin) adminCount++;
        }

        if (adminCount <= 1) {
            LOG_ERROR("Cannot delete the last admin user");
            return false;
        }
    }

    if (!deleteUser(username)) {
        LOG_ERROR(QString("Failed to delete user '%1'").arg(username));
        return false;
    }

    LOG_INFO(QString("User '%1' deleted successfully").arg(username));
    return true;
}

bool AuthManager::alterUserPassword(const QString& username,
                                    const QString& newPassword) {
    auto userOpt = getUserFromDatabase(username);
    if (!userOpt) {
        LOG_ERROR(QString("User '%1' not found").arg(username));
        return false;
    }

    UserRecord user = *userOpt;

    // 生成新密码哈希
    user.passwordHash = PasswordHasher::hashPassword(newPassword);
    user.updatedAt = QDateTime::currentDateTime();

    if (!updateUser(user)) {
        LOG_ERROR(QString("Failed to update password for user '%1'").arg(username));
        return false;
    }

    LOG_INFO(QString("Password updated for user '%1'").arg(username));
    return true;
}

bool AuthManager::setUserActive(const QString& username, bool active) {
    auto userOpt = getUserFromDatabase(username);
    if (!userOpt) {
        LOG_ERROR(QString("User '%1' not found").arg(username));
        return false;
    }

    UserRecord user = *userOpt;
    user.isActive = active;
    user.updatedAt = QDateTime::currentDateTime();

    if (!updateUser(user)) {
        LOG_ERROR(QString("Failed to update active status for user '%1'").arg(username));
        return false;
    }

    LOG_INFO(QString("User '%1' active status set to %2")
                .arg(username)
                .arg(active ? "true" : "false"));
    return true;
}

// ========== 认证验证 ==========

bool AuthManager::authenticate(const QString& username, const QString& password) {
    auto userOpt = getUserFromDatabase(username);
    if (!userOpt) {
        LOG_WARN(QString("Authentication failed: user '%1' not found").arg(username));
        return false;
    }

    const UserRecord& user = *userOpt;

    // 检查用户是否激活
    if (!user.isActive) {
        LOG_WARN(QString("Authentication failed: user '%1' is disabled").arg(username));
        return false;
    }

    // 验证密码
    if (!PasswordHasher::verifyPassword(password, user.passwordHash)) {
        LOG_WARN(QString("Authentication failed: invalid password for user '%1'").arg(username));
        return false;
    }

    LOG_INFO(QString("User '%1' authenticated successfully").arg(username));
    return true;
}

// ========== 用户信息查询 ==========

bool AuthManager::userExists(const QString& username) {
    return getUserFromDatabase(username).has_value();
}

bool AuthManager::isUserAdmin(const QString& username) {
    auto userOpt = getUserFromDatabase(username);
    return userOpt ? userOpt->isAdmin : false;
}

bool AuthManager::isUserActive(const QString& username) {
    auto userOpt = getUserFromDatabase(username);
    return userOpt ? userOpt->isActive : false;
}

std::optional<UserRecord> AuthManager::getUser(const QString& username) {
    return getUserFromDatabase(username);
}

QVector<UserRecord> AuthManager::getAllUsers() {
    LOG_INFO("getAllUsers called");
    QVector<UserRecord> users;

    if (!catalog_->tableExists(USERS_TABLE)) {
        LOG_INFO("Users table does not exist");
        return users;
    }

    const TableDef* tableDef = catalog_->getTable(USERS_TABLE);
    PageId pageId = tableDef->firstPageId;
    LOG_INFO(QString("Users table found, first page ID: %1").arg(pageId));

    // 遍历所有页面
    int pageCount = 0;
    const int MAX_PAGES = 100;  // 防止无限循环的保护

    while (pageId != INVALID_PAGE_ID && pageCount < MAX_PAGES) {
        LOG_INFO(QString("Fetching page %1 (iteration %2)").arg(pageId).arg(pageCount));
        Page* page = bufferPool_->fetchPage(pageId);
        if (!page) {
            LOG_ERROR(QString("Failed to fetch page %1").arg(pageId));
            break;
        }
        LOG_INFO(QString("Page %1 fetched successfully").arg(pageId));

        // 获取页面中的所有记录（包含RecordHeader用于MVCC检查）
        QVector<QVector<QVariant>> records;
        QVector<RecordHeader> headers;
        LOG_INFO(QString("Getting all records from page %1").arg(pageId));
        if (!TablePage::getAllRecords(page, tableDef, records, headers)) {
            LOG_WARN(QString("Failed to get records from page %1").arg(pageId));
            bufferPool_->unpinPage(pageId, false);
            break;
        }
        LOG_INFO(QString("Got %1 records from page %2").arg(records.size()).arg(pageId));

        // 解析用户记录
        for (int i = 0; i < records.size(); ++i) {
            const auto& record = records[i];
            if (record.isEmpty()) continue;

            // 跳过已逻辑删除的记录
            if (i < headers.size() && headers[i].deleteTxnId != INVALID_TXN_ID) {
                LOG_DEBUG(QString("Skipping deleted user record (deleteTxnId=%1)").arg(headers[i].deleteTxnId));
                continue;
            }

            UserRecord user;
            user.id = record[0].toULongLong();
            user.username = record[1].toString();
            user.passwordHash = record[2].toString();
            user.createdAt = QDateTime::fromSecsSinceEpoch(record[3].toLongLong());
            user.updatedAt = QDateTime::fromSecsSinceEpoch(record[4].toLongLong());
            user.isActive = record[5].toInt() != 0;  // INT -> BOOL
            user.isAdmin = record[6].toInt() != 0;   // INT -> BOOL

            users.append(user);
        }

        PageId nextPageId = page->getNextPageId();
        LOG_INFO(QString("Page %1 nextPageId: %2").arg(pageId).arg(nextPageId));
        bufferPool_->unpinPage(pageId, false);

        // 防止循环回到同一页
        if (nextPageId == pageId) {
            LOG_ERROR(QString("Page %1 points to itself! Breaking loop.").arg(pageId));
            break;
        }

        pageId = nextPageId;
        pageCount++;
    }

    if (pageCount >= MAX_PAGES) {
        LOG_ERROR(QString("getAllUsers exceeded max page limit (%1), possible infinite loop!").arg(MAX_PAGES));
    }

    LOG_INFO(QString("getAllUsers returning %1 users").arg(users.size()));
    return users;
}

int AuthManager::getUserCount() {
    return getAllUsers().size();
}

// ========== 辅助方法 ==========

std::optional<UserRecord> AuthManager::getUserFromDatabase(const QString& username) {
    auto users = getAllUsers();

    for (const auto& user : users) {
        if (user.username == username) {
            return user;
        }
    }

    return std::nullopt;
}

bool AuthManager::insertUser(const UserRecord& user) {
    if (!catalog_->tableExists(USERS_TABLE)) {
        LOG_ERROR("Users table does not exist");
        return false;
    }

    const TableDef* tableDef = catalog_->getTable(USERS_TABLE);

    // 准备记录数据
    QVector<QVariant> record;
    record.append(QVariant::fromValue(user.id));
    record.append(user.username);
    record.append(user.passwordHash);
    record.append(QVariant::fromValue(user.createdAt.toSecsSinceEpoch()));
    record.append(QVariant::fromValue(user.updatedAt.toSecsSinceEpoch()));
    record.append(user.isActive ? 1 : 0);  // BOOL -> INT
    record.append(user.isAdmin ? 1 : 0);   // BOOL -> INT

    // 查找有空闲空间的页面
    PageId pageId = tableDef->firstPageId;
    Page* page = nullptr;

    while (pageId != INVALID_PAGE_ID) {
        page = bufferPool_->fetchPage(pageId);
        if (!page) {
            LOG_ERROR(QString("Failed to fetch page %1").arg(pageId));
            return false;
        }

        // 尝试插入
        if (TablePage::insertRecord(page, tableDef, user.id, record, INVALID_TXN_ID)) {
            bufferPool_->unpinPage(pageId, true);
            return true;
        }

        // 页面已满，检查下一页
        PageId nextPageId = page->getNextPageId();
        bufferPool_->unpinPage(pageId, false);
        pageId = nextPageId;
    }

    // 所有页面都满了，分配新页面
    PageId newPageId = diskManager_->allocatePage();
    page = bufferPool_->fetchPage(newPageId);
    if (!page) {
        LOG_ERROR("Failed to allocate new page for users table");
        return false;
    }

    // 初始化新页面
    TablePage::init(page, newPageId);

    if (!TablePage::insertRecord(page, tableDef, user.id, record, INVALID_TXN_ID)) {
        bufferPool_->unpinPage(newPageId, false);
        LOG_ERROR("Failed to insert record into new page");
        return false;
    }

    // 链接到表的页面链表
    if (tableDef->firstPageId != INVALID_PAGE_ID) {
        // 找到最后一页并链接
        PageId lastPageId = tableDef->firstPageId;
        while (true) {
            Page* lastPage = bufferPool_->fetchPage(lastPageId);
            if (!lastPage) {
                bufferPool_->unpinPage(newPageId, false);
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
    }

    bufferPool_->unpinPage(newPageId, true);

    return true;
}

bool AuthManager::updateUser(const UserRecord& user) {
    if (!catalog_->tableExists(USERS_TABLE)) {
        return false;
    }

    const TableDef* tableDef = catalog_->getTable(USERS_TABLE);
    PageId pageId = tableDef->firstPageId;

    // 遍历所有页面查找用户
    while (pageId != INVALID_PAGE_ID) {
        Page* page = bufferPool_->fetchPage(pageId);
        if (!page) {
            break;
        }

        // 获取页面中的所有记录
        QVector<QVector<QVariant>> records;
        if (!TablePage::getAllRecords(page, tableDef, records)) {
            bufferPool_->unpinPage(pageId, false);
            break;
        }

        // 遍历所有记录
        for (int slotIndex = 0; slotIndex < records.size(); slotIndex++) {
            const QVector<QVariant>& record = records[slotIndex];
            if (record.isEmpty()) continue;

            // 检查是否是目标用户
            if (record[1].toString() == user.username) {
                // 准备更新后的记录
                QVector<QVariant> newRecord = record;
                newRecord[2] = user.passwordHash;
                newRecord[4] = QVariant::fromValue(user.updatedAt.toSecsSinceEpoch());
                newRecord[5] = user.isActive ? 1 : 0;  // BOOL -> INT
                newRecord[6] = user.isAdmin ? 1 : 0;   // BOOL -> INT

                TablePage::updateRecord(page, tableDef, slotIndex, newRecord, INVALID_TXN_ID);
                bufferPool_->unpinPage(pageId, true);
                return true;
            }
        }

        PageId nextPageId = page->getNextPageId();
        bufferPool_->unpinPage(pageId, false);
        pageId = nextPageId;
    }

    return false;
}

bool AuthManager::deleteUser(const QString& username) {
    if (!catalog_->tableExists(USERS_TABLE)) {
        return false;
    }

    const TableDef* tableDef = catalog_->getTable(USERS_TABLE);
    PageId pageId = tableDef->firstPageId;

    // 遍历所有页面查找用户
    while (pageId != INVALID_PAGE_ID) {
        Page* page = bufferPool_->fetchPage(pageId);
        if (!page) {
            break;
        }

        // 获取页面中的所有记录
        QVector<QVector<QVariant>> records;
        if (!TablePage::getAllRecords(page, tableDef, records)) {
            bufferPool_->unpinPage(pageId, false);
            break;
        }

        // 遍历所有记录
        for (int slotIndex = 0; slotIndex < records.size(); slotIndex++) {
            const QVector<QVariant>& record = records[slotIndex];
            if (record.isEmpty()) continue;

            // 检查是否是目标用户
            if (record[1].toString() == username) {
                // 删除记录 (使用事务ID=1表示系统删除)
                TablePage::deleteRecord(page, slotIndex, 1);
                bufferPool_->unpinPage(pageId, true);
                return true;
            }
        }

        PageId nextPageId = page->getNextPageId();
        bufferPool_->unpinPage(pageId, false);
        pageId = nextPageId;
    }

    return false;
}

uint64_t AuthManager::getNextUserId() {
    auto users = getAllUsers();

    uint64_t maxId = 0;
    for (const auto& user : users) {
        if (user.id > maxId) {
            maxId = user.id;
        }
    }

    return maxId + 1;
}

} // namespace qindb
