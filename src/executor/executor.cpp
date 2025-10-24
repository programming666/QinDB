#include "qindb/executor.h"
#include "qindb/logger.h"
#include "qindb/table_page.h"
#include "qindb/expression_evaluator.h"
#include "qindb/bplus_tree.h"
#include "qindb/generic_bplustree.h"
#include "qindb/hash_index.h"
#include "qindb/inverted_index.h"
#include "qindb/key_comparator.h"
#include "qindb/visibility_checker.h"
#include "qindb/vacuum.h"
#include "qindb/query_rewriter.h"
#include "qindb/statistics.h"
#include "qindb/cost_optimizer.h"
#include "qindb/permission_manager.h"
#include "qindb/query_cache.h"
#include <algorithm>

namespace qindb {

using namespace ast;  // 使用AST命名空间

Executor::Executor(DatabaseManager* dbManager)
    : dbManager_(dbManager)
    , queryRewriter_(std::make_unique<QueryRewriter>())
    , queryRewriteEnabled_(true)
    , queryCache_(std::make_unique<QueryCache>())
{
    LOG_INFO("Executor initialized with query rewriter and query cache");
}

Executor::~Executor() {
    LOG_INFO("Executor destroyed");
}

QueryResult Executor::execute(const std::unique_ptr<ASTNode>& ast) {
    if (!ast) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Null AST node");
    }

    // 数据库操作（不需要当前数据库）
    if (auto* createDBStmt = dynamic_cast<const CreateDatabaseStatement*>(ast.get())) {
        return executeCreateDatabase(createDBStmt);
    }
    if (auto* dropDBStmt = dynamic_cast<const DropDatabaseStatement*>(ast.get())) {
        return executeDropDatabase(dropDBStmt);
    }
    if (auto* useDBStmt = dynamic_cast<const UseDatabaseStatement*>(ast.get())) {
        return executeUseDatabase(useDBStmt);
    }
    if (auto* showDBsStmt = dynamic_cast<const ShowDatabasesStatement*>(ast.get())) {
        return executeShowDatabases(showDBsStmt);
    }

    // 表操作（使用 dynamic_cast 确定语句类型并执行）
    if (auto* createStmt = dynamic_cast<const CreateTableStatement*>(ast.get())) {
        return executeCreateTable(createStmt);
    }
    if (auto* dropStmt = dynamic_cast<const DropTableStatement*>(ast.get())) {
        return executeDropTable(dropStmt);
    }
    if (auto* insertStmt = dynamic_cast<const InsertStatement*>(ast.get())) {
        QueryResult permError;
        if (!ensurePermission(dbManager_->currentDatabaseName(), insertStmt->tableName, PermissionType::INSERT, permError)) {
            return permError;
        }
        return executeInsert(insertStmt);
    }
    if (auto* selectStmt = dynamic_cast<const SelectStatement*>(ast.get())) {
        QueryResult permError;
        if (!checkSelectPermissions(selectStmt, permError)) {
            return permError;
        }
        return executeSelect(selectStmt);
    }
    if (auto* updateStmt = dynamic_cast<const UpdateStatement*>(ast.get())) {
        QueryResult permError;
        if (!ensurePermission(dbManager_->currentDatabaseName(), updateStmt->tableName, PermissionType::UPDATE, permError)) {
            return permError;
        }
        return executeUpdate(updateStmt);
    }
    if (auto* deleteStmt = dynamic_cast<const DeleteStatement*>(ast.get())) {
        QueryResult permError;
        if (!ensurePermission(dbManager_->currentDatabaseName(), deleteStmt->tableName, PermissionType::DELETE, permError)) {
            return permError;
        }
        return executeDelete(deleteStmt);
    }
    if (dynamic_cast<const ast::ShowTablesStatement*>(ast.get())) {
        return executeShowTables();
    }
    if (dynamic_cast<const SaveStatement*>(ast.get())) {
        return executeSave();
    }
    if (auto* createIndexStmt = dynamic_cast<const CreateIndexStatement*>(ast.get())) {
        return executeCreateIndex(createIndexStmt);
    }
    if (auto* dropIndexStmt = dynamic_cast<const DropIndexStatement*>(ast.get())) {
        return executeDropIndex(dropIndexStmt);
    }
    if (auto* vacuumStmt = dynamic_cast<const VacuumStatement*>(ast.get())) {
        return executeVacuum(vacuumStmt);
    }
    if (auto* analyzeStmt = dynamic_cast<const AnalyzeStatement*>(ast.get())) {
        return executeAnalyze(analyzeStmt);
    }
    if (auto* explainStmt = dynamic_cast<const ExplainStatement*>(ast.get())) {
        return executeExplain(explainStmt);
    }
    if (auto* beginStmt = dynamic_cast<const BeginTransactionStatement*>(ast.get())) {
        return executeBegin(beginStmt);
    }
    if (auto* commitStmt = dynamic_cast<const CommitStatement*>(ast.get())) {
        return executeCommit(commitStmt);
    }
    if (auto* rollbackStmt = dynamic_cast<const RollbackStatement*>(ast.get())) {
        return executeRollback(rollbackStmt);
    }

    // 用户管理操作
    if (auto* createUserStmt = dynamic_cast<const CreateUserStatement*>(ast.get())) {
        return executeCreateUser(createUserStmt);
    }
    if (auto* dropUserStmt = dynamic_cast<const DropUserStatement*>(ast.get())) {
        return executeDropUser(dropUserStmt);
    }
    if (auto* alterUserStmt = dynamic_cast<const AlterUserStatement*>(ast.get())) {
        return executeAlterUser(alterUserStmt);
    }

    // 权限管理操作
    if (auto* grantStmt = dynamic_cast<const GrantStatement*>(ast.get())) {
        return executeGrant(grantStmt);
    }
    if (auto* revokeStmt = dynamic_cast<const RevokeStatement*>(ast.get())) {
        return executeRevoke(revokeStmt);
    }

    return createErrorResult(ErrorCode::NOT_IMPLEMENTED,
                            QString("Statement type not implemented"));
}

QueryResult Executor::executeCreateTable(const CreateTableStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid CREATE TABLE statement");
    }

    if (authManager_ && !currentUser_.isEmpty() && !authManager_->isUserAdmin(currentUser_)) {
        return createErrorResult(ErrorCode::PERMISSION_DENIED,
                                QString("User '%1' cannot create tables").arg(currentUser_));
    }

    LOG_INFO(QString("Executing CREATE TABLE: %1").arg(stmt->tableName));

    // 获取当前数据库的组件
    // 获取当前数据库的组件
    Catalog* catalog = dbManager_->getCurrentCatalog();

    if (!catalog) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 检查表是否已存在
    if (catalog->tableExists(stmt->tableName)) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                QString("Table '%1' already exists").arg(stmt->tableName));
    }

    // 构建表定义
    TableDef tableDef(stmt->tableName);

    for (const auto& colDef : stmt->columns) {
        ColumnDef col;
        col.name = colDef.name;
        col.type = colDef.type;  // ColumnDefinition已经包含DataType
        col.length = colDef.length;
        col.notNull = colDef.notNull;
        col.primaryKey = colDef.primaryKey;
        col.autoIncrement = colDef.autoIncrement;

        tableDef.columns.append(col);
    }

    // 分配第一个数据页
    BufferPoolManager* bufferPool = dbManager_->getCurrentBufferPool();
    if (!bufferPool) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }
    
    PageId firstPageId;
    Page* firstPage = bufferPool->newPage(&firstPageId);
    if (!firstPage) {
        return createErrorResult(ErrorCode::IO_ERROR, "Failed to allocate page for table");
    }

    tableDef.firstPageId = firstPageId;

    // 初始化页为TABLE_PAGE
    TablePage::init(firstPage, firstPageId);
    bufferPool->unpinPage(firstPageId, true);

    // 创建表
    if (!catalog->createTable(tableDef)) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Failed to create table in catalog");
    }

    // 保存元数据到当前数据库目录
    QString dbPath = dbManager_->getDatabasePath(dbManager_->currentDatabaseName());
    QString catalogPath = dbPath + "/catalog.json";
    catalog->save(catalogPath);

    LOG_INFO(QString("Table '%1' created successfully with %2 columns")
                 .arg(stmt->tableName)
                 .arg(tableDef.columns.size()));

    return createSuccessResult(QString("Table '%1' created").arg(stmt->tableName));
}

QueryResult Executor::executeDropTable(const DropTableStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid DROP TABLE statement");
    }

    if (authManager_ && !currentUser_.isEmpty() && !authManager_->isUserAdmin(currentUser_)) {
        return createErrorResult(ErrorCode::PERMISSION_DENIED,
                                QString("User '%1' cannot drop tables").arg(currentUser_));
    }

    LOG_INFO(QString("Executing DROP TABLE: %1").arg(stmt->tableName));

    // 获取当前数据库的组件
    Catalog* catalog = dbManager_->getCurrentCatalog();
    BufferPoolManager* bufferPool = dbManager_->getCurrentBufferPool();

    if (!catalog || !bufferPool) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 检查表是否存在
    if (!catalog->tableExists(stmt->tableName)) {
        return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                QString("Table '%1' does not exist").arg(stmt->tableName));
    }

    // 删除表
    if (!catalog->dropTable(stmt->tableName)) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Failed to drop table");
    }

    // 保存元数据到当前数据库目录
    QString dbPath = dbManager_->getDatabasePath(dbManager_->currentDatabaseName());
    QString catalogPath = dbPath + "/catalog.json";
    catalog->save(catalogPath);

    // 使缓存失效
    if (queryCache_) {
        int invalidated = queryCache_->invalidateTable(stmt->tableName);
        if (invalidated > 0) {
            LOG_DEBUG(QString("Invalidated %1 cache entries for table '%2'")
                         .arg(invalidated).arg(stmt->tableName));
        }
    }

    LOG_INFO(QString("Table '%1' dropped successfully").arg(stmt->tableName));

    return createSuccessResult(QString("Table '%1' dropped").arg(stmt->tableName));
}

QueryResult Executor::executeInsert(const InsertStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid INSERT statement");
    }

    LOG_INFO(QString("Executing INSERT INTO: %1").arg(stmt->tableName));

    // 获取当前数据库的组件
    Catalog* catalog = dbManager_->getCurrentCatalog();
    BufferPoolManager* bufferPool = dbManager_->getCurrentBufferPool();
    WALManager* walManager = dbManager_->getCurrentWALManager();
    TransactionManager* txnManager = dbManager_->getCurrentTransactionManager();

    if (!catalog || !bufferPool || !walManager || !txnManager) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 检查表是否存在
    const TableDef* table = catalog->getTable(stmt->tableName);
    if (!table) {
        return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                QString("Table '%1' does not exist").arg(stmt->tableName));
    }

    // 检查是否有会话事务
    TransactionId sessionTxnId = dbManager_->getCurrentTransactionId();
    bool autoCommit = (sessionTxnId == INVALID_TXN_ID);

    TransactionId txnId;
    if (autoCommit) {
        // 自动提交模式：创建新事务
        txnId = txnManager->beginTransaction();
        LOG_INFO(QString("Transaction %1 started for INSERT (auto-commit)").arg(txnId));
    } else {
        // 使用会话事务
        txnId = sessionTxnId;
        LOG_INFO(QString("Using session transaction %1 for INSERT").arg(txnId));
    }

    // 创建表达式求值器
    ExpressionEvaluator evaluator(catalog);
    int insertedCount = 0;

    // 用于追踪需要复制的表定义（因为我们需要修改 nextRowId）
    TableDef mutableTable = *table;

    // 处理每一行数据
    for (const auto& rowExprs : stmt->values) {
        // 求值所有表达式
        QVector<QVariant> values = evaluator.evaluateList(rowExprs);

        if (evaluator.hasError()) {
            return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                    QString("Failed to evaluate expression: %1")
                                        .arg(evaluator.getLastError()));
        }

        // 检查列数匹配
        if (values.size() != table->columns.size()) {
            return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                    QString("Column count mismatch: expected %1, got %2")
                                        .arg(table->columns.size())
                                        .arg(values.size()));
        }

        // 验证和类型转换
        for (int i = 0; i < table->columns.size(); ++i) {
            const ColumnDef& colDef = table->columns[i];
            QVariant& value = values[i];

            // 检查 NOT NULL 约束
            if (value.isNull() && colDef.notNull && !colDef.autoIncrement) {
                return createErrorResult(ErrorCode::CONSTRAINT_VIOLATION,
                                        QString("Column '%1' cannot be NULL")
                                            .arg(colDef.name));
            }

            // 处理 AUTO_INCREMENT
            if (colDef.autoIncrement && value.isNull()) {
                // 使用表的 nextRowId 作为自增值
                value = QVariant::fromValue(mutableTable.nextRowId);
            }
        }

        // 计算记录大小
        uint16_t recordSize = TablePage::calculateRecordSize(table, values);
        if (recordSize == 0) {
            return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                    "Failed to calculate record size");
        }

        // 从第一页开始查找有足够空间的页
        PageId currentPageId = mutableTable.firstPageId;
        bool inserted = false;

        while (currentPageId != INVALID_PAGE_ID && !inserted) {
            Page* page = bufferPool->fetchPage(currentPageId);
            if (!page) {
                txnManager->abortTransaction(txnId);
                return createErrorResult(ErrorCode::IO_ERROR,
                                        QString("Failed to fetch page %1").arg(currentPageId));
            }

            // 检查页是否有足够空间
            if (TablePage::hasEnoughSpace(page, recordSize)) {
                // 生成行ID（使用表的 nextRowId）
                RowId rowId = mutableTable.nextRowId++;

                // 插入记录（传入事务ID）
                if (TablePage::insertRecord(page, table, rowId, values, txnId)) {
                    // 获取插入位置的 slotIndex（假设insertRecord返回最后插入的slot）
                    PageHeader* header = page->getHeader();
                    uint16_t slotIndex = header->slotCount - 1;

                    // 更新 RowIdIndex
                    RowLocation location(currentPageId, slotIndex);
                    mutableTable.rowIdIndex->insert(rowId, location);

                    // 写入 WAL 记录
                    QByteArray walData;
                    QDataStream walStream(&walData, QIODevice::WriteOnly);
                    walStream << stmt->tableName << rowId << currentPageId << slotIndex;
                    WALRecord walRecord(WALRecordType::INSERT, txnId, walData);
                    uint64_t lsn = walManager->writeRecord(walRecord);

                    // 如果是会话事务，添加 Undo 记录
                    if (!autoCommit) {
                        UndoRecord undoRecord = UndoRecord::createInsertUndo(
                            stmt->tableName,
                            currentPageId,
                            slotIndex,
                            lsn
                        );
                        txnManager->addUndoRecord(txnId, undoRecord);
                    }

                    inserted = true;
                    insertedCount++;
                    bufferPool->unpinPage(currentPageId, true);  // 标记为脏页
                    break;
                }
            }

            // 获取下一页
            PageHeader* header = page->getHeader();
            PageId nextPageId = header->nextPageId;
            bufferPool->unpinPage(currentPageId, false);
            currentPageId = nextPageId;
        }

        // 如果没有现有页有空间，分配新页
        if (!inserted) {
            PageId newPageId;
            Page* newPage = bufferPool->newPage(&newPageId);
            if (!newPage) {
                txnManager->abortTransaction(txnId);
                return createErrorResult(ErrorCode::IO_ERROR,
                                        "Failed to allocate new page for table");
            }

            // 初始化新页
            TablePage::init(newPage, newPageId);

            // 链接到前一页（如果有）
            if (mutableTable.firstPageId != INVALID_PAGE_ID) {
                // 找到链表末尾
                PageId lastPageId = mutableTable.firstPageId;
                while (lastPageId != INVALID_PAGE_ID) {
                    Page* tmpPage = bufferPool->fetchPage(lastPageId);
                    if (!tmpPage) break;

                    PageHeader* tmpHeader = tmpPage->getHeader();
                    if (tmpHeader->nextPageId == INVALID_PAGE_ID) {
                        // 找到末尾页，链接新页
                        tmpHeader->nextPageId = newPageId;
                        bufferPool->unpinPage(lastPageId, true);
                        break;
                    }

                    PageId nextId = tmpHeader->nextPageId;
                    bufferPool->unpinPage(lastPageId, false);
                    lastPageId = nextId;
                }
            }

            // 插入记录到新页（传入事务ID）
            RowId rowId = mutableTable.nextRowId++;

            if (TablePage::insertRecord(newPage, table, rowId, values, txnId)) {
                // 获取插入位置的 slotIndex
                PageHeader* header = newPage->getHeader();
                uint16_t slotIndex = header->slotCount - 1;

                // 更新 RowIdIndex
                RowLocation location(newPageId, slotIndex);
                mutableTable.rowIdIndex->insert(rowId, location);

                // 写入 WAL 记录
                QByteArray walData;
                QDataStream walStream(&walData, QIODevice::WriteOnly);
                walStream << stmt->tableName << rowId << newPageId << slotIndex;
                WALRecord walRecord(WALRecordType::INSERT, txnId, walData);
                uint64_t lsn = walManager->writeRecord(walRecord);

                // 如果是会话事务，添加 Undo 记录
                if (!autoCommit) {
                    UndoRecord undoRecord = UndoRecord::createInsertUndo(
                        stmt->tableName,
                        newPageId,
                        slotIndex,
                        lsn
                    );
                    txnManager->addUndoRecord(txnId, undoRecord);
                }

                inserted = true;
                insertedCount++;
            }

            bufferPool->unpinPage(newPageId, true);
        }

        if (!inserted) {
            txnManager->abortTransaction(txnId);
            return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                    "Failed to insert record");
        }

        // 更新所有索引
        QVector<IndexDef> tableIndexes = catalog->getTableIndexes(stmt->tableName);
        for (const auto& indexDef : tableIndexes) {
            // 只处理单列索引
            if (indexDef.columns.size() != 1) continue;

            QString columnName = indexDef.columns[0];
            int columnIndex = table->getColumnIndex(columnName);
            if (columnIndex < 0) continue;

            // 获取索引键值
            QVariant keyValue = values[columnIndex];
            if (keyValue.isNull()) continue; // 跳过NULL值

            // 使用通用 B+ 树插入（支持所有数据类型）
            RowId lastInsertedRowId = mutableTable.nextRowId - 1;
            GenericBPlusTree genericBTree(bufferPool, indexDef.keyType, indexDef.rootPageId);

            if (!genericBTree.insert(keyValue, lastInsertedRowId)) {
                LOG_WARN(QString("Failed to update index '%1' for inserted row")
                            .arg(indexDef.name));
            } else {
                LOG_DEBUG(QString("Updated index '%1': inserted key")
                             .arg(indexDef.name));
            }
        }
    }

    // 更新表定义到 Catalog（保存 nextRowId 和 rowIdIndex）
    if (!catalog->updateTable(stmt->tableName, mutableTable)) {
        if (autoCommit) {
            txnManager->abortTransaction(txnId);
        }
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                "Failed to update table metadata");
    }

    // 提交事务（仅在自动提交模式下）
    if (autoCommit) {
        if (!txnManager->commitTransaction(txnId)) {
            txnManager->abortTransaction(txnId);
            return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                    "Failed to commit transaction");
        }
        LOG_INFO(QString("Transaction %1 committed (auto-commit)").arg(txnId));
    }

    // 保存 Catalog 到磁盘（确保 nextRowId 持久化）
    QString dbPath = dbManager_->getDatabasePath(dbManager_->currentDatabaseName());
    QString catalogPath = dbPath + "/catalog.json";
    if (!catalog->save(catalogPath)) {
        LOG_ERROR("Failed to save catalog after INSERT");
    }

    // 刷新所有脏页
    bufferPool->flushAllPages();

    // 使缓存失效
    if (queryCache_) {
        int invalidated = queryCache_->invalidateTable(stmt->tableName);
        if (invalidated > 0) {
            LOG_DEBUG(QString("Invalidated %1 cache entries for table '%2'")
                         .arg(invalidated).arg(stmt->tableName));
        }
    }

    LOG_INFO(QString("INSERT INTO '%1': %2 row(s) inserted")
                .arg(stmt->tableName)
                .arg(insertedCount));

    return createSuccessResult(QString("Inserted %1 row(s) into '%2'")
                                   .arg(insertedCount)
                                   .arg(stmt->tableName));
}

QueryResult Executor::executeSelect(const SelectStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid SELECT statement");
    }

    // 应用查询重写优化
    std::unique_ptr<SelectStatement> rewrittenStmt;
    const SelectStatement* actualStmt = stmt;

    if (queryRewriteEnabled_ && queryRewriter_) {
        LOG_INFO("Applying query rewrite optimizations...");
        rewrittenStmt = queryRewriter_->rewrite(stmt);
        if (rewrittenStmt) {
            actualStmt = rewrittenStmt.get();
            const auto& stats = queryRewriter_->getStats();
            LOG_INFO(QString("Query rewrite stats: predicates=%1, constants=%2, columns=%3, subqueries=%4")
                    .arg(stats.predicatesPushedDown)
                    .arg(stats.constantsFolded)
                    .arg(stats.columnsPruned)
                    .arg(stats.subqueriesUnnested));
            if (stats.predicatesPushedDown > 0 || stats.constantsFolded > 0) {
                LOG_DEBUG(QString("Rewrite log:\n%1").arg(stats.rewriteLog));
            }
        } else {
            LOG_WARN("Query rewrite failed, using original statement");
        }
    }

    QString fromTable = actualStmt->from ? actualStmt->from->tableName : "(none)";
    LOG_INFO(QString("Executing SELECT FROM: %1").arg(fromTable));

    // 尝试从查询缓存中获取结果
    QString querySql;
    QueryResult cachedResult;
    bool usedCache = false;

    if (queryCache_ && queryCache_->isEnabled() && actualStmt->from) {
        // 生成缓存键（简化实现：使用表名 + WHERE toString）
        querySql = QString("SELECT FROM %1").arg(fromTable);
        if (actualStmt->where) {
            querySql += " WHERE " + actualStmt->where->toString();
        }
        if (!actualStmt->orderBy.empty()) {
            querySql += " ORDER BY ...";
        }
        if (actualStmt->limit > 0) {
            querySql += QString(" LIMIT %1").arg(actualStmt->limit);
        }

        // 标准化查询键
        querySql = QueryCache::normalizeQuery(querySql);

        // 查找缓存
        if (queryCache_->get(querySql, cachedResult)) {
            LOG_INFO(QString("Query cache HIT: %1").arg(querySql.left(60)));
            usedCache = true;
            return cachedResult;
        } else {
            LOG_DEBUG(QString("Query cache MISS: %1").arg(querySql.left(60)));
        }
    }

    QueryResult result;
    result.success = true;

    // 检查表是否存在
    if (actualStmt->from) {
        // 获取当前数据库的组件
        Catalog* catalog = dbManager_->getCurrentCatalog();
        BufferPoolManager* bufferPool = dbManager_->getCurrentBufferPool();

        if (!catalog || !bufferPool) {
            return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
        }

        const QString& leftTableName = actualStmt->from->tableName;
        const TableDef* leftTable = catalog->getTable(leftTableName);

        if (!leftTable) {
            return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                    QString("Table '%1' does not exist").arg(leftTableName));
        }

        // 创建表达式求值器（用于WHERE子句和JOIN条件）
        ExpressionEvaluator evaluator(catalog);

        // 检查是否有JOIN子句
        if (!actualStmt->joins.empty()) {
            // 执行JOIN操作（NestedLoopJoin）
            LOG_INFO(QString("Executing JOIN: %1 tables").arg(actualStmt->joins.size() + 1));

            // 获取右表信息
            const JoinClause* joinClause = actualStmt->joins[0].get();
            const QString& rightTableName = joinClause->right->tableName;
            const TableDef* rightTable = catalog->getTable(rightTableName);

            if (!rightTable) {
                return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                        QString("Table '%1' does not exist").arg(rightTableName));
            }

            // 设置列名：左表列 + 右表列
            for (const auto& col : leftTable->columns) {
                result.columnNames.append(leftTableName + "." + col.name);
            }
            for (const auto& col : rightTable->columns) {
                result.columnNames.append(rightTableName + "." + col.name);
            }

            // 读取左表的所有记录
            QVector<QVector<QVariant>> leftRecords;
            PageId leftPageId = leftTable->firstPageId;

            while (leftPageId != INVALID_PAGE_ID) {
                Page* page = bufferPool->fetchPage(leftPageId);
                if (!page) {
                    LOG_ERROR(QString("Failed to fetch page %1").arg(leftPageId));
                    break;
                }

                QVector<QVector<QVariant>> pageRecords;
                if (TablePage::getAllRecords(page, leftTable, pageRecords)) {
                    leftRecords.append(pageRecords);
                }

                PageHeader* header = page->getHeader();
                PageId nextPageId = header->nextPageId;
                bufferPool->unpinPage(leftPageId, false);
                leftPageId = nextPageId;
            }

            // 读取右表的所有记录
            QVector<QVector<QVariant>> rightRecords;
            PageId rightPageId = rightTable->firstPageId;

            while (rightPageId != INVALID_PAGE_ID) {
                Page* page = bufferPool->fetchPage(rightPageId);
                if (!page) {
                    LOG_ERROR(QString("Failed to fetch page %1").arg(rightPageId));
                    break;
                }

                QVector<QVector<QVariant>> pageRecords;
                if (TablePage::getAllRecords(page, rightTable, pageRecords)) {
                    rightRecords.append(pageRecords);
                }

                PageHeader* header = page->getHeader();
                PageId nextPageId = header->nextPageId;
                bufferPool->unpinPage(rightPageId, false);
                rightPageId = nextPageId;
            }

            // NestedLoopJoin: 对于左表的每一行，遍历右表的所有行
            int totalRows = 0;
            for (const auto& leftRow : leftRecords) {
                for (const auto& rightRow : rightRecords) {
                    // 合并左右行
                    QVector<QVariant> joinedRow;
                    joinedRow.append(leftRow);
                    joinedRow.append(rightRow);

                    bool includeRow = true;

                    // 评估JOIN条件
                    if (joinClause->condition) {
                        // 创建临时的组合表定义用于求值
                        // 简化实现：直接评估条件（需要扩展ExpressionEvaluator支持多表）
                        // 这里暂时使用简化的方法
                        QVariant joinResult = evaluator.evaluateWithRow(joinClause->condition.get(), leftTable, joinedRow);

                        if (evaluator.hasError()) {
                            return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                                    QString("JOIN condition evaluation error: %1")
                                                        .arg(evaluator.getLastError()));
                        }

                        includeRow = !joinResult.isNull() && joinResult.toBool();
                    }

                    // 评估WHERE条件
                    if (includeRow && actualStmt->where) {
                        QVariant whereResult = evaluator.evaluateWithRow(actualStmt->where.get(), leftTable, joinedRow);

                        if (evaluator.hasError()) {
                            return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                                    QString("WHERE clause evaluation error: %1")
                                                        .arg(evaluator.getLastError()));
                        }

                        includeRow = !whereResult.isNull() && whereResult.toBool();
                    }

                    if (includeRow) {
                        result.rows.append(joinedRow);
                        totalRows++;
                    }
                }
            }

            result.message = QString("SELECT executed (%1 rows from JOIN)").arg(totalRows);

            LOG_INFO(QString("JOIN '%1' with '%2': %3 rows returned")
                        .arg(leftTableName)
                        .arg(rightTableName)
                        .arg(totalRows));

        } else {
            // 无JOIN，执行简单的单表查询
            // 设置列名 - 暂时返回所有列
            for (const auto& col : leftTable->columns) {
                result.columnNames.append(col.name);
            }

            // 尝试使用索引优化查询
            bool useIndex = false;
            RowId indexRowId = INVALID_ROW_ID;
            bool useFullTextIndex = false;
            QVector<RowId> fullTextResults;

            // 检查WHERE子句是否是MATCH...AGAINST表达式
            if (actualStmt->where) {
                const ast::MatchExpression* matchExpr = dynamic_cast<const ast::MatchExpression*>(actualStmt->where.get());

                if (matchExpr) {
                    // MATCH...AGAINST 全文搜索
                    LOG_INFO(QString("Detected MATCH expression for columns: %1").arg(matchExpr->columns.join(", ")));

                    // 检查列是否有全文索引
                    if (!matchExpr->columns.isEmpty()) {
                        QString columnName = matchExpr->columns[0];  // 当前只支持单列
                        QVector<IndexDef> tableIndexes = catalog->getTableIndexes(leftTableName);

                        for (const auto& indexDef : tableIndexes) {
                            if (indexDef.indexType == qindb::IndexType::INVERTED &&
                                indexDef.columns.size() == 1 &&
                                indexDef.columns[0].compare(columnName, Qt::CaseInsensitive) == 0) {

                                // 找到全文索引，执行搜索
                                LOG_INFO(QString("Using FULLTEXT index '%1' for search").arg(indexDef.name));

                                InvertedIndex invertedIndex(indexDef.name, bufferPool);
                                invertedIndex.setRootPageId(indexDef.rootPageId);

                                // 根据模式选择搜索方法
                                QVector<SearchResult> searchResults;
                                if (matchExpr->mode == ast::MatchMode::BOOLEAN) {
                                    // AND 模式：所有词都要匹配
                                    QStringList queryTerms = matchExpr->query.split(' ', Qt::SkipEmptyParts);
                                    searchResults = invertedIndex.searchAnd(queryTerms, actualStmt->limit);
                                } else {
                                    // 自然语言模式（OR 模式）
                                    searchResults = invertedIndex.search(matchExpr->query, actualStmt->limit);
                                }

                                // 提取 RowId 列表
                                for (const SearchResult& sr : searchResults) {
                                    fullTextResults.append(sr.docId);
                                }

                                useFullTextIndex = true;
                                LOG_INFO(QString("Full-text search found %1 matching documents").arg(fullTextResults.size()));
                                break;
                            }
                        }

                        if (!useFullTextIndex) {
                            LOG_WARN(QString("No FULLTEXT index found for column '%1', query will be slow").arg(columnName));
                        }
                    }
                }
            }

            // 检查WHERE子句是否是简单的等值条件（例如：id = 1）
            if (!useFullTextIndex && actualStmt->where) {
                // 简化实现：只检测 "column = constant" 形式的WHERE子句
                const BinaryExpression* binExpr = dynamic_cast<const BinaryExpression*>(actualStmt->where.get());
                if (binExpr && binExpr->op == BinaryOp::EQ) {
                    const ColumnExpression* colExpr = dynamic_cast<const ColumnExpression*>(binExpr->left.get());
                    const LiteralExpression* litExpr = dynamic_cast<const LiteralExpression*>(binExpr->right.get());

                    if (colExpr && litExpr) {
                        // 检查该列是否有索引
                        QString columnName = colExpr->column;
                        QVector<IndexDef> tableIndexes = catalog->getTableIndexes(leftTableName);

                        for (const auto& indexDef : tableIndexes) {
                            if (indexDef.columns.size() == 1 &&
                                indexDef.columns[0].compare(columnName, Qt::CaseInsensitive) == 0) {

                                // 检查该列是否有索引
                                int columnIndex = leftTable->getColumnIndex(columnName);
                                if (columnIndex >= 0) {
                                    // 评估字面值
                                    QVariant keyValue = evaluator.evaluate(litExpr);
                                    if (!keyValue.isNull()) {
                                        // 使用通用 B+ 树索引查找（支持所有数据类型）
                                        GenericBPlusTree genericBTree(bufferPool, indexDef.keyType, indexDef.rootPageId);
                                        if (genericBTree.search(keyValue, indexRowId)) {
                                            useIndex = true;
                                            LOG_INFO(QString("Using index '%1' for query (rowId=%2)")
                                                        .arg(indexDef.name).arg(indexRowId));
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            int totalRows = 0;

            if (useIndex && indexRowId != INVALID_ROW_ID) {
                // 索引查找成功，但需要通过rowId定位到实际记录
                // 当前限制：缺少rowId到(pageId, slotIndex)的映射表
                //
                // 实现选项：
                // 1. 在TableDef中维护rowId -> (pageId, slotIndex)映射（哈希表）
                // 2. 在PageHeader中记录该页的rowId范围，二分查找定位页面
                // 3. 使用聚簇索引（主键索引直接存储完整记录）
                //
                // 当前策略：回退到全表扫描，但记录索引被成功使用的日志
                LOG_INFO(QString("Index '%1' matched rowId %2, but direct rowId lookup not implemented - using full table scan")
                            .arg("index").arg(indexRowId));
                useIndex = false;
            }

            if (!useIndex) {
                // 全表扫描（支持MVCC可见性检查）
                // 获取当前事务ID
                TransactionManager* txnManager = dbManager_->getCurrentTransactionManager();
                TransactionId currentTxnId = dbManager_->getCurrentTransactionId();

                if (currentTxnId == INVALID_TXN_ID) {
                    // 如果没有活跃事务，使用一个虚拟的事务ID（0）来读取已提交的数据
                    currentTxnId = 0;
                }

                // 创建可见性检查器
                VisibilityChecker* checker = nullptr;
                if (txnManager) {
                    checker = new VisibilityChecker(txnManager);
                }

                // 扫描所有数据页，读取记录
                PageId currentPageId = leftTable->firstPageId;

                while (currentPageId != INVALID_PAGE_ID) {
                Page* page = bufferPool->fetchPage(currentPageId);
                if (!page) {
                    LOG_ERROR(QString("Failed to fetch page %1").arg(currentPageId));
                    break;
                }

                // 获取该页的所有记录（包含RecordHeader用于MVCC检查）
                QVector<QVector<QVariant>> pageRecords;
                QVector<RecordHeader> pageHeaders;
                QVector<RowId> rowIds;

                // 先获取headers
                if (TablePage::getAllRecords(page, leftTable, pageRecords, pageHeaders)) {
                    // 如果需要RowId（用于全文搜索过滤），再调用一次
                    if (useFullTextIndex) {
                        QVector<QVector<QVariant>> tempRecords;
                        TablePage::getAllRecords(page, leftTable, tempRecords, &rowIds);
                    }

                    // 应用 MVCC 可见性检查和 WHERE 过滤条件
                    for (int i = 0; i < pageRecords.size(); ++i) {
                        const auto& record = pageRecords[i];
                        const auto& recordHeader = pageHeaders[i];
                        RowId rowId = (useFullTextIndex && i < rowIds.size()) ? rowIds[i] : INVALID_ROW_ID;

                        bool includeRow = true;

                        // 如果使用了全文索引，检查RowId是否在结果中
                        if (useFullTextIndex && rowId != INVALID_ROW_ID) {
                            if (!fullTextResults.contains(rowId)) {
                                continue;  // 跳过不在全文搜索结果中的记录
                            }
                        }

                        // MVCC可见性检查
                        if (checker && !checker->isVisible(recordHeader, currentTxnId)) {
                            continue;  // 跳过对当前事务不可见的记录
                        }

                        // 如果有WHERE子句��不是MATCH表达式，评估条件
                        if (actualStmt->where && !useFullTextIndex) {
                            QVariant whereResult = evaluator.evaluateWithRow(actualStmt->where.get(), leftTable, record);

                            if (evaluator.hasError()) {
                                bufferPool->unpinPage(currentPageId, false);
                                delete checker;
                                return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                                        QString("WHERE clause evaluation error: %1")
                                                            .arg(evaluator.getLastError()));
                            }

                            // SQL三值逻辑：只有明确为true才包含行
                            includeRow = !whereResult.isNull() && whereResult.toBool();
                        }

                        if (includeRow) {
                            // TODO: 应用列投影（SELECT 指定列）
                            result.rows.append(record);
                            totalRows++;
                        }
                    }
                }

                // 移动到下一页
                PageHeader* header = page->getHeader();
                PageId nextPageId = header->nextPageId;
                bufferPool->unpinPage(currentPageId, false);
                currentPageId = nextPageId;
                }

                // 清理可见性检查器
                delete checker;
            }

            result.message = QString("SELECT executed (%1 rows)").arg(totalRows);

            LOG_INFO(QString("SELECT FROM '%1': %2 rows returned")
                        .arg(leftTableName)
                        .arg(result.rows.size()));
        }

        // 应用 GROUP BY 和聚合函数
        if (actualStmt->groupBy) {
            LOG_INFO("Executing GROUP BY");

            // 使用分组键（group key）构建分组表
            QMap<QString, QVector<QVector<QVariant>>> groups;

            for (const auto& row : result.rows) {
                // 计算分组键
                QString groupKey;
                for (const auto& groupExpr : actualStmt->groupBy->expressions) {
                    QVariant keyValue = evaluator.evaluateWithRow(groupExpr.get(), leftTable, row);
                    if (evaluator.hasError()) {
                        return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                                QString("GROUP BY evaluation error: %1")
                                                    .arg(evaluator.getLastError()));
                    }
                    groupKey += keyValue.toString() + "|";
                }

                groups[groupKey].append(row);
            }

            // 处理每个分组，计算聚合函数
            result.rows.clear();
            result.columnNames.clear();

            // 设置列名（暂时简化为"groupKey"和"count"）
            result.columnNames.append("group_key");
            result.columnNames.append("count");

            for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
                const QString& groupKey = it.key();
                const QVector<QVector<QVariant>>& groupRows = it.value();

                // 计算聚合值
                QVector<QVariant> aggregatedRow;
                aggregatedRow.append(groupKey);
                aggregatedRow.append(groupRows.size()); // COUNT(*)

                // 应用 HAVING 过滤
                bool includeGroup = true;
                if (actualStmt->groupBy->having) {
                    // 简化实现：暂时只支持COUNT(*)的HAVING条件
                    // TODO: 完整实现需要在HAVING中支持所有聚合函数
                    LOG_WARN("HAVING clause not fully implemented yet");
                }

                if (includeGroup) {
                    result.rows.append(aggregatedRow);
                }
            }

            result.message = QString("SELECT executed with GROUP BY (%1 groups)")
                                .arg(result.rows.size());

            LOG_INFO(QString("GROUP BY returned %1 groups").arg(result.rows.size()));
        }

        // 应用 ORDER BY 排序
        if (!actualStmt->orderBy.empty()) {
            // 对结果进行排序
            std::sort(result.rows.begin(), result.rows.end(),
                     [&](const QVector<QVariant>& row1, const QVector<QVariant>& row2) -> bool {
                // 按照ORDER BY子句中的每个列依次比较
                for (const auto& orderItem : actualStmt->orderBy) {
                    // 评估ORDER BY表达式（可能是列名或表达式）
                    QVariant val1 = evaluator.evaluateWithRow(orderItem.expression.get(), leftTable, row1);
                    QVariant val2 = evaluator.evaluateWithRow(orderItem.expression.get(), leftTable, row2);

                    if (evaluator.hasError()) {
                        // 如果求值失败，跳过这个排序项
                        continue;
                    }

                    // NULL值处理：NULL视为最小值
                    if (val1.isNull() && !val2.isNull()) {
                        return orderItem.ascending; // NULL在升序时排前面
                    }
                    if (!val1.isNull() && val2.isNull()) {
                        return !orderItem.ascending; // NULL在降序时排后面
                    }
                    if (val1.isNull() && val2.isNull()) {
                        continue; // 都是NULL，比较下一列
                    }

                    // 数值比较
                    if (val1.canConvert<double>() && val2.canConvert<double>()) {
                        double d1 = val1.toDouble();
                        double d2 = val2.toDouble();
                        if (d1 != d2) {
                            return orderItem.ascending ? (d1 < d2) : (d1 > d2);
                        }
                    }
                    // 字符串比较
                    else {
                        QString s1 = val1.toString();
                        QString s2 = val2.toString();
                        int cmp = s1.compare(s2);
                        if (cmp != 0) {
                            return orderItem.ascending ? (cmp < 0) : (cmp > 0);
                        }
                    }

                    // 相等，继续比较下一列
                }

                // 所有列都相等
                return false;
            });
        }

        // 应用 LIMIT 限制
        if (actualStmt->limit > 0 && result.rows.size() > actualStmt->limit) {
            result.rows.resize(actualStmt->limit);
            result.message += QString(" (limited to %1 rows)").arg(actualStmt->limit);
        }

    } else {
        result.message = "SELECT without FROM clause";
    }

    // 存储到查询缓存（仅针对成功的查询且未使用缓存时）
    if (!usedCache && queryCache_ && queryCache_->isEnabled() && actualStmt->from && result.success) {
        QSet<QString> affectedTables;
        affectedTables.insert(fromTable);

        // 添加JOIN表
        for (const auto& joinPtr : actualStmt->joins) {
            if (joinPtr && joinPtr->right) {
                affectedTables.insert(joinPtr->right->tableName);
            }
        }

        // 存储到缓存
        if (queryCache_->put(querySql, result, affectedTables)) {
            LOG_DEBUG(QString("Query cached: %1").arg(querySql.left(60)));
        }
    }

    return result;
}

QueryResult Executor::executeUpdate(const UpdateStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid UPDATE statement");
    }

    LOG_INFO(QString("Executing UPDATE: %1").arg(stmt->tableName));

    // 获取当前数据库的组件
    Catalog* catalog = dbManager_->getCurrentCatalog();
    BufferPoolManager* bufferPool = dbManager_->getCurrentBufferPool();
    WALManager* walManager = dbManager_->getCurrentWALManager();
    TransactionManager* txnManager = dbManager_->getCurrentTransactionManager();

    if (!catalog || !bufferPool || !walManager || !txnManager) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 检查表是否存在
    const TableDef* table = catalog->getTable(stmt->tableName);
    if (!table) {
        return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                QString("Table '%1' does not exist").arg(stmt->tableName));
    }

    // 检查是否有会话事务
    TransactionId sessionTxnId = dbManager_->getCurrentTransactionId();
    bool autoCommit = (sessionTxnId == INVALID_TXN_ID);

    TransactionId txnId;
    if (autoCommit) {
        // 自动提交模式：创建新事务
        txnId = txnManager->beginTransaction();
        LOG_INFO(QString("Transaction %1 started for UPDATE (auto-commit)").arg(txnId));
    } else {
        // 使用会话事务
        txnId = sessionTxnId;
        LOG_INFO(QString("Using session transaction %1 for UPDATE").arg(txnId));
    }

    // 创建表达式求值器
    ExpressionEvaluator evaluator(catalog);

    // 第一步：收集所有需要更新的行（行数据和位置信息）
    struct UpdateCandidate {
        PageId pageId;
        int slotIndex;
        QVector<QVariant> oldRow;
        QVector<QVariant> newRow;
    };

    QVector<UpdateCandidate> candidates;

    // 获取当前事务ID用于MVCC可见性检查
    TransactionId currentTxnId = dbManager_->getCurrentTransactionId();
    if (currentTxnId == INVALID_TXN_ID) {
        currentTxnId = 0;  // 没有活跃事务时，使用虚拟ID读取已提交数据
    }

    // 创建可见性检查器
    VisibilityChecker* checker = nullptr;
    if (txnManager) {
        checker = new VisibilityChecker(txnManager);
    }

    // 扫描所有页，找到符合WHERE条件的行
    PageId currentPageId = table->firstPageId;

    while (currentPageId != INVALID_PAGE_ID) {
        Page* page = bufferPool->fetchPage(currentPageId);
        if (!page) {
            LOG_ERROR(QString("Failed to fetch page %1").arg(currentPageId));
            break;
        }

        // 获取该页的所有记录（包含RecordHeader用于MVCC检查）
        QVector<QVector<QVariant>> pageRecords;
        QVector<RecordHeader> pageHeaders;
        if (TablePage::getAllRecords(page, table, pageRecords, pageHeaders)) {
            for (int i = 0; i < pageRecords.size(); ++i) {
                const auto& record = pageRecords[i];
                const auto& recordHeader = pageHeaders[i];
                bool shouldUpdate = true;

                // MVCC可见性检查
                if (checker && !checker->isVisible(recordHeader, currentTxnId)) {
                    continue;  // 跳过对当前事务不可见的记录
                }

                // 如果有WHERE子句，评估条件
                if (stmt->where) {
                    QVariant whereResult = evaluator.evaluateWithRow(stmt->where.get(), table, record);

                    if (evaluator.hasError()) {
                        bufferPool->unpinPage(currentPageId, false);
                        delete checker;
                        return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                                QString("WHERE clause evaluation error: %1")
                                                    .arg(evaluator.getLastError()));
                    }

                    shouldUpdate = !whereResult.isNull() && whereResult.toBool();
                }

                if (shouldUpdate) {
                    // 创建更新后的行（复制原行，然后更新指定列）
                    QVector<QVariant> newRow = record;

                    // 应用SET子句
                    for (const auto& assignment : stmt->assignments) {
                        // 找到列索引
                        int colIndex = -1;
                        for (int i = 0; i < table->columns.size(); ++i) {
                            if (table->columns[i].name.compare(assignment.first, Qt::CaseInsensitive) == 0) {
                                colIndex = i;
                                break;
                            }
                        }

                        if (colIndex < 0) {
                            bufferPool->unpinPage(currentPageId, false);
                            return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                                    QString("Column '%1' not found in table '%2'")
                                                        .arg(assignment.first)
                                                        .arg(stmt->tableName));
                        }

                        // 计算新值（可以引用当前行的列）
                        QVariant newValue = evaluator.evaluateWithRow(assignment.second.get(), table, record);

                        if (evaluator.hasError()) {
                            bufferPool->unpinPage(currentPageId, false);
                            delete checker;
                            return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                                    QString("SET clause evaluation error: %1")
                                                        .arg(evaluator.getLastError()));
                        }

                        newRow[colIndex] = newValue;
                    }

                    // 记录候选更新
                    UpdateCandidate candidate;
                    candidate.pageId = currentPageId;
                    candidate.slotIndex = i;  // 使用循环索引 i 而不是 slotIndex
                    candidate.oldRow = record;
                    candidate.newRow = newRow;
                    candidates.append(candidate);
                }
            }
        }

        // 移动到下一页
        PageHeader* header = page->getHeader();
        PageId nextPageId = header->nextPageId;
        bufferPool->unpinPage(currentPageId, false);
        currentPageId = nextPageId;
    }

    // 清理可见性检查器
    delete checker;

    // 第二步：执行物理更新
    int updatedCount = 0;
    int failedCount = 0;

    for (const auto& candidate : candidates) {
        Page* page = bufferPool->fetchPage(candidate.pageId);
        if (!page) {
            LOG_ERROR(QString("Failed to fetch page %1 for update").arg(candidate.pageId));
            failedCount++;
            continue;
        }

        // 尝试原地更新
        if (TablePage::updateRecord(page, table, candidate.slotIndex, candidate.newRow)) {
            updatedCount++;

            // 如果是会话事务，添加 Undo 记录
            if (!autoCommit) {
                // 写入 WAL 记录
                QByteArray walData;
                QDataStream walStream(&walData, QIODevice::WriteOnly);
                walStream << stmt->tableName << candidate.pageId << candidate.slotIndex;
                WALRecord walRecord(WALRecordType::UPDATE, txnId, walData);
                uint64_t lsn = walManager->writeRecord(walRecord);

                // 添加 Undo 记录（保存旧值）
                UndoRecord undoRecord = UndoRecord::createUpdateUndo(
                    stmt->tableName,
                    candidate.pageId,
                    candidate.slotIndex,
                    candidate.oldRow,
                    lsn
                );
                txnManager->addUndoRecord(txnId, undoRecord);
            }

            bufferPool->unpinPage(candidate.pageId, true);  // 标记为脏页

            // 更新所有索引（使用通用 B+ 树，支持所有数据类型）
            QVector<IndexDef> tableIndexes = catalog->getTableIndexes(stmt->tableName);
            for (const auto& indexDef : tableIndexes) {
                if (indexDef.columns.size() != 1) continue;

                QString columnName = indexDef.columns[0];
                int columnIndex = table->getColumnIndex(columnName);
                if (columnIndex < 0) continue;

                // 获取旧键和新键
                QVariant oldKeyValue = candidate.oldRow[columnIndex];
                QVariant newKeyValue = candidate.newRow[columnIndex];

                if (!oldKeyValue.isNull() && !newKeyValue.isNull()) {
                    // 使用 KeyComparator 比较键值是否改变
                    int cmpResult = KeyComparator::compare(oldKeyValue, newKeyValue, indexDef.keyType);

                    // 如果键值改变，需要更新索引
                    if (cmpResult != 0) {
                        GenericBPlusTree genericBTree(bufferPool, indexDef.keyType, indexDef.rootPageId);

                        // 删除旧键
                        if (!genericBTree.remove(oldKeyValue)) {
                            LOG_WARN(QString("Failed to remove old key from index '%1'")
                                        .arg(indexDef.name));
                        }

                        // 插入新键（使用相同的rowId）
                        // TODO: 需要获取实际的rowId
                        static RowId tempRowId = 1;
                        if (!genericBTree.insert(newKeyValue, tempRowId++)) {
                            LOG_WARN(QString("Failed to insert new key into index '%1'")
                                        .arg(indexDef.name));
                        } else {
                            LOG_DEBUG(QString("Updated index '%1' with new key")
                                         .arg(indexDef.name));
                        }
                    }
                }
            }
        } else {
            // 原地更新失败（通常是新记录更大），使用删除+插入策略
            LOG_DEBUG(QString("In-place update failed for slot %1, trying delete+insert")
                         .arg(candidate.slotIndex));

            // 删除旧记录（逻辑删除，传入事务ID）
            if (TablePage::deleteRecord(page, candidate.slotIndex, txnId)) {
                bufferPool->unpinPage(candidate.pageId, true);

                // 尝试插入新记录
                // 生成新的行ID（保留旧的行ID会更好，但简化实现）
                static RowId nextRowId = 1000000;  // 使用不同的起始值避免冲突
                RowId newRowId = nextRowId++;

                uint16_t recordSize = TablePage::calculateRecordSize(table, candidate.newRow);
                bool inserted = false;

                // 尝试在现有页中插入
                PageId insertPageId = table->firstPageId;
                while (insertPageId != INVALID_PAGE_ID && !inserted) {
                    Page* insertPage = bufferPool->fetchPage(insertPageId);
                    if (!insertPage) break;

                    if (TablePage::hasEnoughSpace(insertPage, recordSize)) {
                        if (TablePage::insertRecord(insertPage, table, newRowId, candidate.newRow, txnId)) {
                            inserted = true;
                            updatedCount++;
                            bufferPool->unpinPage(insertPageId, true);
                            break;
                        }
                    }

                    PageHeader* h = insertPage->getHeader();
                    PageId nextId = h->nextPageId;
                    bufferPool->unpinPage(insertPageId, false);
                    insertPageId = nextId;
                }

                if (!inserted) {
                    LOG_ERROR("Failed to insert updated record (no space available)");
                    failedCount++;
                }
            } else {
                bufferPool->unpinPage(candidate.pageId, false);
                failedCount++;
            }
        }
    }

    // 提交事务（仅在自动提交模式下）
    if (autoCommit) {
        if (!txnManager->commitTransaction(txnId)) {
            txnManager->abortTransaction(txnId);
            return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                    "Failed to commit transaction");
        }
        LOG_INFO(QString("Transaction %1 committed (auto-commit)").arg(txnId));
    }

    // 保存 Catalog 到磁盘（确保元数据持久化）
    QString dbPath = dbManager_->getDatabasePath(dbManager_->currentDatabaseName());
    QString catalogPath = dbPath + "/catalog.json";
    if (!catalog->save(catalogPath)) {
        LOG_ERROR("Failed to save catalog after UPDATE");
    }

    // 刷新所有脏页
    bufferPool->flushAllPages();

    // 使缓存失效
    if (queryCache_) {
        int invalidated = queryCache_->invalidateTable(stmt->tableName);
        if (invalidated > 0) {
            LOG_DEBUG(QString("Invalidated %1 cache entries for table '%2'")
                         .arg(invalidated).arg(stmt->tableName));
        }
    }

    LOG_INFO(QString("UPDATE '%1': %2 row(s) updated, %3 failed")
                .arg(stmt->tableName)
                .arg(updatedCount)
                .arg(failedCount));

    if (failedCount > 0) {
        return createSuccessResult(QString("Updated %1 row(s) in '%2' (%3 failed)")
                                       .arg(updatedCount)
                                       .arg(stmt->tableName)
                                       .arg(failedCount));
    }

    return createSuccessResult(QString("Updated %1 row(s) in '%2'")
                                   .arg(updatedCount)
                                   .arg(stmt->tableName));
}

QueryResult Executor::executeDelete(const DeleteStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid DELETE statement");
    }

    LOG_INFO(QString("Executing DELETE FROM: %1").arg(stmt->tableName));

    // 获取当前数据库的组件
    Catalog* catalog = dbManager_->getCurrentCatalog();
    BufferPoolManager* bufferPool = dbManager_->getCurrentBufferPool();
    WALManager* walManager = dbManager_->getCurrentWALManager();
    TransactionManager* txnManager = dbManager_->getCurrentTransactionManager();

    if (!catalog || !bufferPool || !walManager || !txnManager) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 检查表是否存在
    const TableDef* table = catalog->getTable(stmt->tableName);
    if (!table) {
        return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                QString("Table '%1' does not exist").arg(stmt->tableName));
    }

    // 检查是否有会话事务
    TransactionId sessionTxnId = dbManager_->getCurrentTransactionId();
    bool autoCommit = (sessionTxnId == INVALID_TXN_ID);

    TransactionId txnId;
    if (autoCommit) {
        // 自动提交模式：创建新事务
        txnId = txnManager->beginTransaction();
        LOG_INFO(QString("Transaction %1 started for DELETE (auto-commit)").arg(txnId));
    } else {
        // 使用会话事务
        txnId = sessionTxnId;
        LOG_INFO(QString("Using session transaction %1 for DELETE").arg(txnId));
    }

    // 创建表达式求值器
    ExpressionEvaluator evaluator(catalog);

    // 第一步：收集所有需要删除的记录位置
    struct DeleteCandidate {
        PageId pageId;
        int slotIndex;
        QVector<QVariant> record;  // 保存记录数据用于索引维护
    };

    QVector<DeleteCandidate> candidates;

    // 获取当前事务ID用于MVCC可见性检查
    TransactionId currentTxnId = dbManager_->getCurrentTransactionId();
    if (currentTxnId == INVALID_TXN_ID) {
        currentTxnId = 0;  // 没有活跃事务时，使用虚拟ID读取已提交数据
    }

    // 创建可见性检查器
    VisibilityChecker* checker = nullptr;
    if (txnManager) {
        checker = new VisibilityChecker(txnManager);
    }

    // 扫描所有页，找到符合WHERE条件的行
    PageId currentPageId = table->firstPageId;

    while (currentPageId != INVALID_PAGE_ID) {
        Page* page = bufferPool->fetchPage(currentPageId);
        if (!page) {
            LOG_ERROR(QString("Failed to fetch page %1").arg(currentPageId));
            break;
        }

        // 获取该页的所有记录（包含RecordHeader用于MVCC检查）
        QVector<QVector<QVariant>> pageRecords;
        QVector<RecordHeader> pageHeaders;
        if (TablePage::getAllRecords(page, table, pageRecords, pageHeaders)) {
            for (int i = 0; i < pageRecords.size(); ++i) {
                const auto& record = pageRecords[i];
                const auto& recordHeader = pageHeaders[i];
                bool shouldDelete = true;

                // MVCC可见性检查
                if (checker && !checker->isVisible(recordHeader, currentTxnId)) {
                    continue;  // 跳过对当前事务不可见的记录
                }

                // 如果有WHERE子句，评估条件
                if (stmt->where) {
                    QVariant whereResult = evaluator.evaluateWithRow(stmt->where.get(), table, record);

                    if (evaluator.hasError()) {
                        bufferPool->unpinPage(currentPageId, false);
                        delete checker;
                        return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                                QString("WHERE clause evaluation error: %1")
                                                    .arg(evaluator.getLastError()));
                    }

                    shouldDelete = !whereResult.isNull() && whereResult.toBool();
                }

                if (shouldDelete) {
                    // 记录候选删除（保存记录数据）
                    DeleteCandidate candidate;
                    candidate.pageId = currentPageId;
                    candidate.slotIndex = i;  // 使用循环索引 i 而不是 slotIndex
                    candidate.record = record;
                    candidates.append(candidate);
                }
            }
        }

        // 移动到下一页
        PageHeader* header = page->getHeader();
        PageId nextPageId = header->nextPageId;
        bufferPool->unpinPage(currentPageId, false);
        currentPageId = nextPageId;
    }

    // 清理可见性检查器
    delete checker;

    // 第二步：执行物理删除（逻辑删除：设置deleteTxnId）
    int deletedCount = 0;
    int failedCount = 0;

    for (const auto& candidate : candidates) {
        Page* page = bufferPool->fetchPage(candidate.pageId);
        if (!page) {
            LOG_ERROR(QString("Failed to fetch page %1 for deletion").arg(candidate.pageId));
            failedCount++;
            continue;
        }

        // 执行逻辑删除（传入事务ID）
        if (TablePage::deleteRecord(page, candidate.slotIndex, txnId)) {
            deletedCount++;

            // 如果是会话事务，添加 Undo 记录
            if (!autoCommit) {
                // 写入 WAL 记录
                QByteArray walData;
                QDataStream walStream(&walData, QIODevice::WriteOnly);
                walStream << stmt->tableName << candidate.pageId << candidate.slotIndex;
                WALRecord walRecord(WALRecordType::DELETE, txnId, walData);
                uint64_t lsn = walManager->writeRecord(walRecord);

                // 添加 Undo 记录（保存整行数据以便恢复）
                UndoRecord undoRecord = UndoRecord::createDeleteUndo(
                    stmt->tableName,
                    candidate.pageId,
                    candidate.slotIndex,
                    candidate.record,
                    lsn
                );
                txnManager->addUndoRecord(txnId, undoRecord);
            }

            bufferPool->unpinPage(candidate.pageId, true);  // 标记为脏页

            // 从所有索引中删除该记录
            QVector<IndexDef> tableIndexes = catalog->getTableIndexes(stmt->tableName);
            for (const auto& indexDef : tableIndexes) {
                if (indexDef.columns.size() != 1) continue;

                QString columnName = indexDef.columns[0];
                int columnIndex = table->getColumnIndex(columnName);
                if (columnIndex < 0) continue;

                // 获取被删除记录的索引键值
                QVariant keyValue = candidate.record[columnIndex];
                if (!keyValue.isNull()) {
                    // 使用通用 B+ 树删除（支持所有数据类型）
                    GenericBPlusTree genericBTree(bufferPool, indexDef.keyType, indexDef.rootPageId);
                    if (!genericBTree.remove(keyValue)) {
                        LOG_WARN(QString("Failed to remove key from index '%1'")
                                    .arg(indexDef.name));
                    } else {
                        LOG_DEBUG(QString("Removed key from index '%1'")
                                     .arg(indexDef.name));
                    }
                }
            }
        } else {
            LOG_ERROR(QString("Failed to delete record at page %1, slot %2")
                         .arg(candidate.pageId)
                         .arg(candidate.slotIndex));
            failedCount++;
            bufferPool->unpinPage(candidate.pageId, false);
        }
    }

    // 提交事务（仅在自动提交模式下）
    if (autoCommit) {
        if (!txnManager->commitTransaction(txnId)) {
            txnManager->abortTransaction(txnId);
            return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                    "Failed to commit transaction");
        }
        LOG_INFO(QString("Transaction %1 committed (auto-commit)").arg(txnId));
    }

    // 保存 Catalog 到磁盘（确保元数据持久化）
    QString dbPath = dbManager_->getDatabasePath(dbManager_->currentDatabaseName());
    QString catalogPath = dbPath + "/catalog.json";
    if (!catalog->save(catalogPath)) {
        LOG_ERROR("Failed to save catalog after DELETE");
    }

    // 刷新所有脏页到磁盘
    bufferPool->flushAllPages();

    // 使缓存失效
    if (queryCache_) {
        int invalidated = queryCache_->invalidateTable(stmt->tableName);
        if (invalidated > 0) {
            LOG_DEBUG(QString("Invalidated %1 cache entries for table '%2'")
                         .arg(invalidated).arg(stmt->tableName));
        }
    }

    LOG_INFO(QString("DELETE FROM '%1': %2 row(s) deleted, %3 failed")
                .arg(stmt->tableName)
                .arg(deletedCount)
                .arg(failedCount));

    if (failedCount > 0) {
        return createSuccessResult(QString("Deleted %1 row(s) from '%2' (%3 failed)")
                                       .arg(deletedCount)
                                       .arg(stmt->tableName)
                                       .arg(failedCount));
    }

    return createSuccessResult(QString("Deleted %1 row(s) from '%2'")
                                   .arg(deletedCount)
                                   .arg(stmt->tableName));
}

QueryResult Executor::executeShowTables() {
    LOG_INFO("Executing SHOW TABLES");

    // 获取当前数据库的组件
    Catalog* catalog = dbManager_->getCurrentCatalog();
    if (!catalog) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    QueryResult result;
    result.success = true;
    result.columnNames.append("Table");

    QVector<QString> tableNames = catalog->getAllTableNames();

    for (const auto& tableName : tableNames) {
        QVector<QVariant> row;
        row.append(tableName);
        result.rows.append(row);
    }

    result.message = QString("Found %1 table(s)").arg(tableNames.size());

    return result;
}

DataType Executor::convertDataType(const QString& typeStr) {
    QString lower = typeStr.toLower().trimmed();

    // 整数类型
    if (lower == "tinyint") return DataType::TINYINT;
    if (lower == "smallint") return DataType::SMALLINT;
    if (lower == "mediumint") return DataType::MEDIUMINT;
    if (lower == "int" || lower == "integer") return DataType::INT;
    if (lower == "bigint") return DataType::BIGINT;
    if (lower == "serial") return DataType::SERIAL;
    if (lower == "bigserial") return DataType::BIGSERIAL;

    // 浮点类型
    if (lower == "float") return DataType::FLOAT;
    if (lower == "real") return DataType::REAL;
    if (lower == "double" || lower == "double precision") return DataType::DOUBLE;
    if (lower == "binary_float") return DataType::BINARY_FLOAT;
    if (lower == "binary_double") return DataType::BINARY_DOUBLE;

    // 定点数类型
    if (lower == "decimal" || lower == "dec" || lower == "numeric") return DataType::DECIMAL;

    // 字符串类型
    if (lower == "char" || lower == "character") return DataType::CHAR;
    if (lower == "varchar" || lower == "varchar2") return DataType::VARCHAR;
    if (lower == "nchar") return DataType::NCHAR;
    if (lower == "nvarchar") return DataType::NVARCHAR;
    if (lower == "text") return DataType::TEXT;
    if (lower == "tinytext") return DataType::TINYTEXT;
    if (lower == "mediumtext") return DataType::MEDIUMTEXT;
    if (lower == "longtext") return DataType::LONGTEXT;
    if (lower == "ntext") return DataType::NTEXT;
    if (lower == "clob") return DataType::CLOB;
    if (lower == "nclob") return DataType::NCLOB;

    // 二进制类型
    if (lower == "binary") return DataType::BINARY;
    if (lower == "varbinary") return DataType::VARBINARY;
    if (lower == "bytea") return DataType::BYTEA;
    if (lower == "blob") return DataType::BLOB;
    if (lower == "tinyblob") return DataType::TINYBLOB;
    if (lower == "mediumblob") return DataType::MEDIUMBLOB;
    if (lower == "longblob") return DataType::LONGBLOB;
    if (lower == "image") return DataType::IMAGE;

    // 日期时间类型
    if (lower == "date") return DataType::DATE;
    if (lower == "time") return DataType::TIME;
    if (lower == "datetime") return DataType::DATETIME;
    if (lower == "datetime2") return DataType::DATETIME2;
    if (lower == "smalldatetime") return DataType::SMALLDATETIME;
    if (lower == "timestamp") return DataType::TIMESTAMP;
    if (lower == "timestamp with time zone" || lower == "timestamptz") return DataType::TIMESTAMP_TZ;
    if (lower == "datetimeoffset") return DataType::DATETIMEOFFSET;

    // 布尔类型
    if (lower == "boolean" || lower == "bool") return DataType::BOOLEAN;

    // JSON 类型
    if (lower == "json") return DataType::JSON;
    if (lower == "jsonb") return DataType::JSONB;

    // XML 类型
    if (lower == "xml") return DataType::XML;

    // 特殊类型
    if (lower == "uuid") return DataType::UUID;
    if (lower == "uniqueidentifier") return DataType::UNIQUEIDENTIFIER;
    if (lower == "rowid") return DataType::ROWID;
    if (lower == "geometry") return DataType::GEOMETRY;
    if (lower == "geography") return DataType::GEOGRAPHY;
    if (lower == "hierarchyid") return DataType::HIERARCHYID;

    // 未知类型
    LOG_WARN(QString("Unknown data type: %1, defaulting to NULL_TYPE").arg(typeStr));
    return DataType::NULL_TYPE;
}

QueryResult Executor::createErrorResult(ErrorCode code, const QString& message) {
    QueryResult result;
    result.success = false;
    result.error = Error(code, message);
    result.message = message;

    LOG_ERROR(message);

    return result;
}

QueryResult Executor::createSuccessResult(const QString& message) {
    QueryResult result;
    result.success = true;
    result.message = message;

    return result;
}

QueryResult Executor::executeCreateDatabase(const CreateDatabaseStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid CREATE DATABASE statement");
    }

    if (authManager_ && !currentUser_.isEmpty() && !authManager_->isUserAdmin(currentUser_)) {
        return createErrorResult(ErrorCode::PERMISSION_DENIED,
                                QString("User '%1' cannot create databases").arg(currentUser_));
    }

    LOG_INFO(QString("Executing CREATE DATABASE: %1").arg(stmt->databaseName));

    if (!dbManager_->createDatabase(stmt->databaseName, stmt->ifNotExists)) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                dbManager_->lastError().message);
    }

    return createSuccessResult(QString("Database '%1' created").arg(stmt->databaseName));
}

QueryResult Executor::executeDropDatabase(const DropDatabaseStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid DROP DATABASE statement");
    }

    if (authManager_ && !currentUser_.isEmpty() && !authManager_->isUserAdmin(currentUser_)) {
        return createErrorResult(ErrorCode::PERMISSION_DENIED,
                                QString("User '%1' cannot drop databases").arg(currentUser_));
    }

    LOG_INFO(QString("Executing DROP DATABASE: %1").arg(stmt->databaseName));

    if (!dbManager_->dropDatabase(stmt->databaseName, stmt->ifExists)) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                dbManager_->lastError().message);
    }

    return createSuccessResult(QString("Database '%1' dropped").arg(stmt->databaseName));
}

QueryResult Executor::executeUseDatabase(const UseDatabaseStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid USE DATABASE statement");
    }

    LOG_INFO(QString("Executing USE DATABASE: %1").arg(stmt->databaseName));

    if (!dbManager_->useDatabase(stmt->databaseName)) {
        return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                dbManager_->lastError().message);
    }

    permissionManager_ = dbManager_->getCurrentPermissionManager();

    return createSuccessResult(QString("Switched to database '%1'").arg(stmt->databaseName));
}

QueryResult Executor::executeShowDatabases(const ShowDatabasesStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid SHOW DATABASES statement");
    }

    LOG_INFO("Executing SHOW DATABASES");

    QueryResult result;
    result.success = true;
    result.columnNames.append("Database");

    QVector<QString> dbNames = dbManager_->getAllDatabaseNames();

    for (const auto& dbName : dbNames) {
        QVector<QVariant> row;
        row.append(dbName);
        result.rows.append(row);
    }

    result.message = QString("Found %1 database(s)").arg(dbNames.size());

    return result;
}

QueryResult Executor::executeSave() {
    LOG_INFO("Executing SAVE");

    // 获取当前数据库的组件
    Catalog* catalog = dbManager_->getCurrentCatalog();
    BufferPoolManager* bufferPool = dbManager_->getCurrentBufferPool();

    if (!catalog || !bufferPool) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 保存catalog到当前数据库目录
    QString dbPath = dbManager_->getDatabasePath(dbManager_->currentDatabaseName());
    QString catalogPath = dbPath + "/catalog.json";

    if (!catalog->save(catalogPath)) {
        return createErrorResult(ErrorCode::IO_ERROR, "Failed to save catalog to disk");
    }

    // 刷新所有脏页到磁盘
    bufferPool->flushAllPages();

    LOG_INFO(QString("Database '%1' saved successfully").arg(dbManager_->currentDatabaseName()));

    return createSuccessResult(QString("Database '%1' saved to disk").arg(dbManager_->currentDatabaseName()));
}

QueryResult Executor::executeCreateIndex(const CreateIndexStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid CREATE INDEX statement");
    }

    LOG_INFO(QString("Executing CREATE INDEX: %1 ON %2")
                .arg(stmt->indexName)
                .arg(stmt->tableName));

    // 获取当前数据库的组件
    Catalog* catalog = dbManager_->getCurrentCatalog();
    BufferPoolManager* bufferPool = dbManager_->getCurrentBufferPool();

    if (!catalog || !bufferPool) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 检查表是否存在
    const TableDef* table = catalog->getTable(stmt->tableName);
    if (!table) {
        return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                QString("Table '%1' does not exist").arg(stmt->tableName));
    }

    // 检查索引是否已存在
    if (catalog->getIndex(stmt->indexName)) {
        if (stmt->ifNotExists) {
            return createSuccessResult(QString("Index '%1' already exists (skipped)").arg(stmt->indexName));
        }
        return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                QString("Index '%1' already exists").arg(stmt->indexName));
    }

    // 当前只支持单列索引
    if (stmt->columns.size() != 1) {
        return createErrorResult(ErrorCode::NOT_IMPLEMENTED,
                                "Composite indexes not yet supported. Only single-column indexes are supported.");
    }

    QString columnName = stmt->columns[0];
    int columnIndex = table->getColumnIndex(columnName);
    if (columnIndex < 0) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                QString("Column '%1' not found in table '%2'")
                                    .arg(columnName)
                                    .arg(stmt->tableName));
    }

    const ColumnDef& column = table->columns[columnIndex];

    // 检查列类型是否支持索引
    if (!KeyComparator::isIndexableType(column.type)) {
        return createErrorResult(ErrorCode::NOT_IMPLEMENTED,
                                QString("Index on column type '%1' not supported (GEOMETRY/GEOGRAPHY require R-tree)")
                                    .arg(getDataTypeName(column.type)));
    }

    // 根据索引类型创建相应的索引结构
    PageId rootPageId = INVALID_PAGE_ID;

    if (stmt->type == ast::IndexType::HASH) {
        // 创建哈希索引
        LOG_INFO(QString("Creating HASH index '%1' on column '%2'")
                     .arg(stmt->indexName).arg(columnName));

        HashIndex* hashIndex = new HashIndex(stmt->indexName, column.type, bufferPool, 256);

        // 扫描表，将所有记录插入索引
        PageId currentPageId = table->firstPageId;
        int totalRows = 0;

        while (currentPageId != INVALID_PAGE_ID) {
            Page* page = bufferPool->fetchPage(currentPageId);
            if (!page) {
                delete hashIndex;
                return createErrorResult(ErrorCode::IO_ERROR,
                                        QString("Failed to fetch page %1").arg(currentPageId));
            }

            QVector<QVector<QVariant>> pageRecords;
            QVector<RowId> rowIds;

            if (TablePage::getAllRecords(page, table, pageRecords, &rowIds)) {
                for (int i = 0; i < pageRecords.size(); ++i) {
                    const QVector<QVariant>& record = pageRecords[i];
                    RowId rowId = rowIds[i];

                    QVariant keyValue = record[columnIndex];

                    if (keyValue.isNull()) {
                        LOG_DEBUG(QString("Skipping NULL value in index column for row %1").arg(rowId));
                        continue;
                    }

                    if (!hashIndex->insert(keyValue, rowId)) {
                        bufferPool->unpinPage(currentPageId, false);
                        delete hashIndex;
                        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                                QString("Failed to insert key into hash index"));
                    }

                    totalRows++;
                }
            }

            PageHeader* header = page->getHeader();
            PageId nextPageId = header->nextPageId;
            bufferPool->unpinPage(currentPageId, false);
            currentPageId = nextPageId;
        }

        rootPageId = hashIndex->getDirectoryPageId();
        delete hashIndex;

        LOG_INFO(QString("HASH index '%1' created successfully (%2 rows indexed)")
                     .arg(stmt->indexName).arg(totalRows));
    }
    else if (stmt->type == ast::IndexType::BTREE) {
        // 创建通用B+树索引
        LOG_INFO(QString("Creating BTREE index '%1' on column '%2'")
                     .arg(stmt->indexName).arg(columnName));

        GenericBPlusTree* genericBTree = new GenericBPlusTree(bufferPool, column.type);

        // 扫描表，将所有记录插入索引
        PageId currentPageId = table->firstPageId;
        int totalRows = 0;

        while (currentPageId != INVALID_PAGE_ID) {
            Page* page = bufferPool->fetchPage(currentPageId);
            if (!page) {
                delete genericBTree;
                return createErrorResult(ErrorCode::IO_ERROR,
                                        QString("Failed to fetch page %1").arg(currentPageId));
            }

            QVector<QVector<QVariant>> pageRecords;
            QVector<RowId> rowIds;

            if (TablePage::getAllRecords(page, table, pageRecords, &rowIds)) {
                for (int i = 0; i < pageRecords.size(); ++i) {
                    const QVector<QVariant>& record = pageRecords[i];
                    RowId rowId = rowIds[i];

                    QVariant keyValue = record[columnIndex];

                    if (keyValue.isNull()) {
                        LOG_DEBUG(QString("Skipping NULL value in index column for row %1").arg(rowId));
                        continue;
                    }

                    if (!genericBTree->insert(keyValue, rowId)) {
                        bufferPool->unpinPage(currentPageId, false);
                        delete genericBTree;
                        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                                QString("Failed to insert key into index"));
                    }

                    totalRows++;
                }
            }

            PageHeader* header = page->getHeader();
            PageId nextPageId = header->nextPageId;
            bufferPool->unpinPage(currentPageId, false);
            currentPageId = nextPageId;
        }

        rootPageId = genericBTree->getRootPageId();
        delete genericBTree;

        LOG_INFO(QString("BTREE index '%1' created successfully (%2 rows indexed)")
                     .arg(stmt->indexName).arg(totalRows));
    }
    else if (stmt->type == ast::IndexType::FULLTEXT) {
        // 创建全文索引（倒排索引）
        LOG_INFO(QString("Creating FULLTEXT index '%1' on column '%2'")
                     .arg(stmt->indexName).arg(columnName));

        // 检查列类型是否为文本类型
        if (column.type != DataType::VARCHAR &&
            column.type != DataType::TEXT &&
            column.type != DataType::CHAR) {
            return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                    QString("FULLTEXT index can only be created on text columns (VARCHAR, TEXT, CHAR)"));
        }

        InvertedIndex* invertedIndex = new InvertedIndex(stmt->indexName, bufferPool);

        // 扫描表，将所有记录插入索引
        PageId currentPageId = table->firstPageId;
        int totalRows = 0;

        while (currentPageId != INVALID_PAGE_ID) {
            Page* page = bufferPool->fetchPage(currentPageId);
            if (!page) {
                delete invertedIndex;
                return createErrorResult(ErrorCode::IO_ERROR,
                                        QString("Failed to fetch page %1").arg(currentPageId));
            }

            QVector<QVector<QVariant>> pageRecords;
            QVector<RowId> rowIds;

            if (TablePage::getAllRecords(page, table, pageRecords, &rowIds)) {
                for (int i = 0; i < pageRecords.size(); ++i) {
                    const QVector<QVariant>& record = pageRecords[i];
                    RowId rowId = rowIds[i];

                    QVariant textValue = record[columnIndex];

                    if (textValue.isNull()) {
                        LOG_DEBUG(QString("Skipping NULL value in index column for row %1").arg(rowId));
                        continue;
                    }

                    QString text = textValue.toString();
                    if (!invertedIndex->insert(rowId, text)) {
                        bufferPool->unpinPage(currentPageId, false);
                        delete invertedIndex;
                        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                                QString("Failed to insert document into inverted index"));
                    }

                    totalRows++;
                }
            }

            PageHeader* header = page->getHeader();
            PageId nextPageId = header->nextPageId;
            bufferPool->unpinPage(currentPageId, false);
            currentPageId = nextPageId;
        }

        rootPageId = invertedIndex->getRootPageId();
        delete invertedIndex;

        LOG_INFO(QString("FULLTEXT index '%1' created successfully (%2 documents indexed)")
                     .arg(stmt->indexName).arg(totalRows));
    }
    else {
        return createErrorResult(ErrorCode::NOT_IMPLEMENTED,
                                QString("Index type not yet implemented"));
    }

    // 创建索引元数据
    IndexDef indexDef;
    indexDef.name = stmt->indexName;
    indexDef.tableName = stmt->tableName;
    indexDef.columns.append(columnName);
    // 根据stmt->type设置索引类型
    if (stmt->type == ast::IndexType::HASH) {
        indexDef.indexType = qindb::IndexType::HASH;
    } else if (stmt->type == ast::IndexType::BTREE) {
        indexDef.indexType = qindb::IndexType::BTREE;
    } else if (stmt->type == ast::IndexType::FULLTEXT) {
        indexDef.indexType = qindb::IndexType::INVERTED;
    } else {
        indexDef.indexType = qindb::IndexType::BTREE; // 默认
    }
    indexDef.keyType = column.type;             // 保存键的数据类型
    indexDef.unique = stmt->unique;
    indexDef.rootPageId = rootPageId;

    // 保存索引到catalog
    if (!catalog->createIndex(indexDef)) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                "Failed to create index in catalog");
    }

    // 保存元数据到磁盘
    QString dbPath = dbManager_->getDatabasePath(dbManager_->currentDatabaseName());
    QString catalogPath = dbPath + "/catalog.json";
    catalog->save(catalogPath);

    // 刷新所有脏页
    bufferPool->flushAllPages();

    QString indexTypeStr = getIndexTypeName(indexDef.indexType);
    LOG_INFO(QString("Index '%1' (%2) created successfully on table '%3', column '%4'")
                .arg(stmt->indexName)
                .arg(indexTypeStr)
                .arg(stmt->tableName)
                .arg(columnName));

    return createSuccessResult(QString("Index '%1' (%2) created on table '%3', column '%4'")
                                   .arg(stmt->indexName)
                                   .arg(indexTypeStr)
                                   .arg(stmt->tableName)
                                   .arg(columnName));
}

QueryResult Executor::executeDropIndex(const DropIndexStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid DROP INDEX statement");
    }

    LOG_INFO(QString("Executing DROP INDEX: %1").arg(stmt->indexName));

    // 获取当前数据库的组件
    Catalog* catalog = dbManager_->getCurrentCatalog();

    if (!catalog) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 检查索引是否存在
    const IndexDef* index = catalog->getIndex(stmt->indexName);
    if (!index) {
        if (stmt->ifExists) {
            return createSuccessResult(QString("Index '%1' does not exist (skipped)").arg(stmt->indexName));
        }
        return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                QString("Index '%1' does not exist").arg(stmt->indexName));
    }

    // 删除索引
    if (!catalog->dropIndex(stmt->indexName)) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                QString("Failed to drop index '%1'").arg(stmt->indexName));
    }

    // 保存元数据到磁盘
    QString dbPath = dbManager_->getDatabasePath(dbManager_->currentDatabaseName());
    QString catalogPath = dbPath + "/catalog.json";
    catalog->save(catalogPath);

    LOG_INFO(QString("Index '%1' dropped successfully").arg(stmt->indexName));

    return createSuccessResult(QString("Index '%1' dropped").arg(stmt->indexName));
}

QueryResult Executor::executeVacuum(const VacuumStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid VACUUM statement");
    }

    LOG_INFO(QString("Executing VACUUM: %1").arg(stmt->tableName.isEmpty() ? "ALL TABLES" : stmt->tableName));

    // 获取当前数据库的组件
    Catalog* catalog = dbManager_->getCurrentCatalog();
    BufferPoolManager* bufferPool = dbManager_->getCurrentBufferPool();
    TransactionManager* txnManager = dbManager_->getCurrentTransactionManager();

    if (!catalog || !bufferPool || !txnManager) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 创建 VacuumWorker
    VacuumWorker vacuumWorker(txnManager, bufferPool);

    int totalCleaned = 0;

    if (stmt->tableName.isEmpty()) {
        // VACUUM 所有表
        QVector<QString> tableNames = catalog->getAllTableNames();

        for (const QString& tableName : tableNames) {
            const TableDef* table = catalog->getTable(tableName);
            if (table) {
                int cleaned = vacuumWorker.cleanupTable(table);
                totalCleaned += cleaned;

                LOG_INFO(QString("VACUUM: Cleaned %1 records from table '%2'")
                            .arg(cleaned)
                            .arg(tableName));
            }
        }

        // 刷新所有脏页到磁盘
        bufferPool->flushAllPages();

        return createSuccessResult(QString("VACUUM completed: %1 records cleaned from all tables")
                                       .arg(totalCleaned));
    } else {
        // VACUUM 指定表
        const TableDef* table = catalog->getTable(stmt->tableName);
        if (!table) {
            return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                    QString("Table '%1' does not exist").arg(stmt->tableName));
        }

        totalCleaned = vacuumWorker.cleanupTable(table);

        // 刷新所有脏页到磁盘
        bufferPool->flushAllPages();

        return createSuccessResult(QString("VACUUM completed: %1 records cleaned from table '%2'")
                                       .arg(totalCleaned)
                                       .arg(stmt->tableName));
    }
}

QueryResult Executor::executeBegin(const BeginTransactionStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid BEGIN statement");
    }

    LOG_INFO("Executing BEGIN TRANSACTION");

    // 获取事务管理器
    TransactionManager* txnManager = dbManager_->getCurrentTransactionManager();
    if (!txnManager) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 检查是否已经有活跃事务
    TransactionId currentTxnId = dbManager_->getCurrentTransactionId();
    if (currentTxnId != INVALID_TXN_ID) {
        TransactionState state = txnManager->getTransactionState(currentTxnId);
        if (state == TransactionState::ACTIVE) {
            return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                    QString("Transaction %1 is already active. Commit or rollback first.")
                                        .arg(currentTxnId));
        }
    }

    // 开始新事务
    TransactionId newTxnId = txnManager->beginTransaction();
    dbManager_->setCurrentTransactionId(newTxnId);

    LOG_INFO(QString("Transaction %1 started").arg(newTxnId));

    return createSuccessResult(QString("Transaction %1 started").arg(newTxnId));
}

QueryResult Executor::executeCommit(const CommitStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid COMMIT statement");
    }

    LOG_INFO("Executing COMMIT");

    // 获取事务管理器
    TransactionManager* txnManager = dbManager_->getCurrentTransactionManager();
    if (!txnManager) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 检查是否有活跃事务
    TransactionId currentTxnId = dbManager_->getCurrentTransactionId();
    if (currentTxnId == INVALID_TXN_ID) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No active transaction to commit");
    }

    // 检查事务状态
    TransactionState state = txnManager->getTransactionState(currentTxnId);
    if (state != TransactionState::ACTIVE) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                QString("Transaction %1 is not active").arg(currentTxnId));
    }

    // 提交事务
    bool success = txnManager->commitTransaction(currentTxnId);
    if (!success) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                QString("Failed to commit transaction %1").arg(currentTxnId));
    }

    // 清除当前事务ID
    dbManager_->setCurrentTransactionId(INVALID_TXN_ID);

    LOG_INFO(QString("Transaction %1 committed").arg(currentTxnId));

    return createSuccessResult(QString("Transaction %1 committed").arg(currentTxnId));
}

QueryResult Executor::executeRollback(const RollbackStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid ROLLBACK statement");
    }

    LOG_INFO("Executing ROLLBACK");

    // 获取事务管理器
    TransactionManager* txnManager = dbManager_->getCurrentTransactionManager();
    if (!txnManager) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 检查是否有活跃事务
    TransactionId currentTxnId = dbManager_->getCurrentTransactionId();
    if (currentTxnId == INVALID_TXN_ID) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No active transaction to rollback");
    }

    // 检查事务状态
    TransactionState state = txnManager->getTransactionState(currentTxnId);
    if (state != TransactionState::ACTIVE) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                QString("Transaction %1 is not active").arg(currentTxnId));
    }

    // 获取必要的组件
    Catalog* catalog = dbManager_->getCurrentCatalog();
    BufferPoolManager* bufferPool = dbManager_->getCurrentBufferPool();

    if (!catalog || !bufferPool) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected.");
    }

    // 获取事务对象和Undo Log
    Transaction* txn = txnManager->getTransaction(currentTxnId);
    if (!txn) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Transaction object not found");
    }

    // 执行 Undo 操作（逆序执行）
    int undoCount = 0;
    for (int i = txn->undoLog.size() - 1; i >= 0; --i) {
        const UndoRecord& undo = txn->undoLog[i];

        // 获取表定义
        const TableDef* table = catalog->getTable(undo.tableName);
        if (!table) {
            LOG_ERROR(QString("Table '%1' not found during rollback").arg(undo.tableName));
            continue;
        }

        // 获取页面
        Page* page = bufferPool->fetchPage(undo.pageId);
        if (!page) {
            LOG_ERROR(QString("Failed to fetch page %1 during rollback").arg(undo.pageId));
            continue;
        }

        // 根据操作类型执行相应的撤销
        switch (undo.opType) {
        case UndoOperationType::INSERT:
            // 撤销 INSERT：删除记录
            if (TablePage::deleteRecord(page, undo.slotIndex, currentTxnId)) {
                undoCount++;
                LOG_DEBUG(QString("Undo INSERT: deleted record at page %1, slot %2")
                             .arg(undo.pageId).arg(undo.slotIndex));
            }
            bufferPool->unpinPage(undo.pageId, true);
            break;

        case UndoOperationType::UPDATE:
            // 撤销 UPDATE：恢复旧值
            if (TablePage::updateRecord(page, table, undo.slotIndex, undo.oldValues)) {
                undoCount++;
                LOG_DEBUG(QString("Undo UPDATE: restored old values at page %1, slot %2")
                             .arg(undo.pageId).arg(undo.slotIndex));
            }
            bufferPool->unpinPage(undo.pageId, true);
            break;

        case UndoOperationType::DELETE:
            // 撤销 DELETE：恢复记录（清除deleteTxnId）
            // 这里我们需要调用一个特殊的方法来取消逻辑删除
            // 简单的做法是直接更新 deleteTxnId 为 INVALID_TXN_ID
            {
                RecordHeader* header = TablePage::getRecordHeader(page, undo.slotIndex);
                if (header) {
                    header->deleteTxnId = INVALID_TXN_ID;
                    undoCount++;
                    LOG_DEBUG(QString("Undo DELETE: restored record at page %1, slot %2")
                                 .arg(undo.pageId).arg(undo.slotIndex));
                }
                bufferPool->unpinPage(undo.pageId, true);
            }
            break;

        default:
            bufferPool->unpinPage(undo.pageId, false);
            LOG_WARN(QString("Unknown undo operation type: %1").arg(static_cast<int>(undo.opType)));
            break;
        }
    }

    LOG_INFO(QString("Executed %1 undo operations for transaction %2")
                .arg(undoCount).arg(currentTxnId));

    // 刷新脏页
    bufferPool->flushAllPages();

    // 回滚事务（释放锁等）
    bool success = txnManager->abortTransaction(currentTxnId);
    if (!success) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                QString("Failed to rollback transaction %1").arg(currentTxnId));
    }

    // 清除当前事务ID
    dbManager_->setCurrentTransactionId(INVALID_TXN_ID);

    LOG_INFO(QString("Transaction %1 rolled back (%2 operations undone)")
                .arg(currentTxnId).arg(undoCount));

    return createSuccessResult(QString("Transaction %1 rolled back (%2 operations undone)")
                                   .arg(currentTxnId).arg(undoCount));
}

void Executor::setQueryRewriteEnabled(bool enabled) {
    queryRewriteEnabled_ = enabled;
    LOG_INFO(QString("Query rewrite %1").arg(enabled ? "enabled" : "disabled"));
}

QueryRewriter* Executor::getQueryRewriter() {
    return queryRewriter_.get();
}

void Executor::setQueryCacheEnabled(bool enabled) {
    if (queryCache_) {
        queryCache_->setEnabled(enabled);
        LOG_INFO(QString("Query cache %1").arg(enabled ? "enabled" : "disabled"));
    }
}

void Executor::clearQueryCache() {
    if (queryCache_) {
        queryCache_->clear();
        LOG_INFO("Query cache cleared");
    }
}

Executor::QueryCacheStats Executor::getQueryCacheStats() const {
    QueryCacheStats stats;
    if (queryCache_) {
        QueryCache::Statistics cacheStats = queryCache_->getStatistics();
        stats.totalEntries = cacheStats.totalEntries;
        stats.totalHits = cacheStats.totalHits;
        stats.totalMisses = cacheStats.totalMisses;
        stats.totalEvictions = cacheStats.totalEvictions;
        stats.totalMemoryBytes = cacheStats.totalMemoryBytes;
        stats.hitRate = cacheStats.hitRate;
    }
    return stats;
}

// CBO相关的Executor方法 - 临时文件

QueryResult Executor::executeAnalyze(const AnalyzeStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid ANALYZE statement");
    }

    LOG_INFO(QString("Executing ANALYZE: %1").arg(stmt->tableName.isEmpty() ? "ALL TABLES" : stmt->tableName));

    // 获取当前数据库的组件
    Catalog* catalog = dbManager_->getCurrentCatalog();
    BufferPoolManager* bufferPool = dbManager_->getCurrentBufferPool();

    if (!catalog || !bufferPool) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 创建统计收集器
    StatisticsCollector statsCollector(catalog, bufferPool);

    bool success = false;
    QString message;

    if (stmt->tableName.isEmpty()) {
        // 收集所有表的统计信息
        success = statsCollector.collectAllStats();
        message = success ? "Statistics collected for all tables" : "Failed to collect statistics";
    } else {
        // 收集指定表的统计信息
        const TableDef* table = catalog->getTable(stmt->tableName);
        if (!table) {
            return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                   QString("Table '%1' does not exist").arg(stmt->tableName));
        }

        success = statsCollector.collectTableStats(stmt->tableName);
        message = success
                ? QString("Statistics collected for table '%1'").arg(stmt->tableName)
                : QString("Failed to collect statistics for table '%1'").arg(stmt->tableName);
    }

    // 保存统计信息到文件
    if (success) {
        QString statsPath = dbManager_->getDatabasePath(dbManager_->currentDatabaseName()) + "/statistics.json";
        statsCollector.saveStats(statsPath);
    }

    return success ? createSuccessResult(message)
                   : createErrorResult(ErrorCode::INTERNAL_ERROR, message);
}

QueryResult Executor::executeExplain(const ExplainStatement* stmt) {
    if (!stmt || !stmt->query) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid EXPLAIN statement");
    }

    LOG_INFO("Executing EXPLAIN for query");

    // 获取当前数据库的组件
    Catalog* catalog = dbManager_->getCurrentCatalog();
    BufferPoolManager* bufferPool = dbManager_->getCurrentBufferPool();

    if (!catalog || !bufferPool) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR, "No database selected. Use 'USE DATABASE <name>' first.");
    }

    // 创建统计收集器和CBO优化器
    StatisticsCollector statsCollector(catalog, bufferPool);

    // 尝试加载统计信息
    QString statsPath = dbManager_->getDatabasePath(dbManager_->currentDatabaseName()) + "/statistics.json";
    statsCollector.loadStats(statsPath);

    CostOptimizer optimizer(catalog, &statsCollector);

    // 生成执行计划
    auto plan = optimizer.optimizeSelect(stmt->query.get());

    if (!plan) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Failed to generate execution plan");
    }

    // 格式化执行计划输出
    QString planStr = formatPlan(plan.get(), 0);

    QueryResult result;
    result.success = true;
    result.message = "Execution Plan";
    result.columnNames = {"Plan"};
    result.rows = {{QVariant(planStr)}};

    return result;
}

QString Executor::formatPlan(const PlanNode* node, int indent) const {
    if (!node) return "";

    QString indentStr(indent * 2, ' ');
    QString result;

    // 节点类型
    QString nodeType;
    switch (node->nodeType) {
        case PlanNodeType::SEQ_SCAN:
            nodeType = "SeqScan";
            break;
        case PlanNodeType::INDEX_SCAN:
            nodeType = "IndexScan";
            break;
        case PlanNodeType::NESTED_LOOP_JOIN:
            nodeType = "NestedLoopJoin";
            break;
        case PlanNodeType::HASH_JOIN:
            nodeType = "HashJoin";
            break;
        case PlanNodeType::SORT_MERGE_JOIN:
            nodeType = "SortMergeJoin";
            break;
        case PlanNodeType::SORT:
            nodeType = "Sort";
            break;
        case PlanNodeType::LIMIT:
            nodeType = "Limit";
            break;
        default:
            nodeType = "Unknown";
    }

    // 格式化当前节点
    result += indentStr + nodeType;

    if (!node->tableName.isEmpty()) {
        result += " on " + node->tableName;
    }

    if (!node->indexName.isEmpty()) {
        result += " using " + node->indexName;
    }

    result += QString(" (cost=%1 rows=%2)\n")
                .arg(node->cost.totalCost, 0, 'f', 2)
                .arg(node->cost.estimatedRows);

    // 递归格式化子节点
    for (const auto& child : node->children) {
        result += formatPlan(child.get(), indent + 1);
    }

    return result;
}

// ========== 用户管理方法 ==========

void Executor::setAuthManager(AuthManager* authManager) {
    authManager_ = authManager;
}

void Executor::setPermissionManager(PermissionManager* permissionManager) {
    permissionManager_ = permissionManager;
}

void Executor::setCurrentUser(const QString& username) {
    currentUser_ = username;
}

QueryResult Executor::executeCreateUser(const CreateUserStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid CREATE USER statement");
    }

    LOG_INFO(QString("Executing CREATE USER: %1").arg(stmt->username));

    // 检查AuthManager是否可用
    if (!authManager_) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                "Authentication manager not available");
    }

    // 检查用户名是否已存在
    if (authManager_->userExists(stmt->username)) {
        return createErrorResult(ErrorCode::DUPLICATE_KEY,
                                QString("User '%1' already exists").arg(stmt->username));
    }

    // 创建用户
    bool success = authManager_->createUser(stmt->username, stmt->password, stmt->isAdmin);

    if (!success) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                QString("Failed to create user '%1'").arg(stmt->username));
    }

    LOG_INFO(QString("User '%1' created successfully").arg(stmt->username));
    return createSuccessResult(QString("User '%1' created successfully").arg(stmt->username));
}

QueryResult Executor::executeDropUser(const DropUserStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid DROP USER statement");
    }

    LOG_INFO(QString("Executing DROP USER: %1").arg(stmt->username));

    // 检查AuthManager是否可用
    if (!authManager_) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                "Authentication manager not available");
    }

    // 检查用户是否存在
    if (!authManager_->userExists(stmt->username)) {
        return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                QString("User '%1' does not exist").arg(stmt->username));
    }

    // 删除用户
    bool success = authManager_->dropUser(stmt->username);

    if (!success) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                QString("Failed to drop user '%1'").arg(stmt->username));
    }

    LOG_INFO(QString("User '%1' dropped successfully").arg(stmt->username));
    return createSuccessResult(QString("User '%1' dropped successfully").arg(stmt->username));
}

QueryResult Executor::executeAlterUser(const AlterUserStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid ALTER USER statement");
    }

    LOG_INFO(QString("Executing ALTER USER: %1").arg(stmt->username));

    // 检查AuthManager是否可用
    if (!authManager_) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                "Authentication manager not available");
    }

    // 检查用户是否存在
    if (!authManager_->userExists(stmt->username)) {
        return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                QString("User '%1' does not exist").arg(stmt->username));
    }

    // 修改用户密码
    bool success = authManager_->alterUserPassword(stmt->username, stmt->newPassword);

    if (!success) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                QString("Failed to alter user '%1'").arg(stmt->username));
    }

    LOG_INFO(QString("User '%1' password updated successfully").arg(stmt->username));
    return createSuccessResult(QString("User '%1' password updated successfully").arg(stmt->username));
}

// ========== 权限管理方法 ==========

// 辅助函数：将AST PrivilegeType转换为PermissionManager的PermissionType
static PermissionType convertPrivilegeType(ast::PrivilegeType privType) {
    switch (privType) {
        case ast::PrivilegeType::SELECT: return PermissionType::SELECT;
        case ast::PrivilegeType::INSERT: return PermissionType::INSERT;
        case ast::PrivilegeType::UPDATE: return PermissionType::UPDATE;
        case ast::PrivilegeType::DELETE_PRIV: return PermissionType::DELETE;
        case ast::PrivilegeType::ALL: return PermissionType::ALL;
        default: return PermissionType::SELECT;
    }
}

QueryResult Executor::executeGrant(const GrantStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid GRANT statement");
    }

    if (authManager_ && !currentUser_.isEmpty() && !authManager_->isUserAdmin(currentUser_)) {
        return createErrorResult(ErrorCode::PERMISSION_DENIED,
                                QString("User '%1' cannot grant permissions").arg(currentUser_));
    }

    LOG_INFO(QString("Executing GRANT %1 ON %2.%3 TO %4")
                 .arg(static_cast<int>(stmt->privilegeType))
                 .arg(stmt->databaseName)
                 .arg(stmt->tableName.isEmpty() ? "*" : stmt->tableName)
                 .arg(stmt->username));

    // 检查PermissionManager是否可用
    if (!permissionManager_) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                "Permission manager not available");
    }

    // 检查AuthManager是否可用（需要验证用户存在）
    if (!authManager_) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                "Authentication manager not available");
    }

    // 检查用户是否存在
    if (!authManager_->userExists(stmt->username)) {
        return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                QString("User '%1' does not exist").arg(stmt->username));
    }

    // 检查数据库是否存在
    if (!dbManager_->databaseExists(stmt->databaseName)) {
        return createErrorResult(ErrorCode::SEMANTIC_ERROR,
                                QString("Database '%1' does not exist").arg(stmt->databaseName));
    }

    // 如果指定了表名，检查表是否存在
    if (!stmt->tableName.isEmpty()) {
        // 需要临时切换到目标数据库检查表
        QString currentDb = dbManager_->currentDatabaseName();
        dbManager_->useDatabase(stmt->databaseName);

        Catalog* catalog = dbManager_->getCurrentCatalog();
        if (!catalog || !catalog->tableExists(stmt->tableName)) {
            dbManager_->useDatabase(currentDb);  // 恢复原数据库
            return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                    QString("Table '%1' does not exist in database '%2'")
                                        .arg(stmt->tableName).arg(stmt->databaseName));
        }

        dbManager_->useDatabase(currentDb);  // 恢复原数据库
    }

    // 转换权限类型
    PermissionType permType = convertPrivilegeType(stmt->privilegeType);

    // 授予权限
    bool success = permissionManager_->grantPermission(
        stmt->username,
        stmt->databaseName,
        stmt->tableName,
        permType,
        stmt->withGrantOption
    );

    if (!success) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                QString("Failed to grant permission to user '%1'").arg(stmt->username));
    }

    QString privStr = PermissionManager::permissionTypeToString(permType);
    QString targetStr = stmt->tableName.isEmpty()
        ? QString("%1.*").arg(stmt->databaseName)
        : QString("%1.%2").arg(stmt->databaseName).arg(stmt->tableName);

    LOG_INFO(QString("Permission '%1' on '%2' granted to user '%3'")
                 .arg(privStr).arg(targetStr).arg(stmt->username));

    return createSuccessResult(QString("Permission '%1' on '%2' granted to user '%3'")
                                   .arg(privStr).arg(targetStr).arg(stmt->username));
}

QueryResult Executor::executeRevoke(const RevokeStatement* stmt) {
    if (!stmt) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR, "Invalid REVOKE statement");
    }

    if (authManager_ && !currentUser_.isEmpty() && !authManager_->isUserAdmin(currentUser_)) {
        return createErrorResult(ErrorCode::PERMISSION_DENIED,
                                QString("User '%1' cannot revoke permissions").arg(currentUser_));
    }

    LOG_INFO(QString("Executing REVOKE %1 ON %2.%3 FROM %4")
                 .arg(static_cast<int>(stmt->privilegeType))
                 .arg(stmt->databaseName)
                 .arg(stmt->tableName.isEmpty() ? "*" : stmt->tableName)
                 .arg(stmt->username));

    // 检查PermissionManager是否可用
    if (!permissionManager_) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                "Permission manager not available");
    }

    // 检查AuthManager是否可用（需要验证用户存在）
    if (!authManager_) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                "Authentication manager not available");
    }

    // 检查用户是否存在
    if (!authManager_->userExists(stmt->username)) {
        return createErrorResult(ErrorCode::TABLE_NOT_FOUND,
                                QString("User '%1' does not exist").arg(stmt->username));
    }

    // 转换权限类型
    PermissionType permType = convertPrivilegeType(stmt->privilegeType);

    // 撤销权限
    bool success = permissionManager_->revokePermission(
        stmt->username,
        stmt->databaseName,
        stmt->tableName,
        permType
    );

    if (!success) {
        return createErrorResult(ErrorCode::INTERNAL_ERROR,
                                QString("Failed to revoke permission from user '%1'").arg(stmt->username));
    }

    QString privStr = PermissionManager::permissionTypeToString(permType);
    QString targetStr = stmt->tableName.isEmpty()
        ? QString("%1.*").arg(stmt->databaseName)
        : QString("%1.%2").arg(stmt->databaseName).arg(stmt->tableName);

    LOG_INFO(QString("Permission '%1' on '%2' revoked from user '%3'")
                 .arg(privStr).arg(targetStr).arg(stmt->username));

    return createSuccessResult(QString("Permission '%1' on '%2' revoked from user '%3'")
                                   .arg(privStr).arg(targetStr).arg(stmt->username));
}

bool Executor::checkSelectPermissions(const SelectStatement* stmt, QueryResult& errorOut) {
    if (!stmt || !stmt->from) {
        return true;
    }

    const QString currentDb = dbManager_->currentDatabaseName();
    if (!ensurePermission(currentDb, stmt->from->tableName, PermissionType::SELECT, errorOut)) {
        return false;
    }

    for (const auto& joinPtr : stmt->joins) {
        if (!joinPtr || !joinPtr->right) {
            continue;
        }
        const QString tableName = joinPtr->right->tableName;
        if (!ensurePermission(currentDb, tableName, PermissionType::SELECT, errorOut)) {
            return false;
        }
    }

    return true;
}

bool Executor::ensurePermission(const QString& databaseName,
                                const QString& tableName,
                                PermissionType permType,
                                QueryResult& errorOut) {
    QString effectiveDb = databaseName.isEmpty() ? dbManager_->currentDatabaseName() : databaseName;

    if (!authManager_ || currentUser_.isEmpty()) {
        return true;
    }

    if (authManager_->isUserAdmin(currentUser_)) {
        return true;
    }

    PermissionManager* manager = permissionManager_;
    if (!manager) {
        manager = dbManager_->getCurrentPermissionManager();
    }
    if (!manager) {
        errorOut = createErrorResult(ErrorCode::INTERNAL_ERROR,
                                     "Permission manager not available");
        return false;
    }

    if (manager->hasPermission(currentUser_, effectiveDb, tableName, permType)) {
        return true;
    }

    QString privName = PermissionManager::permissionTypeToString(permType);
    QString target = tableName.isEmpty()
        ? QString("%1.*").arg(effectiveDb)
        : QString("%1.%2").arg(effectiveDb, tableName);

    errorOut = createErrorResult(
        ErrorCode::PERMISSION_DENIED,
        QString("User '%1' lacks %2 permission on %3")
            .arg(currentUser_)
            .arg(privName)
            .arg(target));
    return false;
}


} // namespace qindb
