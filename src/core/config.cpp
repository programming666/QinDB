/**
 * @file config.cpp
 * @brief 配置管理类的实现，负责加载、保存和管理系统配置
 */
#include "qindb/config.h"
#include "qindb/logger.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>

namespace qindb {

/**
 * @brief 获取Config类的单例实例
 * @return Config类的引用
 */
Config& Config::instance() {
    static Config instance;
    return instance;
}

/**
 * @brief Config类的构造函数，加载默认配置
 */
Config::Config() {
    loadDefaults();
}

void Config::loadDefaults() {
    // 日志配置默认值
    verboseOutput_ = false;           // 默认不显示详细分析信息
    analysisLogEnabled_ = true;       // 默认启用分析日志
    analysisLogPath_ = "qindb_analysis.log";
    showResults_ = true;              // 默认显示查询结果
    showSummary_ = true;              // 默认显示简洁消息
    slowQueryEnabled_ = false;
    slowQueryThresholdMs_ = 500;
    slowQueryLogPath_ = "qindb_slow.log";

    // 系统日志配置
    systemLogPath_ = "qindb.log";
    systemLogConsole_ = false;        // 默认系统日志不输出到控制台

    // 数据库配置
    bufferPoolSize_ = 1024;           // 默认缓冲池 1024 页 (8MB)
    defaultDbPath_ = "qindb.db";

    // 持久化配置
    catalogUseFile_ = true;           // 默认使用JSON文件存储元数据
    catalogFilePath_ = "catalog.json";

    walUseFile_ = true;               // 默认使用独立文件存储WAL日志
    walFilePath_ = "qindb.wal";

    // 网络配置
    networkEnabled_ = false;          // 默认不启用网络服务器
    serverAddress_ = "0.0.0.0";       // 监听所有网卡
    serverPort_ = 24678;               // 默认端口
    maxConnections_ = 1000;           // 最大 1000 个并发连接
    sslEnabled_ = false;              // 默认不启用 SSL
    sslCertPath_ = "server.crt";
    sslKeyPath_ = "server.key";

}

bool Config::load(const QString& configPath) {
    QMutexLocker locker(&mutex_);

    configPath_ = configPath;

    QFileInfo fileInfo(configPath);
    if (!fileInfo.exists()) {
        LOG_WARN(QString("Config file not found: %1, using defaults").arg(configPath));
        return false;
    }

    QSettings settings(configPath, QSettings::IniFormat);

    // 读取日志配置
    verboseOutput_ = settings.value("Output/VerboseOutput", verboseOutput_).toBool();
    analysisLogEnabled_ = settings.value("Output/AnalysisLogEnabled", analysisLogEnabled_).toBool();
    analysisLogPath_ = settings.value("Output/AnalysisLogPath", analysisLogPath_).toString();
    showResults_ = settings.value("Output/ShowResults", showResults_).toBool();
    showSummary_ = settings.value("Output/ShowSummary", showSummary_).toBool();
    slowQueryEnabled_ = settings.value("Output/SlowQueryEnabled", slowQueryEnabled_).toBool();
    slowQueryThresholdMs_ = settings.value("Output/SlowQueryThresholdMs", slowQueryThresholdMs_).toInt();
    slowQueryLogPath_ = settings.value("Output/SlowQueryLogPath", slowQueryLogPath_).toString();

    // 读取系统日志配置
    systemLogPath_ = settings.value("SystemLog/LogPath", systemLogPath_).toString();
    systemLogConsole_ = settings.value("SystemLog/ConsoleOutput", systemLogConsole_).toBool();

    // 读取数据库配置
    bufferPoolSize_ = settings.value("Database/BufferPoolSize", static_cast<qulonglong>(bufferPoolSize_)).toULongLong();
    defaultDbPath_ = settings.value("Database/DefaultDbPath", defaultDbPath_).toString();

    // 读取持久化配置
    catalogUseFile_ = settings.value("Persistence/CatalogUseFile", catalogUseFile_).toBool();
    catalogFilePath_ = settings.value("Persistence/CatalogFilePath", catalogFilePath_).toString();
    walUseFile_ = settings.value("Persistence/WalUseFile", walUseFile_).toBool();
    walFilePath_ = settings.value("Persistence/WalFilePath", walFilePath_).toString();

    // 读取网络配置
    networkEnabled_ = settings.value("Network/Enabled", networkEnabled_).toBool();
    serverAddress_ = settings.value("Network/Address", serverAddress_).toString();
    serverPort_ = settings.value("Network/Port", serverPort_).toUInt();
    maxConnections_ = settings.value("Network/MaxConnections", maxConnections_).toInt();
    sslEnabled_ = settings.value("Network/SSLEnabled", sslEnabled_).toBool();
    sslCertPath_ = settings.value("Network/SSLCertPath", sslCertPath_).toString();
    sslKeyPath_ = settings.value("Network/SSLKeyPath", sslKeyPath_).toString();


    LOG_INFO(QString("Configuration loaded from: %1").arg(configPath));
    LOG_INFO(QString("  VerboseOutput: %1").arg(verboseOutput_ ? "true" : "false"));
    LOG_INFO(QString("  AnalysisLogEnabled: %1").arg(analysisLogEnabled_ ? "true" : "false"));
    LOG_INFO(QString("  AnalysisLogPath: %1").arg(analysisLogPath_));


    return true;
}

bool Config::save() {
    QMutexLocker locker(&mutex_);

    if (configPath_.isEmpty()) {
        configPath_ = "qindb.ini";
    }

    QSettings settings(configPath_, QSettings::IniFormat);

    // 保存日志配置
    settings.setValue("Output/VerboseOutput", verboseOutput_);
    settings.setValue("Output/AnalysisLogEnabled", analysisLogEnabled_);
    settings.setValue("Output/AnalysisLogPath", analysisLogPath_);
    settings.setValue("Output/ShowResults", showResults_);
    settings.setValue("Output/ShowSummary", showSummary_);
    settings.setValue("Output/SlowQueryEnabled", slowQueryEnabled_);
    settings.setValue("Output/SlowQueryThresholdMs", slowQueryThresholdMs_);
    settings.setValue("Output/SlowQueryLogPath", slowQueryLogPath_);

    // 保存系统日志配置
    settings.setValue("SystemLog/LogPath", systemLogPath_);
    settings.setValue("SystemLog/ConsoleOutput", systemLogConsole_);

    // 保存数据库配置
    settings.setValue("Database/BufferPoolSize", static_cast<qulonglong>(bufferPoolSize_));
    settings.setValue("Database/DefaultDbPath", defaultDbPath_);

    // 保存持久化配置
    settings.setValue("Persistence/CatalogUseFile", catalogUseFile_);
    settings.setValue("Persistence/CatalogFilePath", catalogFilePath_);
    settings.setValue("Persistence/WalUseFile", walUseFile_);
    settings.setValue("Persistence/WalFilePath", walFilePath_);

    // 保存网络配置
    settings.setValue("Network/Enabled", networkEnabled_);
    settings.setValue("Network/Address", serverAddress_);
    settings.setValue("Network/Port", serverPort_);
    settings.setValue("Network/MaxConnections", maxConnections_);
    settings.setValue("Network/SSLEnabled", sslEnabled_);
    settings.setValue("Network/SSLCertPath", sslCertPath_);
    settings.setValue("Network/SSLKeyPath", sslKeyPath_);


    settings.sync();

    LOG_INFO(QString("Configuration saved to: %1").arg(configPath_));

    return settings.status() == QSettings::NoError;
}

bool Config::createDefaultConfig(const QString& configPath) {
    QSettings settings(configPath, QSettings::IniFormat);

    // 输出配置
    settings.setValue("Output/VerboseOutput", false);
    settings.setValue("Output/AnalysisLogEnabled", true);
    settings.setValue("Output/AnalysisLogPath", "qindb_analysis.log");
    settings.setValue("Output/ShowResults", true);
    settings.setValue("Output/ShowSummary", true);
    settings.setValue("Output/SlowQueryEnabled", false);
    settings.setValue("Output/SlowQueryThresholdMs", 500);
    settings.setValue("Output/SlowQueryLogPath", "qindb_slow.log");

    // 系统日志配置
    settings.setValue("SystemLog/LogPath", "qindb.log");
    settings.setValue("SystemLog/ConsoleOutput", false);

    // 数据库配置
    settings.setValue("Database/BufferPoolSize", 1024);
    settings.setValue("Database/DefaultDbPath", "qindb.db");

    // 持久化配置
    settings.setValue("Persistence/CatalogUseFile", true);
    settings.setValue("Persistence/CatalogFilePath", "catalog.json");
    settings.setValue("Persistence/WalUseFile", true);
    settings.setValue("Persistence/WalFilePath", "qindb.wal");
    // 网络配置
    settings.setValue("Network/Enabled", false);
    settings.setValue("Network/Address", "0.0.0.0");
    settings.setValue("Network/Port", 24678);
    settings.setValue("Network/MaxConnections", 1000);
    settings.setValue("Network/SSLEnabled", false);
    settings.setValue("Network/SSLCertPath", "server.crt");
    settings.setValue("Network/SSLKeyPath", "server.key");


    // 添加注释（通过 QSettings 不支持注释，所以我们直接写文件）
    settings.sync();

    // 在文件开头添加注释
    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = file.readAll();
        file.close();

        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "; qinDB Configuration File\n";
            out << "; \n";
            out << "; This file controls the behavior of qinDB database system.\n";
            out << "; \n";
            out << "; [Output] section controls what is displayed to users\n";
            out << ";   VerboseOutput        - Show detailed lexical and syntactic analysis (true/false)\n";
            out << ";   AnalysisLogEnabled   - Write analysis details to log file (true/false)\n";
            out << ";   AnalysisLogPath      - Path to analysis log file\n";
            out << ";   ShowResults          - Show query results in console (true/false)\n";
            out << ";   ShowSummary          - Show brief success/failure messages (true/false)\n";
            out << "; \n";
            out << "; [SystemLog] section controls system logging\n";
            out << ";   LogPath              - Path to system log file\n";
            out << ";   ConsoleOutput        - Output system logs to console (true/false)\n";
            out << "; \n";
            out << "; [Database] section controls database engine parameters\n";
            out << ";   BufferPoolSize       - Number of pages in buffer pool (default: 1024 = 8MB)\n";
            out << ";   DefaultDbPath        - Default database file path\n";
            out << "; \n";
            out << "; [Persistence] section controls metadata and log persistence\n";
            out << ";   CatalogUseFile       - Store catalog metadata in JSON file (true) or database (false)\n";
            out << ";   CatalogFilePath      - Path to catalog JSON file (when CatalogUseFile=true)\n";
            out << ";   WalUseFile           - Store WAL logs in separate file (true) or database (false)\n";
            out << ";   WalFilePath          - Path to WAL log file (when WalUseFile=true)\n";
            out << "; \n\n";
            out << content;
            file.close();
            return true;
        }
    }

    return settings.status() == QSettings::NoError;
}

} // namespace qindb
