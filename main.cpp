#include <QCoreApplication>
#include <QTextStream>
#include <iostream>
#include <string>
#include <QtNetwork/QTcpSocket>
#include <QThread>
#include "qindb/logger.h"
#include "qindb/lexer.h"
#include "qindb/parser.h"
#include "qindb/config.h"
#include "qindb/catalog.h"
#include "qindb/executor.h"
#include "qindb/disk_manager.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/auth_manager.h"
#include "qindb/server.h"
#include "qindb/client_manager.h"
#include "qindb/connection_string_parser.h"
#include <QFile>
#include <QDateTime>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

void setupConsole() {
#ifdef _WIN32
    // 设置控制台代码页为 UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 启用 ANSI 转义序列支持（Windows 10+）
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);

    // 设置 stdout 为 UTF-8 模式
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
    _setmode(_fileno(stdin), _O_U8TEXT);
#endif
}

void printBanner() {
    std::wcout << LR"(
╔═══════════════════════════════════════════════════════════╗
║                      qinDB v1.0.0                         ║
║                      关系型数据库                         ║
╚═══════════════════════════════════════════════════════════╝

欢迎来到qinDB!
输入'help'获取帮助信息,输入'exit'或'quit'退出.
)" << std::endl;
}

void showHelp() {
    std::wcout << LR"(
其它 命令:
  help              - 显示帮助信息
  exit, quit        - 退出qinDB
  clear             - 清屏

数据库管理 命令:
  CREATE DATABASE <name>           - 创建数据库
  DROP DATABASE <name>             - 删除数据库
  USE DATABASE <name>              - 切换数据库
  SHOW DATABASES                   - 列出所有数据库

表操作 命令:
  CREATE TABLE <name> (...)        - 创建数据表
  DROP TABLE <name>                - 删除数据表
  SHOW TABLES                      - 列出所有数据表

索引操作 命令:
  CREATE INDEX <name> ON <table>(<column>)  - 创建索引 (支持60+种数据类型)
  DROP INDEX <name>                         - 删除索引

数据操作 命令:
  SELECT ... FROM ... WHERE ...    - 查询数据 (支持索引优化)
  INSERT INTO ... VALUES (...)     - 插入数据
  UPDATE ... SET ... WHERE ...     - 更新数据
  DELETE FROM ... WHERE ...        - 删除数据

高级查询:
  JOIN                             - 表连接 (支持 INNER/LEFT JOIN)
  GROUP BY ... HAVING ...          - 分组与聚合
  ORDER BY ... ASC/DESC            - 排序
  LIMIT n                          - 限制结果数量

支持的索引类型:
  • 整数类型: INT, BIGINT, SMALLINT, TINYINT, SERIAL 等
  • 浮点类型: FLOAT, DOUBLE, REAL, DECIMAL 等
  • 字符串类型: VARCHAR, CHAR, TEXT, NVARCHAR 等
  • 日期时间: DATE, TIMESTAMP, DATETIME 等
  • 其他类型: BOOLEAN, JSON, UUID 等 (共60+种)

注意:
  - 所有 SQL 语句必须以分号(;)结尾
  - 索引查询自动优化 WHERE 子句中的等值条件
)" << std::endl;
}

void writeAnalysisLog(const QString& sql, const QString& content) {
    using namespace qindb;
    const Config& config = Config::instance();

    if (!config.isAnalysisLogEnabled()) {
        return;
    }

    QFile logFile(config.getAnalysisLogPath());
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);

        // 添加时间戳
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        out << "\n[" << timestamp << "]\n";
        out << "SQL: " << sql << "\n";
        out << content << "\n";

        logFile.close();
    }
}

void analyzeSql(const QString& sql, qindb::Executor* executor) {
    using namespace qindb;
    const Config& config = Config::instance();

    QString analysisLog;
    QTextStream logStream(&analysisLog);

    // 1. 词法分析
    logStream << "═════════════════════════════════════════════════════════\n";
    logStream << "1. 词法分析 (Tokens):\n";
    logStream << "─────────────────────────────────────────────────────────\n";

    Lexer lexer(sql);
    Token token;
    int tokenCount = 0;
    QVector<Token> tokens;

    do {
        token = lexer.nextToken();
        if (token.type != TokenType::EOF_TOKEN) {
            tokens.append(token);
            logStream << "  " << (++tokenCount) << ". "
                      << tokenTypeToString(token.type)
                      << " [" << token.lexeme << "]\n";
        }
    } while (token.type != TokenType::EOF_TOKEN);

    if (tokenCount == 0) {
        logStream << "  (empty)\n";
    }

    // 2. 语法分析
    logStream << "\n2. 语法分析 (AST):\n";
    logStream << "─────────────────────────────────────────────────────────\n";

    Parser parser(sql);
    auto ast = parser.parse();

    bool parseSuccess = false;
    QString errorMsg;
    QString errorDetail;

    if (ast) {
        parseSuccess = true;
        logStream << "✓ 解析成功!\n";
        logStream << "\nAST 结构:\n";
        logStream << "  " << ast->toString() << "\n";
    } else {
        const Error& err = parser.lastError();
        errorMsg = err.message;
        errorDetail = err.detail;

        logStream << "✗ 解析失败!\n";
        logStream << "\n错误: " << errorMsg << "\n";
        if (!errorDetail.isEmpty()) {
            logStream << "详情: " << errorDetail << "\n";
        }
    }

    logStream << "═════════════════════════════════════════════════════════\n";

    // 3. 执行SQL（如果解析成功）
    QueryResult execResult;
    if (parseSuccess && executor) {
        logStream << "\n3. 执行:\n";
        logStream << "─────────────────────────────────────────────────────────\n";

        execResult = executor->execute(ast);

        if (execResult.success) {
            logStream << "✓ 执行成功!\n";
            logStream << "  " << execResult.message << "\n";
        } else {
            logStream << "✗ 执行失败!\n";
            logStream << "  " << execResult.error.message << "\n";
        }
    }

    // 根据配置决定输出
    if (config.isVerboseOutput()) {
        // 详细模式：显示完整的分析信息
        std::wcout << L"\n" << analysisLog.toStdWString();
    } else {
        // 简洁模式：只显示执行结果
        if (config.isShowSummary()) {
            if (!parseSuccess) {
                std::wcout << L"✗ SQL语法错误： " << errorMsg.toStdWString() << L"\n";
                if (!errorDetail.isEmpty()) {
                    std::wcout << L"  " << errorDetail.toStdWString() << L"\n";
                }
            } else if (executor) {
                if (execResult.success) {
                    std::wcout << L"✓ " << execResult.message.toStdWString() << L"\n";

                    // 如果是SELECT，显示结果
                    if (!execResult.rows.isEmpty()) {
                        std::wcout << L"\n";

                        // 计算每列的最大宽度
                        QVector<int> columnWidths;
                        for (int i = 0; i < execResult.columnNames.size(); ++i) {
                            int maxWidth = execResult.columnNames[i].length();
                            for (const auto& row : execResult.rows) {
                                if (i < row.size()) {
                                    int valueWidth = row[i].toString().length();
                                    if (valueWidth > maxWidth) {
                                        maxWidth = valueWidth;
                                    }
                                }
                            }
                            columnWidths.append(maxWidth + 2); // 加2个空格作为间距
                        }

                        // 显示列名
                        for (int i = 0; i < execResult.columnNames.size(); ++i) {
                            QString colName = execResult.columnNames[i];
                            std::wcout << colName.toStdWString();
                            // 填充空格
                            int padding = columnWidths[i] - colName.length();
                            for (int j = 0; j < padding; ++j) {
                                std::wcout << L" ";
                            }
                        }
                        std::wcout << L"\n";

                        // 显示分隔线
                        for (int i = 0; i < execResult.columnNames.size(); ++i) {
                            for (int j = 0; j < columnWidths[i]; ++j) {
                                std::wcout << L"-";
                            }
                        }
                        std::wcout << L"\n";

                        // 显示数据
                        for (const auto& row : execResult.rows) {
                            for (int i = 0; i < row.size(); ++i) {
                                QString value = row[i].toString();
                                std::wcout << value.toStdWString();
                                // 填充空格
                                int padding = columnWidths[i] - value.length();
                                for (int j = 0; j < padding; ++j) {
                                    std::wcout << L" ";
                                }
                            }
                            std::wcout << L"\n";
                        }
                    }
                } else {
                    std::wcout << L"✗ " << execResult.error.message.toStdWString() << L"\n";
                }
            } else {
                // 没有executor，显示原来的消息
                QString sqlType = "UNKNOWN";
                QString lowerSql = sql.trimmed().toLower();
                if (lowerSql.startsWith("select")) {
                    sqlType = "SELECT";
                } else if (lowerSql.startsWith("insert")) {
                    sqlType = "INSERT";
                } else if (lowerSql.startsWith("update")) {
                    sqlType = "UPDATE";
                } else if (lowerSql.startsWith("delete")) {
                    sqlType = "DELETE";
                } else if (lowerSql.startsWith("create table")) {
                    sqlType = "CREATE TABLE";
                } else if (lowerSql.startsWith("drop table")) {
                    sqlType = "DROP TABLE";
                } else if (lowerSql.startsWith("create")) {
                    sqlType = "CREATE";
                } else if (lowerSql.startsWith("drop")) {
                    sqlType = "DROP";
                }

                std::wcout << L"✓ " << sqlType.toStdWString()
                          << L" 语句解析成功.\n";
            }
        }
    }

    // 写入日志文件
    writeAnalysisLog(sql, analysisLog);
}

/**
 * @brief 运行客户端模式（使用连接字符串）
 * @param connectionString 连接字符串
 * @return 客户端程序退出代码
 */
int runClientMode(const QString& connectionString) {
    // 解析连接字符串
    auto paramsOpt = qindb::ConnectionStringParser::parse(connectionString);
    if (!paramsOpt) {
        std::wcout << L"✗ 无效的连接字符串格式\n";
        std::wcout << L"连接字符串格式: qindb://主机:端口?usr=用户名&pswd=密码&ssl=是否启用\n";
        return 1;
    }

    auto params = paramsOpt.value();

    std::wcout << L"正在连接到服务器 " << params.host.toStdWString() 
              << L":" << params.port << L"...\n" << std::endl;

    // 创建客户端管理器
    qindb::ClientManager clientManager;

    // 连接信号槽
    QObject::connect(&clientManager, &qindb::ClientManager::connected, [&]() {
        std::wcout << L"✓ 连接成功\n" << std::endl;
    });

    QObject::connect(&clientManager, &qindb::ClientManager::authenticated, [&]() {
        std::wcout << L"✓ 认证成功\n" << std::endl;
    });

    QObject::connect(&clientManager, &qindb::ClientManager::authenticationFailed, [](const QString& error) {
        std::wcout << L"✗ 认证失败: " << error.toStdWString() << L"\n" << std::endl;
    });

    QObject::connect(&clientManager, &qindb::ClientManager::error, [](const QString& error) {
        std::wcout << L"✗ 错误: " << error.toStdWString() << L"\n" << std::endl;
    });

    QObject::connect(&clientManager, &qindb::ClientManager::queryResponse, [](const qindb::QueryResponse& response) {
        std::wcout << L"收到查询响应\n" << std::endl;
        // 这里可以添加响应处理逻辑
    });

    // 连接到服务器
    if (!clientManager.connectToServer(params)) {
        std::wcout << L"✗ 连接服务器失败\n" << std::endl;
        return 1;
    }

    // 等待认证完成
    while (!clientManager.isAuthenticated()) {
        QCoreApplication::processEvents();
        QThread::msleep(100);
    }

    if (!clientManager.isAuthenticated()) {
        std::wcout << L"✗ 认证失败，无法继续\n" << std::endl;
        return 1;
    }

    // 客户端交互循环
    std::wstring line;
    std::wstring sqlBuffer;

    while (true) {
        // 显示提示符
        if (sqlBuffer.empty()) {
            std::wcout << L"qindb> " << std::flush;
        } else {
            std::wcout << L"    -> " << std::flush;
        }

        // 读取一行
        if (!std::getline(std::wcin, line)) {
            break; // EOF (Ctrl+D / Ctrl+Z)
        }

        // 移除首尾空格
        size_t start = line.find_first_not_of(L" \t\r\n");
        size_t end = line.find_last_not_of(L" \t\r\n");
        if (start == std::wstring::npos) {
            continue; // 空行
        }
        line = line.substr(start, end - start + 1);

        // 添加到缓冲区
        if (!sqlBuffer.empty()) {
            sqlBuffer += L" ";
        }
        sqlBuffer += line;

        // 检查特殊命令
        QString currentInput = QString::fromStdWString(sqlBuffer).trimmed().toLower();
        if (currentInput == "exit" || currentInput == "quit" ||
            currentInput == "help" || currentInput == "clear" || currentInput == "cls") {

            if (currentInput == "exit" || currentInput == "quit") {
                std::wcout << L"再见!" << std::endl;
                break;
            } else if (currentInput == "help") {
                std::wcout << L"支持的命令:\n";
                std::wcout << L"  help              - 显示帮助\n";
                std::wcout << L"  exit, quit        - 退出客户端\n";
                std::wcout << L"  clear, cls        - 清屏\n";
                std::wcout << L"  SQL语句;         - 执行SQL语句（以分号结尾）\n";
            } else if (currentInput == "clear" || currentInput == "cls") {
#ifdef _WIN32
                system("cls");
#else
                system("clear");
#endif
            }

            sqlBuffer.clear();
            continue;
        }

        // 检查是否以分号结尾（SQL语句）
        if (sqlBuffer.back() != L';') {
            continue; // 继续读取多行SQL
        }

        // 转换为 QString
        QString sql = QString::fromStdWString(sqlBuffer);
        sqlBuffer.clear();

        // 移除末尾的分号
        sql = sql.trimmed();
        if (sql.endsWith(';')) {
            sql.chop(1);
            sql = sql.trimmed();
        }

        if (sql.isEmpty()) {
            continue;
        }

        // 发送查询到服务器
        if (clientManager.sendQuery(sql)) {
            std::wcout << L"✓ 查询已发送\n" << std::endl;
        } else {
            std::wcout << L"✗ 发送查询失败\n" << std::endl;
        }
    }

    clientManager.disconnectFromServer();
    std::wcout << L"已断开与服务器的连接\n" << std::endl;
    return 0;
}

void runInteractiveMode(qindb::Executor* executor, qindb::DatabaseManager* dbManager) {
    std::wstring line;
    std::wstring sqlBuffer;

    while (true) {
        // 显示提示符
        if (sqlBuffer.empty()) {
            // 获取当前数据库名
            QString currentDb = dbManager->currentDatabaseName();
            if (currentDb.isEmpty()) {
                std::wcout << L"default> " << std::flush;
            } else {
                std::wcout << currentDb.toStdWString() << L"> " << std::flush;
            }
        } else {
            std::wcout << L"    -> " << std::flush;
        }

        // 读取一行
        if (!std::getline(std::wcin, line)) {
            break; // EOF (Ctrl+D / Ctrl+Z)
        }

        // 移除首尾空格
        size_t start = line.find_first_not_of(L" \t\r\n");
        size_t end = line.find_last_not_of(L" \t\r\n");
        if (start == std::wstring::npos) {
            continue; // 空行
        }
        line = line.substr(start, end - start + 1);

        // 添加到缓冲区
        if (!sqlBuffer.empty()) {
            sqlBuffer += L" ";
        }
        sqlBuffer += line;

        // 检查是否是特殊命令（不需要分号）
        QString currentInput = QString::fromStdWString(sqlBuffer).trimmed().toLower();
        if (currentInput == "exit" || currentInput == "quit" ||
            currentInput == "help" ||
            currentInput == "clear" || currentInput == "cls" ||
            currentInput == "show tables") {

            // 处理特殊命令
            if (currentInput == "exit" || currentInput == "quit") {
                std::wcout << L"再见!" << std::endl;
                break;
            } else if (currentInput == "help") {
                showHelp();
            } else if (currentInput == "clear" || currentInput == "cls") {
#ifdef _WIN32
                system("cls");
#else
                system("clear");
#endif
                printBanner();
            } else if (currentInput == "show tables") {
                // 执行SHOW TABLES
                if (executor) {
                    auto result = executor->executeShowTables();
                    if (result.success) {
                        std::wcout << L"\nTables:\n";
                        for (const auto& row : result.rows) {
                            std::wcout << L"  " << row[0].toString().toStdWString() << L"\n";
                        }
                        std::wcout << L"\n" << result.message.toStdWString() << L"\n";
                    } else {
                        std::wcout << L"✗ " << result.error.message.toStdWString() << L"\n";
                    }
                }
            }

            sqlBuffer.clear();
            continue;
        }

        // 检查是否以分号结尾（SQL语句）
        if (sqlBuffer.back() != L';') {
            continue; // 继续读取多行SQL
        }

        // 转换为 QString
        QString sql = QString::fromStdWString(sqlBuffer);
        sqlBuffer.clear();

        // 移除末尾的分号
        sql = sql.trimmed();
        if (sql.endsWith(';')) {
            sql.chop(1);
            sql = sql.trimmed();
        }

        if (sql.isEmpty()) {
            continue;
        }

        // 分析并执行 SQL
        analyzeSql(sql, executor);
    }
}

int main(int argc, char *argv[])
{
    // 首先设置控制台
    setupConsole();

    try {
        QCoreApplication app(argc, argv);

        // 检查命令行参数
        bool isServerMode = false;
        bool isClientMode = false;
        QString connectionString = "qindb://localhost:24678?usr=admin&pswd=&ssl=false";

        for (int i = 1; i < argc; i++) {
            QString arg = QString::fromUtf8(argv[i]);
            if (arg == "--server" || arg == "-s") {
                isServerMode = true;
            } else if (arg == "--client" || arg == "-c") {
                isClientMode = true;
            } else if (arg.startsWith("--connect=") || arg.startsWith("-c=")) {
                connectionString = arg.mid(arg.indexOf('=') + 1);
            } else if (arg == "--help" || arg == "-?") {
                std::wcout << L"用法: qindb [选项]\n";
                std::wcout << L"  --server, -s                    以服务器模式启动\n";
                std::wcout << L"  --client, -c                    以客户端模式启动\n";
                std::wcout << L"  --connect=<连接字符串>          客户端连接字符串\n";
                std::wcout << L"  --help, -?                      显示帮助信息\n";
                std::wcout << L"\n连接字符串格式:\n";
                std::wcout << L"  qindb://主机:端口?usr=用户名&pswd=密码&ssl=是否启用\n";
                std::wcout << L"  示例: qindb://localhost:24678?usr=admin&pswd=123&ssl=false\n";
                return 0;
            }
        }

        // 如果同时指定了服务器和客户端模式，优先使用服务器模式
        if (isClientMode && isServerMode) {
            isClientMode = false;
        }

        // 如果是客户端模式，启动客户端
        if (isClientMode) {
            return runClientMode(connectionString);
        }

        // 加载配置文件（如果不存在则创建默认配置）
        if (!QFile::exists("qindb.ini")) {
            qindb::Config::createDefaultConfig("qindb.ini");
            std::wcout << L"Created default configuration file: qindb.ini\n" << std::endl;
        }

        qindb::Config& config = qindb::Config::instance();
        config.load("qindb.ini");

        // 初始化日志系统
        qindb::Logger::instance().setLevel(qindb::LogLevel::INFO);
        qindb::Logger::instance().enableConsole(config.isSystemLogConsoleEnabled());
        qindb::Logger::instance().setLogFile(config.getSystemLogPath());

        printBanner();

        if (!config.isVerboseOutput()) {
            std::wcout << L"在简单模式下运行.\n";
            std::wcout << L"编辑qindb.ini，设置VerboseOutput=true来获取详细分析.\n" << std::endl;
        }

        LOG_INFO("qinDB Database System Starting...");
        LOG_INFO(QString("Verbose output: %1").arg(config.isVerboseOutput() ? "enabled" : "disabled"));
        LOG_INFO(QString("Analysis log: %1").arg(config.isAnalysisLogEnabled() ? "enabled" : "disabled"));

        std::wcout << L"系统启动检查中...\n" << std::endl;

        // 检查配置文件是否有效
        try {
            if (!config.load("qindb.ini")) {
                std::wcout << L"✗ 配置文件加载失败\n" << std::endl;
                return 1;
            }
        } catch (const std::exception& e) {
            std::wcout << L"✗ 配置文件加载异常: " << QString::fromUtf8(e.what()).toStdWString() << L"\n" << std::endl;
            return 1;
        } catch (...) {
            std::wcout << L"✗ 配置文件加载发生未知异常\n" << std::endl;
            return 1;
        }

        std::wcout << L"✓ 系统启动检查完成\n" << std::endl;

        // 初始化DatabaseManager
        LOG_INFO("Initializing database manager");
        std::wcout << L"正在初始化数据库管理器...\n" << std::endl;

        qindb::DatabaseManager databaseManager(config.getDefaultDbPath());

        // 添加异常处理
        try {
            // 从磁盘加载数据库管理器状态
            if (!databaseManager.loadFromDisk()) {
                LOG_INFO("Starting with empty database manager");
                std::wcout << L"✓ 使用空数据库管理器启动\n" << std::endl;
            } else {
                LOG_INFO("Loaded existing database manager state");
                std::wcout << L"✓ 已加载现有数据库管理器状态\n" << std::endl;
            }
        } catch (const std::exception& e) {
            LOG_ERROR(QString("Exception during database manager initialization: %1").arg(e.what()));
            std::wcout << L"✗ 数据库管理器初始化异常: " << QString::fromUtf8(e.what()).toStdWString() << L"\n" << std::endl;
            return 1;
        } catch (...) {
            LOG_ERROR("Unknown exception during database manager initialization");
            std::wcout << L"✗ 数据库管理器初始化发生未知异常\n" << std::endl;
            return 1;
        }

        // 初始化Executor
        qindb::Executor executor(&databaseManager);

        try {
            LOG_INFO("Query executor initialized");
            std::wcout << L"✓ 查询执行器初始化成功\n" << std::endl;
        } catch (const std::exception& e) {
            LOG_ERROR(QString("Exception during executor initialization: %1").arg(e.what()));
            std::wcout << L"✗ 查询执行器初始化异常: " << QString::fromUtf8(e.what()).toStdWString() << L"\n" << std::endl;
            return 1;
        } catch (...) {
            LOG_ERROR("Unknown exception during executor initialization");
            std::wcout << L"✗ 查询执行器初始化发生未知异常\n" << std::endl;
            return 1;
        }

        // 初始化认证管理器（使用系统数据库 qindb）
        LOG_INFO("Initializing authentication system...");

        // 保存当前数据库名
        QString previousDatabase = databaseManager.currentDatabaseName();

        // 确保系统数据库 qindb 存在且有效
    bool needCreateSystemDB = false;
    if (!databaseManager.databaseExists("qindb")) {
        LOG_INFO("System database 'qindb' does not exist, will create a new one");
        needCreateSystemDB = true;
    } else {
        LOG_INFO("System database 'qindb' exists, checking integrity...");

        // 尝试切换到系统数据库并检查组件
        if (!databaseManager.useDatabase("qindb")) {
            LOG_WARN("Failed to switch to existing system database, will recreate it");
            needCreateSystemDB = true;
        } else {
            // 检查系统数据库组件是否完整
            qindb::Catalog* testCatalog = databaseManager.getCurrentCatalog();
            qindb::BufferPoolManager* testBufferPool = databaseManager.getCurrentBufferPool();
            qindb::DiskManager* testDiskManager = databaseManager.getCurrentDiskManager();

            if (!testCatalog || !testBufferPool || !testDiskManager) {
                LOG_WARN("System database components are incomplete, will recreate it");
                needCreateSystemDB = true;
            }
        }
    }

    if (needCreateSystemDB) {
        // 如果需要创建新系统数据库，先删除旧的（如果存在）
        if (databaseManager.databaseExists("qindb")) {
            LOG_INFO("Removing corrupted system database 'qindb'");
            if (!databaseManager.dropDatabase("qindb")) {
                LOG_ERROR("Failed to remove corrupted system database");
                std::wcout << L"✗ 无法删除损坏的系统数据库\n" << std::endl;
                return 1;
            }
        }

        // 创建新的系统数据库
        LOG_INFO("Creating new system database 'qindb'");
        if (!databaseManager.createDatabase("qindb")) {
            LOG_ERROR("Failed to create system database 'qindb'");
            std::wcout << L"✗ 创建系统数据库失败。请检查磁盘空间和权限。\n" << std::endl;
            return 1;
        }
        LOG_INFO("System database 'qindb' created successfully");
    } else {
        LOG_INFO("System database 'qindb' is valid and ready to use");
    }

    // 确保切换到系统数据库
    if (!databaseManager.useDatabase("qindb")) {
        LOG_ERROR("Failed to switch to system database 'qindb'");
        std::wcout << L"✗ 切换到系统数据库失败。数据库可能已损坏。\n" << std::endl;
        return 1;
    }
    LOG_INFO("Switched to system database 'qindb' successfully");

        // 获取系统数据库的组件（当前数据库）
        qindb::Catalog* systemCatalog = databaseManager.getCurrentCatalog();
        qindb::BufferPoolManager* systemBufferPool = databaseManager.getCurrentBufferPool();
        qindb::DiskManager* systemDiskManager = databaseManager.getCurrentDiskManager();

        if (!systemCatalog || !systemBufferPool || !systemDiskManager) {
            LOG_ERROR("Failed to get system database components");
            std::wcout << L"✗ 无法获取系统数据库组件\n" << std::endl;
            return 1;
        }

        // 创建认证管理器
        qindb::AuthManager* authManager = nullptr;
        try {
            authManager = new qindb::AuthManager(systemCatalog, systemBufferPool, systemDiskManager);

            // 初始化用户系统表
            if (!authManager->initializeUserSystem()) {
                LOG_ERROR("Failed to initialize user authentication system");
                std::wcout << L"✗ 用户认证系统初始化失败\n" << std::endl;
                delete authManager;
                return 1;
            }

            LOG_INFO("Authentication system initialized successfully");
            std::wcout << L"✓ 用户认证系统初始化成功\n" << std::endl;

            // 将AuthManager设置到Executor
            executor.setAuthManager(authManager);
            executor.setPermissionManager(databaseManager.getCurrentPermissionManager());
            executor.setCurrentUser("admin");
            LOG_INFO("AuthManager linked to executor");
            std::wcout << L"✓ 认证管理器已连接到执行器\n" << std::endl;
        } catch (const std::exception& e) {
            LOG_ERROR(QString("Exception during auth manager initialization: %1").arg(e.what()));
            std::wcout << L"✗ 认证管理器初始化异常: " << QString::fromUtf8(e.what()).toStdWString() << L"\n" << std::endl;
            delete authManager;
            return 1;
        } catch (...) {
            LOG_ERROR("Unknown exception during auth manager initialization");
            std::wcout << L"✗ 认证管理器初始化发生未知异常\n" << std::endl;
            delete authManager;
            return 1;
        }

        // 切换回之前的数据库
        if (!previousDatabase.isEmpty() && previousDatabase != "qindb") {
            if (databaseManager.databaseExists(previousDatabase)) {
                databaseManager.useDatabase(previousDatabase);
            }
        }

        // 启动网络服务器（如果在配置中启用或指定了服务器模式）
        qindb::Server* server = nullptr;
        bool shouldStartServer = config.isNetworkEnabled() || isServerMode;

        if (shouldStartServer) {
            LOG_INFO("Network server enabled in configuration or command line");
            std::wcout << L"✓ 网络服务器模式已启用\n" << std::endl;

            server = new qindb::Server(&databaseManager, authManager);

            QString address = config.getServerAddress();
            uint16_t port = config.getServerPort();

            if (server->start(address, port)) {
                LOG_INFO(QString("Network server started on %1:%2").arg(address).arg(port));
                std::wcout << L"✓ 网络服务器启动成功: " << address.toStdWString()
                          << L":" << port << L"\n" << std::endl;
            } else {
                LOG_ERROR("Failed to start network server");
                std::wcout << L"✗ 网络服务器启动失败\n" << std::endl;
                delete server;
                server = nullptr;
            }
        } else {
            LOG_INFO("Network server is disabled");
            std::wcout << L"提示: 网络服务器未启用。使用 --server 参数或在 qindb.ini 中设置 Network/Enabled=true 来启用。\n" << std::endl;
        }

        // 确定运行模式
        bool useInteractiveMode = true;  // 默认使用交互模式

        // 如果指定了服务器模式且没有客户端连接，则使用Qt事件循环
        if (isServerMode && !server) {
            useInteractiveMode = false;  // 无法启动服务器，退出
        }

        if (server && !useInteractiveMode) {
            // 使用Qt事件循环保持服务器运行
            LOG_INFO("Running in server-only mode with Qt event loop");
            std::wcout << L"\n服务器正在运行...\n按 Ctrl+C 退出.\n" << std::endl;

            int exitCode = app.exec();

            // 清理网络服务器
            LOG_INFO("Stopping network server");
            server->stop();
            delete server;

            return exitCode;
        } else {
            // 进入交互式模式(同时网络服务器在后台运行)
            LOG_INFO("Entering interactive mode");
            runInteractiveMode(&executor, &databaseManager);

            // 清理网络服务器
            if (server) {
                LOG_INFO("Stopping network server");
                server->stop();
                delete server;
            }
        }

        // 保存数据库管理器状态
        if (!databaseManager.saveToDisk()) {
            LOG_ERROR("Failed to save database manager state");
        } else {
            LOG_INFO("Database manager state saved successfully");
        }

        LOG_INFO("qinDB Database System Shutting down");

        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR(QString("Critical exception in main: %1").arg(e.what()));
        std::wcout << L"✗ 程序运行时发生严重异常: " << QString::fromUtf8(e.what()).toStdWString() << L"\n" << std::endl;
        return 1;
    } catch (...) {
        LOG_ERROR("Unknown critical exception in main");
        std::wcout << L"✗ 程序运行时发生未知严重异常\n" << std::endl;
        return 1;
    }
}
