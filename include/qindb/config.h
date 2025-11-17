#ifndef QINDB_CONFIG_H  // 防止重复包含的头文件保护宏
#define QINDB_CONFIG_H

#include <QString>    // Qt字符串类
#include <QSettings>  // Qt设置类，用于读写配置文件
#include <QMutex>     // Qt互斥锁，用于线程同步
#include <memory>     // 智能指针

namespace qindb {    // qinDB命名空间

/**
 * @brief 配置管理器类
 *
 * 负责读取和管理 qinDB 的配置选项
 * 配置文件: qindb.ini
 */
class Config {
public:
    /**
     * @brief 获取单例实例
     */
    static Config& instance();

    /**
     * @brief 加载配置文件
     * @param configPath 配置文件路径，默认为 "qindb.ini"
     * @return 是否成功加载
     */
    bool load(const QString& configPath = "qindb.ini");

    /**
     * @brief 保存配置到文件
     * @return 是否成功保存
     */
    bool save();

    // ========== 日志配置 ==========

    /**
     * @brief 是否启用详细输出（显示词法分析和语法分析详情）
     */
    bool isVerboseOutput() const { return verboseOutput_; }
    void setVerboseOutput(bool enabled) { verboseOutput_ = enabled; }

    /**
     * @brief 是否启用分析日志输出到文件
     */
    bool isAnalysisLogEnabled() const { return analysisLogEnabled_; }
    void setAnalysisLogEnabled(bool enabled) { analysisLogEnabled_ = enabled; }

    /**
     * @brief 获取分析日志文件路径
     */
    QString getAnalysisLogPath() const { return analysisLogPath_; }
    void setAnalysisLogPath(const QString& path) { analysisLogPath_ = path; }

    /**
     * @brief 是否在控制台显示 SQL 执行结果
     */
    bool isShowResults() const { return showResults_; }
    void setShowResults(bool enabled) { showResults_ = enabled; }

    /**
     * @brief 是否显示简洁的成功/失败消息
     */
    bool isShowSummary() const { return showSummary_; }
    void setShowSummary(bool enabled) { showSummary_ = enabled; }

    // ========== 系统日志配置 ==========

    /**
     * @brief 系统日志文件路径
     */
    QString getSystemLogPath() const { return systemLogPath_; }
    void setSystemLogPath(const QString& path) { systemLogPath_ = path; }

    /**
     * @brief 是否启用系统日志到控制台
     */
    bool isSystemLogConsoleEnabled() const { return systemLogConsole_; }
    void setSystemLogConsoleEnabled(bool enabled) { systemLogConsole_ = enabled; }

    // ========== 数据库配置 ==========

    /**
     * @brief 缓冲池大小（页数）
     */
    size_t getBufferPoolSize() const { return bufferPoolSize_; }
    void setBufferPoolSize(size_t size) { bufferPoolSize_ = size; }

    /**
     * @brief 默认数据库文件路径
     */
    QString getDefaultDbPath() const { return defaultDbPath_; }
    void setDefaultDbPath(const QString& path) { defaultDbPath_ = path; }

    /**
     * @brief Catalog持久化模式（true=JSON文件，false=数据库内部）
     */
    bool isCatalogUseFile() const { return catalogUseFile_; }
    void setCatalogUseFile(bool useFile) { catalogUseFile_ = useFile; }

    /**
     * @brief Catalog JSON文件路径
     */
    QString getCatalogFilePath() const { return catalogFilePath_; }
    void setCatalogFilePath(const QString& path) { catalogFilePath_ = path; }

    /**
     * @brief WAL日志持久化模式（true=独立文件，false=数据库内部）
     */
    bool isWalUseFile() const { return walUseFile_; }
    void setWalUseFile(bool useFile) { walUseFile_ = useFile; }

    /**
     * @brief WAL日志文件路径
     */
    QString getWalFilePath() const { return walFilePath_; }
    void setWalFilePath(const QString& path) { walFilePath_ = path; }


    // ========== 网络配置 ==========

    /**
     * @brief 是否启用网络服务器
     */
    bool isNetworkEnabled() const { return networkEnabled_; }
    void setNetworkEnabled(bool enabled) { networkEnabled_ = enabled; }

    /**
     * @brief 服务器监听地址
     */
    QString getServerAddress() const { return serverAddress_; }
    void setServerAddress(const QString& address) { serverAddress_ = address; }

    /**
     * @brief 服务器监听端口
     */
    uint16_t getServerPort() const { return serverPort_; }
    void setServerPort(uint16_t port) { serverPort_ = port; }

    /**
     * @brief 最大连接数
     */
    int getMaxConnections() const { return maxConnections_; }
    void setMaxConnections(int max) { maxConnections_ = max; }

    /**
     * @brief 是否启用 SSL/TLS
     */
    bool isSSLEnabled() const { return sslEnabled_; }
    void setSSLEnabled(bool enabled) { sslEnabled_ = enabled; }

    /**
     * @brief SSL 证书文件路径
     */
    QString getSSLCertPath() const { return sslCertPath_; }
    void setSSLCertPath(const QString& path) { sslCertPath_ = path; }

    /**
     * @brief SSL 私钥文件路径
     */
    QString getSSLKeyPath() const { return sslKeyPath_; }
    void setSSLKeyPath(const QString& path) { sslKeyPath_ = path; }

    /**
     * @brief 创建默认配置文件
     * @param configPath 配置文件路径
     * @return 是否成功创建
     */
    static bool createDefaultConfig(const QString& configPath = "qindb.ini");

private:
    Config();
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    void loadDefaults();

    // 配置项
    bool verboseOutput_;           // 是否显示详细的词法/语法分析信息
    bool analysisLogEnabled_;      // 是否将分析信息写入日志文件
    QString analysisLogPath_;      // 分析日志文件路径
    bool showResults_;             // 是否显示查询结果
    bool showSummary_;             // 是否显示简洁的成功/失败消息

    QString systemLogPath_;        // 系统日志文件路径
    bool systemLogConsole_;        // 系统日志是否输出到控制台

    size_t bufferPoolSize_;        // 缓冲池大小
    QString defaultDbPath_;        // 默认数据库文件路径

    bool catalogUseFile_;          // Catalog是否使用独立JSON文件（true=文件，false=数据库内部）
    QString catalogFilePath_;      // Catalog文件路径

    bool walUseFile_;              // WAL是否使用独立文件（true=文件，false=数据库内部）
    QString walFilePath_;          // WAL文件路径


    bool networkEnabled_;          // 是否启用网络服务器
    QString serverAddress_;        // 服务器监听地址
    uint16_t serverPort_;          // 服务器监听端口
    int maxConnections_;           // 最大连接数
    bool sslEnabled_;              // 是否启用 SSL/TLS
    QString sslCertPath_;          // SSL 证书路径
    QString sslKeyPath_;           // SSL 私钥路径

    QString configPath_;           // 配置文件路径
    mutable QMutex mutex_;         // 线程安全
};

} // namespace qindb

#endif // QINDB_CONFIG_H
