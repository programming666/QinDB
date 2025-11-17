#ifndef QINDB_LOGGER_H  // 防止重复包含该头文件
#define QINDB_LOGGER_H

#include "common.h"  // 包含项目公共头文件
#include <QMutex>    // 包含Qt互斥锁类，用于线程同步
#include <QFile>     // 包含Qt文件操作类，用于日志文件写入
#include <QTextStream>  // 包含Qt文本流类，用于文本格式化输出
#include <QDateTime>  // 包含Qt日期时间类，用于获取时间戳

namespace qindb {  // 定义qindb命名空间

// 日志级别枚举类，定义了四种日志级别
enum class LogLevel {
    DEBUG = 0,  // 调试级别，最低级别
    INFO = 1,   // 信息级别
    WARN = 2,   // 警告级别
    ERROR = 3   // 错误级别，最高级别
};

// 日志系统 (单例)
class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel level);
    void setLogFile(const QString& filename);
    void enableConsole(bool enable);

    void debug(const QString& msg);
    void info(const QString& msg);
    void warn(const QString& msg);
    void error(const QString& msg);

    void log(LogLevel level, const QString& msg);

private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    QString levelToString(LogLevel level) const;
    void write(LogLevel level, const QString& msg);

    LogLevel m_level = LogLevel::INFO;
    bool m_consoleEnabled = true;
    QFile m_logFile;
    QMutex m_mutex;
};

// 便捷宏
#define LOG_DEBUG(msg) qindb::Logger::instance().debug(msg)
#define LOG_INFO(msg) qindb::Logger::instance().info(msg)
#define LOG_WARN(msg) qindb::Logger::instance().warn(msg)
#define LOG_ERROR(msg) qindb::Logger::instance().error(msg)

} // namespace qindb

#endif // QINDB_LOGGER_H
