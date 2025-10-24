#include "qindb/logger.h"
#include <iostream>

namespace qindb {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() = default;

Logger::~Logger() {
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
}

void Logger::setLevel(LogLevel level) {
    QMutexLocker locker(&m_mutex);
    m_level = level;
}

void Logger::setLogFile(const QString& filename) {
    QMutexLocker locker(&m_mutex);
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
    m_logFile.setFileName(filename);
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        std::cerr << "Warning: Failed to open log file: " << filename.toStdString() << std::endl;
    }
}

void Logger::enableConsole(bool enable) {
    QMutexLocker locker(&m_mutex);
    m_consoleEnabled = enable;
}

void Logger::debug(const QString& msg) {
    log(LogLevel::DEBUG, msg);
}

void Logger::info(const QString& msg) {
    log(LogLevel::INFO, msg);
}

void Logger::warn(const QString& msg) {
    log(LogLevel::WARN, msg);
}

void Logger::error(const QString& msg) {
    log(LogLevel::ERROR, msg);
}

void Logger::log(LogLevel level, const QString& msg) {
    if (level < m_level) {
        return;
    }
    write(level, msg);
}

QString Logger::levelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void Logger::write(LogLevel level, const QString& msg) {
    QMutexLocker locker(&m_mutex);

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString levelStr = levelToString(level);
    QString logMsg = QString("[%1] [%2] %3").arg(timestamp, levelStr, msg);

    // 输出到控制台
    if (m_consoleEnabled) {
        std::cerr << logMsg.toStdString() << std::endl;
    }

    // 输出到文件
    if (m_logFile.isOpen()) {
        QTextStream stream(&m_logFile);
        stream << logMsg << "\n";
        stream.flush();
    }
}

} // namespace qindb
