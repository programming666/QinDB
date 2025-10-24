#ifndef QINDB_EXECUTOR_H
#define QINDB_EXECUTOR_H

#include "common.h"
#include "ast.h"
#include "catalog.h"
#include "buffer_pool_manager.h"
#include "disk_manager.h"
#include "database_manager.h"
#include "expression_evaluator.h"
#include "auth_manager.h"
#include "query_result.h"
#include <QString>
#include <QVector>
#include <memory>

namespace qindb {

// Forward declaration
class QueryRewriter;
class PermissionManager;
class QueryCache;
// CBO forward declarations
struct PlanNode;

// 使用 ast 命名空间中的类
using ast::ASTNode;
using ast::CreateTableStatement;
using ast::DropTableStatement;
using ast::InsertStatement;
using ast::SelectStatement;
using ast::UpdateStatement;
using ast::DeleteStatement;
using ast::CreateDatabaseStatement;
using ast::DropDatabaseStatement;
using ast::UseDatabaseStatement;
using ast::ShowDatabasesStatement;
using ast::SaveStatement;
using ast::CreateIndexStatement;
using ast::DropIndexStatement;
using ast::VacuumStatement;
using ast::BeginTransactionStatement;
using ast::CommitStatement;
using ast::RollbackStatement;
using ast::CreateUserStatement;
using ast::DropUserStatement;
using ast::AlterUserStatement;
using ast::GrantStatement;
using ast::RevokeStatement;

/**
 * @brief 查询执行器
 *
 * 职责：
 * - 执行DDL语句（CREATE TABLE, DROP TABLE等）
 * - 执行DML语句（INSERT, SELECT, UPDATE, DELETE）
 * - 管理元数据（通过Catalog）
 * - 管理存储（通过BufferPoolManager）
 */
class Executor {
public:
    /**
     * @brief 构造函数
     */
    Executor(DatabaseManager* dbManager);

    ~Executor();

    /**
     * @brief 设置认证管理器
     */
    void setAuthManager(AuthManager* authManager);

    /**
     * @brief 设置权限管理器
     */
    void setPermissionManager(PermissionManager* permissionManager);

    /**
     * @brief ????????
     */
    void setCurrentUser(const QString& username);

    /**
     * @brief ????????
     */
    const QString& currentUser() const { return currentUser_; }

    /**
     * @brief 执行SQL语句（AST）
     */
    QueryResult execute(const std::unique_ptr<ASTNode>& ast);

    /**
     * @brief 执行CREATE TABLE语句
     */
    QueryResult executeCreateTable(const CreateTableStatement* stmt);

    /**
     * @brief 执行DROP TABLE语句
     */
    QueryResult executeDropTable(const DropTableStatement* stmt);

    /**
     * @brief 执行INSERT语句
     */
    QueryResult executeInsert(const InsertStatement* stmt);

    /**
     * @brief 执行SELECT语句
     */
    QueryResult executeSelect(const SelectStatement* stmt);

    /**
     * @brief 执行UPDATE语句
     */
    QueryResult executeUpdate(const UpdateStatement* stmt);

    /**
     * @brief 执行DELETE语句
     */
    QueryResult executeDelete(const DeleteStatement* stmt);

    /**
     * @brief 执行SHOW TABLES语句
     */
    QueryResult executeShowTables();

    /**
     * @brief 执行CREATE DATABASE语句
     */
    QueryResult executeCreateDatabase(const CreateDatabaseStatement* stmt);

    /**
     * @brief 执行DROP DATABASE语句
     */
    QueryResult executeDropDatabase(const DropDatabaseStatement* stmt);

    /**
     * @brief 执行USE DATABASE语句
     */
    QueryResult executeUseDatabase(const UseDatabaseStatement* stmt);

    /**
     * @brief 执行SHOW DATABASES语句
     */
    QueryResult executeShowDatabases(const ShowDatabasesStatement* stmt);

    /**
     * @brief 执行SAVE语句（保存数据库到磁盘）
     */
    QueryResult executeSave();

    /**
     * @brief 执行CREATE INDEX语句
     */
    QueryResult executeCreateIndex(const CreateIndexStatement* stmt);

    /**
     * @brief 执行DROP INDEX语句
     */
    QueryResult executeDropIndex(const DropIndexStatement* stmt);

    /**
     * @brief 执行VACUUM语句（垃圾回收）
     */
    QueryResult executeVacuum(const VacuumStatement* stmt);
    /**
     * @brief 执行ANALYZE语句（收集统计信息）
     */
    QueryResult executeAnalyze(const ast::AnalyzeStatement* stmt);

    /**
     * @brief 执行EXPLAIN语句（显示执行计划）
     */
    QueryResult executeExplain(const ast::ExplainStatement* stmt);

    /**
     * @brief 执行BEGIN事务语句
     */
    QueryResult executeBegin(const ast::BeginTransactionStatement* stmt);

    /**
     * @brief 执行COMMIT事务语句
     */
    QueryResult executeCommit(const ast::CommitStatement* stmt);

    /**
     * @brief 执行ROLLBACK事务语句
     */
    QueryResult executeRollback(const ast::RollbackStatement* stmt);

    /**
     * @brief 执行CREATE USER语句
     */
    QueryResult executeCreateUser(const ast::CreateUserStatement* stmt);

    /**
     * @brief 执行DROP USER语句
     */
    QueryResult executeDropUser(const ast::DropUserStatement* stmt);

    /**
     * @brief 执行ALTER USER语句
     */
    QueryResult executeAlterUser(const ast::AlterUserStatement* stmt);

    /**
     * @brief 执行GRANT语句
     */
    QueryResult executeGrant(const ast::GrantStatement* stmt);

    /**
     * @brief 执行REVOKE语句
     */
    QueryResult executeRevoke(const ast::RevokeStatement* stmt);

    /**
     * @brief 启用/禁用查询重写优化
     */
    void setQueryRewriteEnabled(bool enabled);

    /**
     * @brief 获取查询重写器实例（用于配置）
     */
    QueryRewriter* getQueryRewriter();

    /**
     * @brief 启用/禁用查询缓存
     */
    void setQueryCacheEnabled(bool enabled);

    /**
     * @brief 清空查询缓存
     */
    void clearQueryCache();

    /**
     * @brief 获取查询缓存统计信息
     */
    struct QueryCacheStats {
        uint64_t totalEntries;
        uint64_t totalHits;
        uint64_t totalMisses;
        uint64_t totalEvictions;
        uint64_t totalMemoryBytes;
        double hitRate;
    };
    QueryCacheStats getQueryCacheStats() const;

private:
    /**
     * @brief 将AST数据类型转换为内部DataType
     */
    DataType convertDataType(const QString& typeStr);

    /**
     * @brief 创建错误结果
     */
    QueryResult createErrorResult(ErrorCode code, const QString& message);

    /**
     * @brief 创建成功结果
     */
    QueryResult createSuccessResult(const QString& message);
    /**
     * @brief 格式化执行计划用于EXPLAIN输出
     */
    QString formatPlan(const PlanNode* node, int indent) const;

    bool checkSelectPermissions(const SelectStatement* stmt, QueryResult& errorOut);

    bool ensurePermission(const QString& databaseName,
                          const QString& tableName,
                          PermissionType permType,
                          QueryResult& errorOut);


    DatabaseManager* dbManager_;            // 数据库管理器
    AuthManager* authManager_ = nullptr;    // 认证管理器（可选）
    PermissionManager* permissionManager_ = nullptr;  // 权限管理器（可选）
    QString currentUser_;                   // 当前用户
    std::unique_ptr<QueryRewriter> queryRewriter_;  // 查询重写器
    bool queryRewriteEnabled_ = true;       // 是否启用查询重写
    std::unique_ptr<QueryCache> queryCache_;   // 查询缓存
};

} // namespace qindb

#endif // QINDB_EXECUTOR_H
