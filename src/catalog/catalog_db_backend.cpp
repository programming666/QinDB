#include "qindb/catalog_db_backend.h"
#include "qindb/system_tables.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include "qindb/table_page.h"
#include "qindb/logger.h"
#include <QDataStream>

namespace qindb {

CatalogDbBackend::CatalogDbBackend(BufferPoolManager* bufferPool, DiskManager* diskManager)
    : bufferPool_(bufferPool)
    , diskManager_(diskManager)
    , sysTablesFirstPage_(INVALID_PAGE_ID)
    , sysColumnsFirstPage_(INVALID_PAGE_ID)
    , sysIndexesFirstPage_(INVALID_PAGE_ID)
{
}

CatalogDbBackend::~CatalogDbBackend() {
}

bool CatalogDbBackend::initialize() {
    if (!bufferPool_ || !diskManager_) {
        LOG_ERROR("Invalid buffer pool or disk manager");
        return false;
    }

    // 检查系统表是否已存在
    if (systemTablesExist()) {
        LOG_INFO("System tables already exist");
        return true;
    }

    // 创建系统表
    return createSystemTables();
}

bool CatalogDbBackend::systemTablesExist() {
    // 简单检查：尝试读取页面1、2、3（预留给系统表）
    // 页面0是header page，页面1-3分别是sys_tables, sys_columns, sys_indexes
    Page* page = bufferPool_->fetchPage(1);
    if (!page) {
        return false;
    }

    bool exists = (page->getPageType() == PageType::TABLE_PAGE);
    bufferPool_->unpinPage(1, false);

    return exists;
}

bool CatalogDbBackend::createSystemTables() {
    LOG_INFO("Creating system tables for catalog storage");

    // 创建sys_tables表的第一个页面（页面ID=1）
    PageId sysTablesPageId = INVALID_PAGE_ID;
    Page* sysTablesPage = bufferPool_->newPage(&sysTablesPageId);
    if (!sysTablesPage || sysTablesPageId != 1) {
        LOG_ERROR("Failed to create sys_tables page");
        if (sysTablesPage) {
            bufferPool_->unpinPage(sysTablesPageId, false);
        }
        return false;
    }
    sysTablesPage->setPageType(PageType::TABLE_PAGE);
    TablePage::initialize(sysTablesPage);
    sysTablesFirstPage_ = sysTablesPageId;
    bufferPool_->unpinPage(sysTablesPageId, true);

    // 创建sys_columns表的第一个页面（页面ID=2）
    PageId sysColumnsPageId = INVALID_PAGE_ID;
    Page* sysColumnsPage = bufferPool_->newPage(&sysColumnsPageId);
    if (!sysColumnsPage || sysColumnsPageId != 2) {
        LOG_ERROR("Failed to create sys_columns page");
        if (sysColumnsPage) {
            bufferPool_->unpinPage(sysColumnsPageId, false);
        }
        return false;
    }
    sysColumnsPage->setPageType(PageType::TABLE_PAGE);
    TablePage::initialize(sysColumnsPage);
    sysColumnsFirstPage_ = sysColumnsPageId;
    bufferPool_->unpinPage(sysColumnsPageId, true);

    // 创建sys_indexes表的第一个页面（页面ID=3）
    PageId sysIndexesPageId = INVALID_PAGE_ID;
    Page* sysIndexesPage = bufferPool_->newPage(&sysIndexesPageId);
    if (!sysIndexesPage || sysIndexesPageId != 3) {
        LOG_ERROR("Failed to create sys_indexes page");
        if (sysIndexesPage) {
            bufferPool_->unpinPage(sysIndexesPageId, false);
        }
        return false;
    }
    sysIndexesPage->setPageType(PageType::TABLE_PAGE);
    TablePage::initialize(sysIndexesPage);
    sysIndexesFirstPage_ = sysIndexesPageId;
    bufferPool_->unpinPage(sysIndexesPageId, true);

    LOG_INFO(QString("System tables created: sys_tables=%1, sys_columns=%2, sys_indexes=%3")
        .arg(sysTablesFirstPage_)
        .arg(sysColumnsFirstPage_)
        .arg(sysIndexesFirstPage_));

    return true;
}

bool CatalogDbBackend::saveCatalog(
    const QHash<QString, std::shared_ptr<TableDef>>& tables,
    const QHash<QString, IndexDef>& indexes)
{
    LOG_INFO("Saving catalog to database");

    // 确保系统表页面ID已设置
    if (sysTablesFirstPage_ == INVALID_PAGE_ID) {
        sysTablesFirstPage_ = 1;
        sysColumnsFirstPage_ = 2;
        sysIndexesFirstPage_ = 3;
    }

    // 清空现有数据
    if (!clearSystemTables()) {
        LOG_ERROR("Failed to clear system tables");
        return false;
    }

    // 保存所有表定义
    for (auto it = tables.begin(); it != tables.end(); ++it) {
        const TableDef& table = *it.value();

        if (!saveTableDef(table)) {
            LOG_ERROR(QString("Failed to save table '%1'").arg(table.name));
            return false;
        }

        // 保存列定义
        for (int i = 0; i < table.columns.size(); ++i) {
            if (!saveColumnDef(table.name, table.columns[i], i)) {
                LOG_ERROR(QString("Failed to save column '%1.%2'")
                    .arg(table.name).arg(table.columns[i].name));
                return false;
            }
        }
    }

    // 保存所有索引定义
    for (auto it = indexes.begin(); it != indexes.end(); ++it) {
        if (!saveIndexDef(it.value())) {
            LOG_ERROR(QString("Failed to save index '%1'").arg(it.value().name));
            return false;
        }
    }

    // 刷新所有页面到磁盘
    bufferPool_->flushAllPages();

    LOG_INFO(QString("Catalog saved: %1 tables, %2 indexes")
        .arg(tables.size()).arg(indexes.size()));

    return true;
}

bool CatalogDbBackend::saveTableDef(const TableDef& table) {
    // 序列化表定义
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    stream << table.name;
    stream << static_cast<qint64>(table.firstPageId);
    stream << static_cast<qint64>(table.nextRowId);

    // 插入到sys_tables表
    Page* page = bufferPool_->fetchPage(sysTablesFirstPage_);
    if (!page) {
        LOG_ERROR("Failed to fetch sys_tables page");
        return false;
    }

    RowId rowId = 1;  // 简单的行ID分配
    bool success = TablePage::insertTuple(page, data, &rowId);
    bufferPool_->unpinPage(sysTablesFirstPage_, true);

    return success;
}

bool CatalogDbBackend::saveColumnDef(const QString& tableName, const ColumnDef& column, int order) {
    // 序列化列定义
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    stream << tableName;
    stream << column.name;
    stream << static_cast<qint32>(order);
    stream << static_cast<qint32>(column.type);
    stream << static_cast<qint32>(column.length);
    stream << static_cast<qint32>(column.notNull ? 1 : 0);
    stream << static_cast<qint32>(column.primaryKey ? 1 : 0);
    stream << static_cast<qint32>(column.autoIncrement ? 1 : 0);

    // 插入到sys_columns表
    Page* page = bufferPool_->fetchPage(sysColumnsFirstPage_);
    if (!page) {
        LOG_ERROR("Failed to fetch sys_columns page");
        return false;
    }

    RowId rowId = 1;
    bool success = TablePage::insertTuple(page, data, &rowId);
    bufferPool_->unpinPage(sysColumnsFirstPage_, true);

    return success;
}

bool CatalogDbBackend::saveIndexDef(const IndexDef& index) {
    // 序列化索引定义
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    stream << index.name;
    stream << index.tableName;
    stream << static_cast<qint32>(index.indexType);
    stream << static_cast<qint32>(index.keyType);
    stream << static_cast<qint32>(index.unique ? 1 : 0);
    stream << static_cast<qint32>(index.autoCreated ? 1 : 0);
    stream << static_cast<qint64>(index.rootPageId);

    // 序列化列名列表
    QString columnsStr = index.columns.join(",");
    stream << columnsStr;

    // 插入到sys_indexes表
    Page* page = bufferPool_->fetchPage(sysIndexesFirstPage_);
    if (!page) {
        LOG_ERROR("Failed to fetch sys_indexes page");
        return false;
    }

    RowId rowId = 1;
    bool success = TablePage::insertTuple(page, data, &rowId);
    bufferPool_->unpinPage(sysIndexesFirstPage_, true);

    return success;
}

bool CatalogDbBackend::loadCatalog(
    QHash<QString, std::shared_ptr<TableDef>>& tables,
    QHash<QString, IndexDef>& indexes)
{
    LOG_INFO("Loading catalog from database");

    // 确保系统表页面ID已设置
    if (sysTablesFirstPage_ == INVALID_PAGE_ID) {
        sysTablesFirstPage_ = 1;
        sysColumnsFirstPage_ = 2;
        sysIndexesFirstPage_ = 3;
    }

    tables.clear();
    indexes.clear();

    // 加载表定义
    if (!loadTableDefs(tables)) {
        LOG_ERROR("Failed to load table definitions");
        return false;
    }

    // 加载列定义
    if (!loadColumnDefs(tables)) {
        LOG_ERROR("Failed to load column definitions");
        return false;
    }

    // 加载索引定义
    if (!loadIndexDefs(tables, indexes)) {
        LOG_ERROR("Failed to load index definitions");
        return false;
    }

    LOG_INFO(QString("Catalog loaded: %1 tables, %2 indexes")
        .arg(tables.size()).arg(indexes.size()));

    return true;
}

bool CatalogDbBackend::loadTableDefs(QHash<QString, std::shared_ptr<TableDef>>& tables) {
    Page* page = bufferPool_->fetchPage(sysTablesFirstPage_);
    if (!page) {
        LOG_ERROR("Failed to fetch sys_tables page");
        return false;
    }

    // 遍历所有元组
    uint16_t slotCount = TablePage::getSlotCount(page);

    for (uint16_t i = 0; i < slotCount; ++i) {
        QByteArray tupleData;
        if (!TablePage::getTuple(page, i, tupleData)) {
            continue;  // 跳过已删除的槽位
        }

        // 反序列化表定义
        QDataStream stream(tupleData);
        QString tableName;
        qint64 firstPageId, nextRowId;

        stream >> tableName >> firstPageId >> nextRowId;

        auto table = std::make_shared<TableDef>();
        table->name = tableName;
        table->firstPageId = static_cast<PageId>(firstPageId);
        table->nextRowId = static_cast<RowId>(nextRowId);

        tables[tableName.toLower()] = table;
    }

    bufferPool_->unpinPage(sysTablesFirstPage_, false);
    return true;
}

bool CatalogDbBackend::loadColumnDefs(QHash<QString, std::shared_ptr<TableDef>>& tables) {
    Page* page = bufferPool_->fetchPage(sysColumnsFirstPage_);
    if (!page) {
        LOG_ERROR("Failed to fetch sys_columns page");
        return false;
    }

    // 遍历所有元组
    uint16_t slotCount = TablePage::getSlotCount(page);

    for (uint16_t i = 0; i < slotCount; ++i) {
        QByteArray tupleData;
        if (!TablePage::getTuple(page, i, tupleData)) {
            continue;
        }

        // 反序列化列定义
        QDataStream stream(tupleData);
        QString tableName, columnName;
        qint32 order, type, length, notNull, primaryKey, autoIncrement;

        stream >> tableName >> columnName >> order >> type >> length
               >> notNull >> primaryKey >> autoIncrement;

        QString lowerTableName = tableName.toLower();
        if (!tables.contains(lowerTableName)) {
            LOG_WARN(QString("Table '%1' not found for column '%2'")
                .arg(tableName).arg(columnName));
            continue;
        }

        ColumnDef column;
        column.name = columnName;
        column.type = static_cast<DataType>(type);
        column.length = length;
        column.notNull = (notNull != 0);
        column.primaryKey = (primaryKey != 0);
        column.autoIncrement = (autoIncrement != 0);

        // 确保按顺序插入列
        auto& table = tables[lowerTableName];
        if (order >= table->columns.size()) {
            table->columns.resize(order + 1);
        }
        table->columns[order] = column;
    }

    bufferPool_->unpinPage(sysColumnsFirstPage_, false);
    return true;
}

bool CatalogDbBackend::loadIndexDefs(
    QHash<QString, std::shared_ptr<TableDef>>& tables,
    QHash<QString, IndexDef>& indexes)
{
    Page* page = bufferPool_->fetchPage(sysIndexesFirstPage_);
    if (!page) {
        LOG_ERROR("Failed to fetch sys_indexes page");
        return false;
    }

    // 遍历所有元组
    uint16_t slotCount = TablePage::getSlotCount(page);

    for (uint16_t i = 0; i < slotCount; ++i) {
        QByteArray tupleData;
        if (!TablePage::getTuple(page, i, tupleData)) {
            continue;
        }

        // 反序列化索引定义
        QDataStream stream(tupleData);
        QString indexName, tableName, columnsStr;
        qint32 indexType, keyType, unique, autoCreated;
        qint64 rootPageId;

        stream >> indexName >> tableName >> indexType >> keyType
               >> unique >> autoCreated >> rootPageId >> columnsStr;

        IndexDef index;
        index.name = indexName;
        index.tableName = tableName;
        index.indexType = static_cast<IndexType>(indexType);
        index.keyType = static_cast<DataType>(keyType);
        index.unique = (unique != 0);
        index.autoCreated = (autoCreated != 0);
        index.rootPageId = static_cast<PageId>(rootPageId);

        // 解析列名列表
        QStringList columnList = columnsStr.split(",", Qt::SkipEmptyParts);
        for (const QString& col : columnList) {
            index.columns.append(col);
        }

        indexes[indexName.toLower()] = index;

        // 同时添加到表定义中
        QString lowerTableName = tableName.toLower();
        if (tables.contains(lowerTableName)) {
            tables[lowerTableName]->indexes.append(index);
        }
    }

    bufferPool_->unpinPage(sysIndexesFirstPage_, false);
    return true;
}

bool CatalogDbBackend::clearSystemTables() {
    // 清空sys_tables
    Page* page = bufferPool_->fetchPage(sysTablesFirstPage_);
    if (page) {
        TablePage::initialize(page);
        bufferPool_->unpinPage(sysTablesFirstPage_, true);
    }

    // 清空sys_columns
    page = bufferPool_->fetchPage(sysColumnsFirstPage_);
    if (page) {
        TablePage::initialize(page);
        bufferPool_->unpinPage(sysColumnsFirstPage_, true);
    }

    // 清空sys_indexes
    page = bufferPool_->fetchPage(sysIndexesFirstPage_);
    if (page) {
        TablePage::initialize(page);
        bufferPool_->unpinPage(sysIndexesFirstPage_, true);
    }

    return true;
}

} // namespace qindb
