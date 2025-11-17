#include "qindb/database_manager.h"  // 引入数据库管理器头文件
#include "qindb/logger.h"           // 引入日志系统头文件
#include "qindb/config.h"           // 引入配置系统头文件
#include "qindb/permission_manager.h" // 引入权限管理器头文件
#include <QFile>                    // 引入Qt文件操作类
#include <QJsonDocument>           // 引入Qt JSON文档处理类
#include <QJsonObject>             // 引入Qt JSON对象类
#include <QJsonArray>              // 引入Qt JSON数组类

namespace qindb {  // 定义qindb命名空间

DatabaseManager::DatabaseManager(const QString& dataDir)
    : m_dataDir(dataDir)
    , m_currentTransactionId(INVALID_TXN_ID)
{
    // 确保数据目录存在
    QDir dir;
    if (!dir.exists(m_dataDir)) {
        dir.mkpath(m_dataDir);
        LOG_INFO(QString("Created data directory: %1").arg(m_dataDir));
    }

    // 尝试加载已存在的数据库列表
    loadFromDisk();
}

DatabaseManager::~DatabaseManager() {
    LOG_INFO("DatabaseManager destructor called, starting cleanup...");
    // 关闭所有数据库（刷新缓冲池和WAL）
    QVector<QString> dbNames;
    for (auto it = m_databases.begin(); it != m_databases.end(); ++it) {
        dbNames.append(it->first);
    }

    for (const QString& dbName : dbNames) {
        closeDatabase(dbName);
    }

    // 保存数据库管理器状态
    saveToDisk();

    // 清空数据库列表
    m_databases.clear();

    LOG_INFO("DatabaseManager destroyed, all databases closed and flushed");
}

bool DatabaseManager::createDatabase(const QString& dbName, bool ifNotExists) {
    QMutexLocker locker(&m_mutex);

    // 检查数据库名是否合法
    if (dbName.isEmpty()) {
        m_error = Error(ErrorCode::SEMANTIC_ERROR, "Database name cannot be empty");
        LOG_ERROR(m_error.message);
        return false;
    }

    // 检查数据库是否已存在
    if (m_databases.contains(dbName)) {
        if (ifNotExists) {
            LOG_INFO(QString("Database '%1' already exists (IF NOT EXISTS)").arg(dbName));
            return true;
        }
        m_error = Error(ErrorCode::SEMANTIC_ERROR,
                       QString("Database '%1' already exists").arg(dbName));
        LOG_ERROR(m_error.message);
        return false;
    }

    // 初始化数据库
    if (!initializeDatabase(dbName)) {
        return false;
    }

    LOG_INFO(QString("Database '%1' created successfully").arg(dbName));

    // 如果是第一个数据库，自动选择为当前数据库
    if (m_currentDatabase.isEmpty()) {
        m_currentDatabase = dbName;
        LOG_INFO(QString("Switched to database '%1'").arg(dbName));
    }

    // 保存元信息
    saveToDisk();

    return true;
}

bool DatabaseManager::dropDatabase(const QString& dbName, bool ifExists) {
    QMutexLocker locker(&m_mutex);

    // 检查数据库是否存在
    if (!m_databases.contains(dbName)) {
        if (ifExists) {
            LOG_INFO(QString("Database '%1' does not exist (IF EXISTS)").arg(dbName));
            return true;
        }
        m_error = Error(ErrorCode::TABLE_NOT_FOUND,
                       QString("Database '%1' does not exist").arg(dbName));
        LOG_ERROR(m_error.message);
        return false;
    }

    // 获取数据库路径
    QString dbPath = m_databases[dbName]->path;

    // 关闭数据库
    closeDatabase(dbName);

    // 从内存中移除
    m_databases.erase(dbName);

    // 删除数据库目录和文件
    QDir dir(dbPath);
    if (dir.exists()) {
        if (!dir.removeRecursively()) {
            m_error = Error(ErrorCode::IO_ERROR,
                           QString("Failed to remove database directory: %1").arg(dbPath));
            LOG_ERROR(m_error.message);
            return false;
        }
    }

    LOG_INFO(QString("Database '%1' dropped successfully").arg(dbName));

    // 如果删除的是当前数据库，清空当前数据库
    if (m_currentDatabase == dbName) {
        m_currentDatabase.clear();
        // 如果还有其他数据库，选择第一个
        if (!m_databases.empty()) {
            m_currentDatabase = m_databases.begin()->first;
            LOG_INFO(QString("Switched to database '%1'").arg(m_currentDatabase));
        }
    }

    // 保存元信息
    saveToDisk();

    return true;
}

bool DatabaseManager::useDatabase(const QString& dbName) {
    QMutexLocker locker(&m_mutex);

    // 检查数据库是否存在
    if (!m_databases.contains(dbName)) {
        // 尝试从磁盘加载
        if (!loadDatabase(dbName)) {
            m_error = Error(ErrorCode::TABLE_NOT_FOUND,
                           QString("Database '%1' does not exist").arg(dbName));
            LOG_ERROR(m_error.message);
            return false;
        }
    }

    m_currentDatabase = dbName;
    LOG_INFO(QString("Switched to database '%1'").arg(dbName));
    return true;
}

QVector<QString> DatabaseManager::getAllDatabaseNames() const {
    QMutexLocker locker(&m_mutex);

    // 扫描数据目录
    QDir dataDir(m_dataDir);
    QVector<QString> dbNames;

    QStringList subdirs = dataDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& subdir : subdirs) {
        // 检查是否是有效的数据库目录（包含catalog.json）
        QString catalogPath = m_dataDir + "/" + subdir + "/catalog.json";
        if (QFile::exists(catalogPath)) {
            dbNames.append(subdir);
        }
    }

    return dbNames;
}

bool DatabaseManager::databaseExists(const QString& dbName) const {
    QMutexLocker locker(&m_mutex);

    if (m_databases.contains(dbName)) {
        return true;
    }

    // 检查磁盘上是否存在
    QString dbPath = m_dataDir + "/" + dbName;
    QString catalogPath = dbPath + "/catalog.json";
    return QFile::exists(catalogPath);
}

QString DatabaseManager::currentDatabaseName() const {
    QMutexLocker locker(&m_mutex);
    return m_currentDatabase;
}

Catalog* DatabaseManager::getCurrentCatalog() const {
    QMutexLocker locker(&m_mutex);

    if (m_currentDatabase.isEmpty()) {
        return nullptr;
    }

    auto it = m_databases.find(m_currentDatabase);
    if (it == m_databases.end()) {
        return nullptr;
    }

    return it->second->catalog.get();
}

BufferPoolManager* DatabaseManager::getCurrentBufferPool() const {
    QMutexLocker locker(&m_mutex);

    if (m_currentDatabase.isEmpty()) {
        return nullptr;
    }

    auto it = m_databases.find(m_currentDatabase);
    if (it == m_databases.end()) {
        return nullptr;
    }

    return it->second->bufferPool.get();
}

DiskManager* DatabaseManager::getCurrentDiskManager() const {
    QMutexLocker locker(&m_mutex);

    if (m_currentDatabase.isEmpty()) {
        return nullptr;
    }

    auto it = m_databases.find(m_currentDatabase);
    if (it == m_databases.end()) {
        return nullptr;
    }

    return it->second->diskManager.get();
}

QString DatabaseManager::getDatabasePath(const QString& dbName) const {
    return m_dataDir + "/" + dbName;
}

bool DatabaseManager::saveToDisk() {
    QString metaPath = m_dataDir + "/databases.json";

    QJsonObject root;
    root["current_database"] = m_currentDatabase;

    QJsonArray dbArray;
    for (auto it = m_databases.begin(); it != m_databases.end(); ++it) {
        QJsonObject dbObj;
        dbObj["name"] = it->first;
        dbObj["path"] = it->second->path;
        dbArray.append(dbObj);
    }
    root["databases"] = dbArray;

    QJsonDocument doc(root);
    QFile file(metaPath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_error = Error(ErrorCode::IO_ERROR,
                       QString("Failed to open databases.json for writing: %1").arg(metaPath));
        LOG_ERROR(m_error.message);
        return false;
    }

    file.write(doc.toJson());
    file.close();

    return true;
}

bool DatabaseManager::loadFromDisk() {
    QString metaPath = m_dataDir + "/databases.json";

    if (!QFile::exists(metaPath)) {
        LOG_INFO("No existing databases.json found");
        return true;  // 不是错误，只是没有已存在的数据库
    }

    QFile file(metaPath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = Error(ErrorCode::IO_ERROR,
                       QString("Failed to open databases.json for reading: %1").arg(metaPath));
        LOG_ERROR(m_error.message);
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        m_error = Error(ErrorCode::IO_ERROR, "Invalid databases.json format");
        LOG_ERROR(m_error.message);
        return false;
    }

    QJsonObject root = doc.object();
    m_currentDatabase = root["current_database"].toString();

    QJsonArray dbArray = root["databases"].toArray();
    for (const QJsonValue& val : dbArray) {
        QJsonObject dbObj = val.toObject();
        QString dbName = dbObj["name"].toString();

        // 延迟加载：只记录数据库存在，不立即加载所有组件
        // 实际使用时再通过useDatabase加载
    }

    LOG_INFO(QString("Loaded database metadata, current database: %1").arg(m_currentDatabase));

    // 如果有当前数据库，尝试加载它
    if (!m_currentDatabase.isEmpty()) {
        loadDatabase(m_currentDatabase);
    }

    return true;
}

bool DatabaseManager::initializeDatabase(const QString& dbName) {
    QString dbPath = getDatabasePath(dbName);

    // 创建数据库目录
    QDir dir;
    if (!dir.mkpath(dbPath)) {
        m_error = Error(ErrorCode::IO_ERROR,
                       QString("Failed to create database directory: %1").arg(dbPath));
        LOG_ERROR(m_error.message);
        return false;
    }

    // 创建数据库定义
    auto dbDef = std::make_unique<DatabaseDef>(dbName, dbPath);

    // 创建磁盘管理器
    QString dataFilePath = dbPath + "/qindb.db";
    dbDef->diskManager = std::make_unique<DiskManager>(dataFilePath);

    // 创建缓冲池管理器
    dbDef->bufferPool = std::make_unique<BufferPoolManager>(DEFAULT_BUFFER_POOL_SIZE, dbDef->diskManager.get());

    // 预留系统表页面 (pages 1-5)
    // 这样可以确保Catalog和WAL的数据库后端有固定的页面ID可用
    bool catalogUseDb = !Config::instance().isCatalogUseFile();
    bool walUseDb = !Config::instance().isWalUseFile();

    if (catalogUseDb || walUseDb) {
        LOG_INFO("Reserving system table pages (1-5) for database backend modes");
        // 预留pages 1-5, 创建占位符页面
        for (int i = 1; i <= 5; i++) {
            PageId pageId = INVALID_PAGE_ID;
            Page* page = dbDef->bufferPool->newPage(&pageId);
            if (!page || pageId != static_cast<PageId>(i)) {
                LOG_ERROR(QString("Failed to reserve system page %1 (got page %2)").arg(i).arg(pageId));
                // 继续尝试
            } else {
                // 初始化为空表页
                page->setPageType(PageType::TABLE_PAGE);
                dbDef->bufferPool->unpinPage(pageId, true);
            }
        }
    }

    // 创建Catalog
    dbDef->catalog = std::make_unique<Catalog>();

    // 如果使用数据库模式，设置后端
    if (!Config::instance().isCatalogUseFile()) {
        dbDef->catalog->setDatabaseBackend(dbDef->bufferPool.get(), dbDef->diskManager.get());
    }

    // Create permission manager and ensure system tables
    dbDef->permissionManager = std::make_unique<PermissionManager>(dbDef->bufferPool.get(), dbDef->catalog.get(), dbName);
    if (!dbDef->permissionManager->initializePermissionSystem()) {
        m_error = Error(ErrorCode::INTERNAL_ERROR,
                       QString("Failed to initialize permissions for database '%1'").arg(dbName));
        LOG_ERROR(m_error.message);
        return false;
    }

    // 创建WAL管理器
    QString walFilePath = dbPath + "/" + Config::instance().getWalFilePath();
    dbDef->walManager = std::make_unique<WALManager>(walFilePath);

    // 如果使用数据库模式，设置WAL后端
    if (!Config::instance().isWalUseFile()) {
        dbDef->walManager->setDatabaseBackend(dbDef->bufferPool.get(), dbDef->diskManager.get());
    }

    if (!dbDef->walManager->initialize()) {
        m_error = Error(ErrorCode::IO_ERROR,
                       QString("Failed to initialize WAL for database '%1'").arg(dbName));
        LOG_ERROR(m_error.message);
        return false;
    }

    // 创建事务管理器
    dbDef->transactionManager = std::make_unique<TransactionManager>(dbDef->walManager.get());

    // 保存Catalog（使用 save() 方法自动选择模式）
    if (!dbDef->catalog->save(dbPath + "/" + Config::instance().getCatalogFilePath())) {
        m_error = Error(ErrorCode::IO_ERROR,
                       QString("Failed to save catalog for database '%1'").arg(dbName));
        LOG_ERROR(m_error.message);
        return false;
    }

    // 写入数据库魔数
    if (!dbDef->diskManager->writeMagicNumber(catalogUseDb, walUseDb)) {
        LOG_WARN("Failed to write magic number");
    }

    // 加入到数据库列表
    m_databases[dbName] = std::move(dbDef);

    return true;
}

bool DatabaseManager::loadDatabase(const QString& dbName) {
    if (m_databases.contains(dbName)) {
        return true;  // 已加载
    }

    QString dbPath = getDatabasePath(dbName);
    QString dataFilePath = dbPath + "/qindb.db";

    // 检查数据库文件是否存在
    if (!QFile::exists(dataFilePath)) {
        m_error = Error(ErrorCode::TABLE_NOT_FOUND,
                       QString("Database '%1' not found").arg(dbName));
        LOG_ERROR(m_error.message);
        return false;
    }

    // 创建数据库定义
    auto dbDef = std::make_unique<DatabaseDef>(dbName, dbPath);

    // 创建磁盘管理器
    dbDef->diskManager = std::make_unique<DiskManager>(dataFilePath);

    // 验证魔数并检查模式匹配
    bool dbCatalogMode = false, dbWalMode = false;
    if (dbDef->diskManager->verifyAndParseMagic(dbCatalogMode, dbWalMode)) {
        bool configCatalogMode = !Config::instance().isCatalogUseFile();
        bool configWalMode = !Config::instance().isWalUseFile();

        if (dbCatalogMode != configCatalogMode || dbWalMode != configWalMode) {
            LOG_WARN(QString("Database mode mismatch for '%1':").arg(dbName));
            LOG_WARN(QString("  Database: Catalog=%1, WAL=%2")
                         .arg(dbCatalogMode ? "DB" : "File")
                         .arg(dbWalMode ? "DB" : "File"));
            LOG_WARN(QString("  Config:   Catalog=%1, WAL=%2")
                         .arg(configCatalogMode ? "DB" : "File")
                         .arg(configWalMode ? "DB" : "File"));
        }
    }

    // 创建缓冲池管理器
    dbDef->bufferPool = std::make_unique<BufferPoolManager>(DEFAULT_BUFFER_POOL_SIZE, dbDef->diskManager.get());

    // 创建Catalog并设置后端（如果需要）
    dbDef->catalog = std::make_unique<Catalog>();
    if (!Config::instance().isCatalogUseFile()) {
        dbDef->catalog->setDatabaseBackend(dbDef->bufferPool.get(), dbDef->diskManager.get());
    }

    // 加载Catalog（自动选择模式）
    QString catalogPath = dbPath + "/" + Config::instance().getCatalogFilePath();
    if (!dbDef->catalog->load(catalogPath)) {
        m_error = Error(ErrorCode::IO_ERROR,
                       QString("Failed to load catalog for database '%1'").arg(dbName));
        LOG_ERROR(m_error.message);
        return false;
    }

    // 创建权限管理器并确保系统表存在
    dbDef->permissionManager = std::make_unique<PermissionManager>(dbDef->bufferPool.get(), dbDef->catalog.get(), dbName);
    if (!dbDef->permissionManager->initializePermissionSystem()) {
        m_error = Error(ErrorCode::INTERNAL_ERROR,
                       QString("Failed to initialize permissions for database '%1'").arg(dbName));
        LOG_ERROR(m_error.message);
        return false;
    }

    // 创建WAL管理器并设置后端（如果需要）
    QString walFilePath = dbPath + "/" + Config::instance().getWalFilePath();
    dbDef->walManager = std::make_unique<WALManager>(walFilePath);

    if (!Config::instance().isWalUseFile()) {
        dbDef->walManager->setDatabaseBackend(dbDef->bufferPool.get(), dbDef->diskManager.get());
    }

    if (!dbDef->walManager->initialize()) {
        m_error = Error(ErrorCode::IO_ERROR,
                       QString("Failed to initialize WAL for database '%1'").arg(dbName));
        LOG_ERROR(m_error.message);
        return false;
    }

    // 创建事务管理器
    dbDef->transactionManager = std::make_unique<TransactionManager>(dbDef->walManager.get());

    // 执行WAL恢复
    LOG_INFO(QString("Performing WAL recovery for database '%1'").arg(dbName));
    if (!dbDef->walManager->recover(dbDef->catalog.get(), dbDef->bufferPool.get())) {
        LOG_WARN(QString("WAL recovery had issues for database '%1', continuing").arg(dbName));
    }

    // 加入到数据库列表
    m_databases[dbName] = std::move(dbDef);

    LOG_INFO(QString("Loaded database '%1'").arg(dbName));

    return true;
}

void DatabaseManager::closeDatabase(const QString& dbName) {
    LOG_INFO(QString("Closing database '%1'...").arg(dbName));
    auto it = m_databases.find(dbName);
    if (it != m_databases.end()) {
        // 保存Catalog（根据配置自动选择模式）
        LOG_INFO(QString("Saving catalog for database '%1'...").arg(dbName));
        QString catalogPath = it->second->path + "/" + Config::instance().getCatalogFilePath();
        it->second->catalog->save(catalogPath);  // 使用 save() 而不是 saveToDisk()
        LOG_INFO(QString("Catalog saved for database '%1'").arg(dbName));

        // 刷新缓冲池
        if (it->second->bufferPool) {
            LOG_INFO(QString("Flushing buffer pool for database '%1'...").arg(dbName));
            it->second->bufferPool->flushAllPages();
            LOG_INFO(QString("Buffer pool flushed for database '%1'").arg(dbName));
        }

        // 刷新WAL
        if (it->second->walManager) {
            LOG_INFO(QString("Flushing WAL for database '%1'...").arg(dbName));
            it->second->walManager->flush();
            LOG_INFO(QString("WAL flushed for database '%1'").arg(dbName));
        }

        LOG_INFO(QString("Closed database '%1'").arg(dbName));
    }
}

WALManager* DatabaseManager::getCurrentWALManager() const {
    QMutexLocker locker(&m_mutex);

    if (m_currentDatabase.isEmpty()) {
        return nullptr;
    }

    auto it = m_databases.find(m_currentDatabase);
    if (it == m_databases.end()) {
        return nullptr;
    }

    return it->second->walManager.get();
}

PermissionManager* DatabaseManager::getCurrentPermissionManager() const {
    QMutexLocker locker(&m_mutex);

    if (m_currentDatabase.isEmpty()) {
        return nullptr;
    }

    auto it = m_databases.find(m_currentDatabase);
    if (it == m_databases.end()) {
        return nullptr;
    }

    return it->second->permissionManager.get();
}

TransactionManager* DatabaseManager::getCurrentTransactionManager() const {
    QMutexLocker locker(&m_mutex);

    if (m_currentDatabase.isEmpty()) {
        return nullptr;
    }

    auto it = m_databases.find(m_currentDatabase);
    if (it == m_databases.end()) {
        return nullptr;
    }

    return it->second->transactionManager.get();
}

TransactionId DatabaseManager::getCurrentTransactionId() const {
    QMutexLocker locker(&m_mutex);
    return m_currentTransactionId;
}

void DatabaseManager::setCurrentTransactionId(TransactionId txnId) {
    QMutexLocker locker(&m_mutex);
    m_currentTransactionId = txnId;
}

} // namespace qindb
