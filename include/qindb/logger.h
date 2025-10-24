#ifndef QINDB_LOGGER_H
#define QINDB_LOGGER_H

#include "common.h"
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

namespace qindb {

// 日志级别
enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
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
