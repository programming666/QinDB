#ifndef QINDB_AUTH_MANAGER_H  // 防止重复包含该头文件
#define QINDB_AUTH_MANAGER_H

#include <QString>      // Qt字符串类
#include <QVector>      // Qt动态数组类
#include <QDateTime>    // Qt日期时间类
#include <optional>     // C++17可选值类型
#include <memory>       // 智能指针
#include "common.h"  // 包含PermissionType定义

namespace qindb {  // 定义qindb命名空间

class Catalog;           // 目录类的前向声明
class BufferPoolManager; // 缓冲池管理器类的前向声明
class DiskManager;       // 磁盘管理器类的前向声明

/**
 * @brief 用户记录结构
 * 
 * 存储用户的基本信息，包括ID、用户名、密码哈希、
 * 创建时间、更新时间、激活状态和管理员权限。
 */
struct UserRecord {
    uint64_t id;              // 用户唯一标识符
    QString username;         // 用户名
    QString passwordHash;  // Base64编码的密码哈希
    QDateTime createdAt;      // 用户创建时间
    QDateTime updatedAt;      // 用户信息最后更新时间
    bool isActive;           // 用户是否激活
    bool isAdmin;            // 用户是否为管理员

    // 默认构造函数
    UserRecord()
        : id(0)              // ID初始化为0
        , isActive(true)     // 默认激活状态
        , isAdmin(false) {}  // 默认非管理员
};

/**
 * @brief 认证管理器类
 *
 * 负责用户管理、认证验证、权限检查等功能。
 * 用户数据存储在系统数据库 qindb.users 表中。
 */
class AuthManager {
public:
    /**
     * @brief 构造函数
     * @param catalog 元数据管理器指针
     * @param bufferPool 缓冲池管理器指针
     * @param diskManager 磁盘管理器指针
     */
    AuthManager(Catalog* catalog,
                BufferPoolManager* bufferPool,
                DiskManager* diskManager);

    ~AuthManager();  // 析构函数

    // ========== 系统初始化 ==========

    /**
     * @brief 初始化用户系统表
     * @return 是否成功初始化
     *
     * 创建 qindb.users 表（如果不存在）
     * 创建默认管理员用户 admin/admin
     */
    bool initializeUserSystem();

    // ========== 用户管理 ==========

    /**
     * @brief 创建用户
     * @param username 用户名（唯一）
     * @param password 密码（明文，会自动哈希）
     * @param isAdmin 是否为管理员
     * @return 是否成功创建
     */
    bool createUser(const QString& username,
                   const QString& password,
                   bool isAdmin = false);

    /**
     * @brief 删除用户
     * @param username 用户名
     * @return 是否成功删除
     */
    bool dropUser(const QString& username);

    /**
     * @brief 修改用户密码
     * @param username 用户名
     * @param newPassword 新密码（明文）
     * @return 是否成功修改
     */
    bool alterUserPassword(const QString& username,
                          const QString& newPassword);

    /**
     * @brief 设置用户激活状态
     * @param username 用户名
     * @param active 是否激活
     * @return 是否成功设置
     */
    bool setUserActive(const QString& username, bool active);

    // ========== 认证验证 ==========

    /**
     * @brief 验证用户名和密码
     * @param username 用户名
     * @param password 密码（明文）
     * @return 是否认证成功
     */
    bool authenticate(const QString& username, const QString& password);

    // ========== 用户信息查询 ==========

    /**
     * @brief 检查用户是否存在
     */
    bool userExists(const QString& username);

    /**
     * @brief 检查用户是否为管理员
     */
    bool isUserAdmin(const QString& username);

    /**
     * @brief 检查用户是否激活
     */
    bool isUserActive(const QString& username);

    /**
     * @brief 获取用户记录
     * @param username 用户名
     * @return 用户记录（如果存在）
     */
    std::optional<UserRecord> getUser(const QString& username);

    /**
     * @brief 获取所有用户列表
     * @return 用户列表
     */
    QVector<UserRecord> getAllUsers();

    /**
     * @brief 获取用户数量
     */
    int getUserCount();

private:
    // ========== 辅助方法 ==========

    /**
     * @brief 从数据库获取用户记录
     */
    std::optional<UserRecord> getUserFromDatabase(const QString& username);

    /**
     * @brief 插入用户到数据库
     */
    bool insertUser(const UserRecord& user);

    /**
     * @brief 更新数据库中的用户记录
     */
    bool updateUser(const UserRecord& user);

    /**
     * @brief 从数据库删除用户
     */
    bool deleteUser(const QString& username);

    /**
     * @brief 生成下一个用户ID
     */
    uint64_t getNextUserId();

private:
    Catalog* catalog_;
    BufferPoolManager* bufferPool_;
    DiskManager* diskManager_;

    // 用户系统表名
    static constexpr const char* USERS_TABLE = "users";
    static constexpr const char* SYSTEM_DATABASE = "qindb";
};

} // namespace qindb

#endif // QINDB_AUTH_MANAGER_H
