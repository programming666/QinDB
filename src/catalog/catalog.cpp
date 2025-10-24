#include "qindb/catalog.h"
#include "qindb/catalog_db_backend.h"
#include "qindb/logger.h"
#include "qindb/config.h"
#include <QFile>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace qindb {

Catalog::Catalog()
    : useDatabase_(false)
{
    // 从配置读取持久化模式
    useDatabase_ = !Config::instance().isCatalogUseFile();
    LOG_INFO(QString("Catalog initialized (mode: %1)")
        .arg(useDatabase_ ? "database" : "file"));
}

Catalog::~Catalog() {
    LOG_INFO("Catalog destroyed");
}

void Catalog::setDatabaseBackend(BufferPoolManager* bufferPool, DiskManager* diskManager) {
    QMutexLocker locker(&mutex_);

    if (!bufferPool || !diskManager) {
        LOG_ERROR("Invalid buffer pool or disk manager for catalog backend");
        return;
    }

    dbBackend_ = std::make_unique<CatalogDbBackend>(bufferPool, diskManager);

    if (!dbBackend_->initialize()) {
        LOG_ERROR("Failed to initialize catalog database backend");
        dbBackend_.reset();
        return;
    }

    LOG_INFO("Catalog database backend initialized");
}

bool Catalog::createTable(const TableDef& tableDef) {
    QMutexLocker locker(&mutex_);

    QString lowerName = tableDef.name.toLower();

    if (tables_.contains(lowerName)) {
        LOG_ERROR(QString("Table '%1' already exists").arg(tableDef.name));
        return false;
    }

    tables_[lowerName] = std::make_shared<TableDef>(tableDef);

    LOG_INFO(QString("Created table '%1' with %2 columns")
                 .arg(tableDef.name)
                 .arg(tableDef.columns.size()));

    return true;
}

bool Catalog::dropTable(const QString& tableName) {
    QMutexLocker locker(&mutex_);

    QString lowerName = tableName.toLower();

    if (!tables_.contains(lowerName)) {
        LOG_ERROR(QString("Table '%1' does not exist").arg(tableName));
        return false;
    }

    // 删除该表的所有索引
    QVector<QString> indexesToRemove;
    for (auto it = indexes_.begin(); it != indexes_.end(); ++it) {
        if (it.value().tableName.toLower() == lowerName) {
            indexesToRemove.append(it.key());
        }
    }

    for (const auto& indexName : indexesToRemove) {
        indexes_.remove(indexName);
        LOG_DEBUG(QString("Removed index '%1'").arg(indexName));
    }

    tables_.remove(lowerName);

    LOG_INFO(QString("Dropped table '%1'").arg(tableName));

    return true;
}

const TableDef* Catalog::getTable(const QString& tableName) const {
    QMutexLocker locker(&mutex_);

    QString lowerName = tableName.toLower();

    auto it = tables_.find(lowerName);
    if (it != tables_.end()) {
        return it.value().get();
    }

    return nullptr;
}

bool Catalog::tableExists(const QString& tableName) const {
    QMutexLocker locker(&mutex_);
    return tables_.contains(tableName.toLower());
}

QVector<QString> Catalog::getAllTableNames() const {
    QMutexLocker locker(&mutex_);

    QVector<QString> names;
    for (auto it = tables_.begin(); it != tables_.end(); ++it) {
        names.append(it.value()->name);
    }

    return names;
}

bool Catalog::createIndex(const IndexDef& indexDef) {
    QMutexLocker locker(&mutex_);

    QString lowerName = indexDef.name.toLower();

    if (indexes_.contains(lowerName)) {
        LOG_ERROR(QString("Index '%1' already exists").arg(indexDef.name));
        return false;
    }

    QString lowerTableName = indexDef.tableName.toLower();
    if (!tables_.contains(lowerTableName)) {
        LOG_ERROR(QString("Table '%1' does not exist").arg(indexDef.tableName));
        return false;
    }

    indexes_[lowerName] = indexDef;

    // 同时添加到表定义中
    tables_[lowerTableName]->indexes.append(indexDef);

    LOG_INFO(QString("Created index '%1' on table '%2'")
                 .arg(indexDef.name)
                 .arg(indexDef.tableName));

    return true;
}

bool Catalog::dropIndex(const QString& indexName) {
    QMutexLocker locker(&mutex_);

    QString lowerName = indexName.toLower();

    if (!indexes_.contains(lowerName)) {
        LOG_ERROR(QString("Index '%1' does not exist").arg(indexName));
        return false;
    }

    QString tableName = indexes_[lowerName].tableName.toLower();
    indexes_.remove(lowerName);

    // 从表定义中移除
    if (tables_.contains(tableName)) {
        auto& tableIndexes = tables_[tableName]->indexes;
        for (int i = 0; i < tableIndexes.size(); ++i) {
            if (tableIndexes[i].name.toLower() == lowerName) {
                tableIndexes.remove(i);
                break;
            }
        }
    }

    LOG_INFO(QString("Dropped index '%1'").arg(indexName));

    return true;
}

const IndexDef* Catalog::getIndex(const QString& indexName) const {
    QMutexLocker locker(&mutex_);

    QString lowerName = indexName.toLower();

    auto it = indexes_.find(lowerName);
    if (it != indexes_.end()) {
        return &it.value();
    }

    return nullptr;
}

QVector<IndexDef> Catalog::getTableIndexes(const QString& tableName) const {
    QMutexLocker locker(&mutex_);

    QVector<IndexDef> result;
    QString lowerTableName = tableName.toLower();

    for (auto it = indexes_.begin(); it != indexes_.end(); ++it) {
        if (it.value().tableName.toLower() == lowerTableName) {
            result.append(it.value());
        }
    }

    return result;
}

bool Catalog::updateTable(const QString& tableName, const TableDef& newDef) {
    QMutexLocker locker(&mutex_);

    QString lowerName = tableName.toLower();

    if (!tables_.contains(lowerName)) {
        LOG_ERROR(QString("Table '%1' does not exist").arg(tableName));
        return false;
    }

    tables_[lowerName] = std::make_shared<TableDef>(newDef);

    LOG_INFO(QString("Updated table '%1'").arg(tableName));

    return true;
}

bool Catalog::saveToDisk(const QString& filePath) {
    QMutexLocker locker(&mutex_);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_ERROR(QString("Failed to open catalog file for writing: %1").arg(filePath));
        return false;
    }

    // 使用JSON格式保存元数据
    QJsonObject root;
    QJsonArray tablesArray;

    for (auto it = tables_.begin(); it != tables_.end(); ++it) {
        const TableDef& table = *it.value();

        QJsonObject tableObj;
        tableObj["name"] = table.name;
        tableObj["firstPageId"] = static_cast<qint64>(table.firstPageId);
        tableObj["nextRowId"] = static_cast<qint64>(table.nextRowId);

        // 列定义
        QJsonArray columnsArray;
        for (const auto& col : table.columns) {
            QJsonObject colObj;
            colObj["name"] = col.name;
            colObj["type"] = static_cast<int>(col.type);
            colObj["length"] = col.length;
            colObj["notNull"] = col.notNull;
            colObj["primaryKey"] = col.primaryKey;
            colObj["autoIncrement"] = col.autoIncrement;
            columnsArray.append(colObj);
        }
        tableObj["columns"] = columnsArray;

        // 索引定义
        QJsonArray indexesArray;
        for (const auto& idx : table.indexes) {
            QJsonObject idxObj;
            idxObj["name"] = idx.name;
            idxObj["unique"] = idx.unique;
            idxObj["autoCreated"] = idx.autoCreated;
            idxObj["rootPageId"] = static_cast<qint64>(idx.rootPageId);
            idxObj["indexType"] = static_cast<int>(idx.indexType);
            idxObj["keyType"] = static_cast<int>(idx.keyType);

            QJsonArray colsArray;
            for (const auto& colName : idx.columns) {
                colsArray.append(colName);
            }
            idxObj["columns"] = colsArray;

            indexesArray.append(idxObj);
        }
        tableObj["indexes"] = indexesArray;

        tablesArray.append(tableObj);
    }

    root["tables"] = tablesArray;

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    LOG_INFO(QString("Saved catalog to %1").arg(filePath));

    return true;
}

bool Catalog::loadFromDisk(const QString& filePath) {
    QMutexLocker locker(&mutex_);

    QFile file(filePath);
    if (!file.exists()) {
        LOG_WARN(QString("Catalog file does not exist: %1").arg(filePath));
        return false;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_ERROR(QString("Failed to open catalog file for reading: %1").arg(filePath));
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        LOG_ERROR("Invalid catalog JSON format");
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray tablesArray = root["tables"].toArray();

    tables_.clear();
    indexes_.clear();

    for (const auto& tableValue : tablesArray) {
        QJsonObject tableObj = tableValue.toObject();

        TableDef table;
        table.name = tableObj["name"].toString();
        table.firstPageId = static_cast<PageId>(tableObj["firstPageId"].toInteger());
        table.nextRowId = static_cast<RowId>(tableObj["nextRowId"].toInteger());

        // 加载列定义
        QJsonArray columnsArray = tableObj["columns"].toArray();
        for (const auto& colValue : columnsArray) {
            QJsonObject colObj = colValue.toObject();

            ColumnDef col;
            col.name = colObj["name"].toString();
            col.type = static_cast<DataType>(colObj["type"].toInt());
            col.length = colObj["length"].toInt();
            col.notNull = colObj["notNull"].toBool();
            col.primaryKey = colObj["primaryKey"].toBool();
            col.autoIncrement = colObj["autoIncrement"].toBool();

            table.columns.append(col);
        }

        // 加载索引定义
        QJsonArray indexesArray = tableObj["indexes"].toArray();
        for (const auto& idxValue : indexesArray) {
            QJsonObject idxObj = idxValue.toObject();

            IndexDef idx;
            idx.name = idxObj["name"].toString();
            idx.tableName = table.name;
            idx.unique = idxObj["unique"].toBool();
            idx.autoCreated = idxObj["autoCreated"].toBool(false);  // 默认false（旧数据兼容）
            idx.rootPageId = static_cast<PageId>(idxObj["rootPageId"].toInteger());
            idx.indexType = static_cast<IndexType>(idxObj["indexType"].toInt(static_cast<int>(IndexType::BTREE)));
            idx.keyType = static_cast<DataType>(idxObj["keyType"].toInt(static_cast<int>(DataType::NULL_TYPE)));

            QJsonArray colsArray = idxObj["columns"].toArray();
            for (const auto& colName : colsArray) {
                idx.columns.append(colName.toString());
            }

            table.indexes.append(idx);
            indexes_[idx.name.toLower()] = idx;
        }

        tables_[table.name.toLower()] = std::make_shared<TableDef>(table);
    }

    LOG_INFO(QString("Loaded catalog from %1 (%2 tables)")
                 .arg(filePath)
                 .arg(tables_.size()));

    return true;
}

bool Catalog::save(const QString& filePath) {
    if (useDatabase_) {
        return saveToDatabase();
    } else {
        return saveToDisk(filePath);
    }
}

bool Catalog::load(const QString& filePath) {
    if (useDatabase_) {
        return loadFromDatabase();
    } else {
        return loadFromDisk(filePath);
    }
}

bool Catalog::saveToDatabase() {
    QMutexLocker locker(&mutex_);

    if (!dbBackend_) {
        LOG_ERROR("Database backend not initialized");
        return false;
    }

    if (!dbBackend_->saveCatalog(tables_, indexes_)) {
        LOG_ERROR("Failed to save catalog to database");
        return false;
    }

    LOG_INFO("Catalog saved to database");
    return true;
}

bool Catalog::loadFromDatabase() {
    QMutexLocker locker(&mutex_);

    if (!dbBackend_) {
        LOG_ERROR("Database backend not initialized");
        return false;
    }

    if (!dbBackend_->loadCatalog(tables_, indexes_)) {
        LOG_ERROR("Failed to load catalog from database");
        return false;
    }

    LOG_INFO("Catalog loaded from database");
    return true;
}

} // namespace qindb
