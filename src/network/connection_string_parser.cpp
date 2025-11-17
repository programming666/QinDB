#include "qindb/connection_string_parser.h"

namespace qindb {

const uint16_t DEFAULT_DB_PORT = 24678;

std::optional<ConnectionParams> ConnectionStringParser::parse(const QString& connectionString) {
    ConnectionParams params;

    // 检查是否以 qindb:// 开头
    if (!connectionString.startsWith("qindb://")) {
        return std::nullopt;
    }

    // 移除 qindb:// 前缀
    QString urlPart = connectionString.mid(8); // "qindb://" 的长度是 8

    // 检查是否有主机部分
    if (urlPart.isEmpty()) {
        return std::nullopt;  // 没有主机名
    }

    // 分离主机:端口部分和查询参数部分
    int queryIndex = urlPart.indexOf('?');
    QString hostPortPart;
    QString queryPart;

    if (queryIndex != -1) {
        hostPortPart = urlPart.left(queryIndex);
        queryPart = urlPart.mid(queryIndex + 1);
    } else {
        hostPortPart = urlPart;
    }

    // 检查主机部分是否为空
    if (hostPortPart.isEmpty()) {
        return std::nullopt;  // 没有主机名
    }

    // 解析主机和端口
    int portIndex = hostPortPart.indexOf(':');
    if (portIndex != -1) {
        params.host = hostPortPart.left(portIndex);
        bool portOk = false;
        uint16_t port = hostPortPart.mid(portIndex + 1).toUShort(&portOk);
        if (!portOk || port == 0) {
            return std::nullopt; // 端口号无效
        }
        params.port = port;
    } else {
        params.host = hostPortPart;
        params.port = DEFAULT_DB_PORT; // 使用默认端口
    }

    // 解析查询参数
    if (!queryPart.isEmpty()) {
        parseQueryParams(queryPart, params);
    }

    return params;
}

bool ConnectionStringParser::isValid(const QString& connectionString) {
    return parse(connectionString).has_value();
}

void ConnectionStringParser::parseQueryParams(const QString& query, ConnectionParams& params) {
    QRegularExpression paramRegex("([^&=]+)=([^&]*)");
    QRegularExpressionMatchIterator it = paramRegex.globalMatch(query);
    
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString key = match.captured(1);
        QString value = match.captured(2);
        
        // 支持 usr 和 user 两种参数名
        if (key == "usr" || key == "user") {
            params.username = value;
        } else if (key == "pswd") {
            params.password = value;
        } else if (key == "ssl") {
            params.sslEnabled = parseSslValue(value);
        }
    }
}

bool ConnectionStringParser::parseSslValue(const QString& value) {
    QString lowerValue = value.toLower();
    
    // 真值：true, True, TRUE, 1, yes, Yes, YES, on, On, ON
    if (lowerValue == "true" || lowerValue == "1" || 
        lowerValue == "yes" || lowerValue == "on") {
        return true;
    }
    
    // 假值：false, False, FALSE, 0, no, No, NO, off, Off, OFF, invalid
    return false;
}

} // namespace qindb
