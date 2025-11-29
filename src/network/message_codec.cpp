#include "qindb/message_codec.h"
#include "qindb/common.h"
#include <QtCore/QDataStream>
#include <QtCore/QIODevice>
#include <QtCore/QtEndian>

namespace qindb {

// ========== 通用消息编解码 ==========

QByteArray MessageCodec::encodeMessage(MessageType type, const QByteArray& payload) {
    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    // 消息长度 = 消息类型(1字节) + 负载长度
    uint32_t messageLength = 1 + payload.size();

    // 写入消息长度（不包括 length 字段本身）
    stream << messageLength;

    // 写入消息类型
    stream << static_cast<uint8_t>(type);

    // 写入负载数据
    if (!payload.isEmpty()) {
        stream.writeRawData(payload.constData(), payload.size());
    }

    return message;
}

bool MessageCodec::decodeMessage(const QByteArray& data, MessageType& type, QByteArray& payload) {
    if (data.size() < 5) {  // 至少需要 4字节长度 + 1字节类型
        return false;
    }

    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    // 读取消息长度
    uint32_t messageLength;
    stream >> messageLength;

    // 验证长度
    if (data.size() < static_cast<int>(4 + messageLength)) {
        return false;
    }

    // 读取消息类型
    uint8_t typeValue;
    stream >> typeValue;
    type = static_cast<MessageType>(typeValue);

    // 读取负载数据
    int payloadSize = messageLength - 1;
    if (payloadSize > 0) {
        payload.resize(payloadSize);
        stream.readRawData(payload.data(), payloadSize);
    } else {
        payload.clear();
    }

    return true;
}

// ========== AUTH_REQUEST 编解码 ==========

QByteArray MessageCodec::encodeAuthRequest(const AuthRequest& request) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    // 协议版本 (2 bytes)
    stream << request.protocolVersion;

    // 用户名
    encodeString(stream, request.username);

    // 密码
    encodeString(stream, request.password);

    // 数据库名
    encodeString(stream, request.database);

    return encodeMessage(MessageType::AUTH_REQUEST, payload);
}

std::optional<AuthRequest> MessageCodec::decodeAuthRequest(const QByteArray& payload) {
    if (payload.isEmpty()) {
        return std::nullopt;
    }

    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);

    AuthRequest request;

    // 协议版本
    stream >> request.protocolVersion;

    // 用户名
    request.username = decodeString(stream);

    // 密码
    request.password = decodeString(stream);

    // 数据库名
    request.database = decodeString(stream);

    if (stream.status() != QDataStream::Ok) {
        return std::nullopt;
    }

    return request;
}

// ========== AUTH_RESPONSE 编解码 ==========

QByteArray MessageCodec::encodeAuthResponse(const AuthResponse& response) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    // 状态码 (1 byte)
    stream << static_cast<uint8_t>(response.status);

    // 会话 ID (8 bytes)
    stream << response.sessionId;

    // 消息
    encodeString(stream, response.message);

    return encodeMessage(MessageType::AUTH_RESPONSE, payload);
}

std::optional<AuthResponse> MessageCodec::decodeAuthResponse(const QByteArray& payload) {
    if (payload.isEmpty()) {
        return std::nullopt;
    }

    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);

    AuthResponse response;

    // 状态码
    uint8_t status;
    stream >> status;
    response.status = static_cast<AuthStatus>(status);

    // 会话 ID
    stream >> response.sessionId;

    // 消息
    response.message = decodeString(stream);

    if (stream.status() != QDataStream::Ok) {
        return std::nullopt;
    }

    return response;
}

// ========== QUERY_REQUEST 编解码 ==========

QByteArray MessageCodec::encodeQueryRequest(const QueryRequest& request) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    // 会话 ID (8 bytes)
    stream << request.sessionId;

    // SQL 语句
    encodeString(stream, request.sql);

    return encodeMessage(MessageType::QUERY_REQUEST, payload);
}

std::optional<QueryRequest> MessageCodec::decodeQueryRequest(const QByteArray& payload) {
    if (payload.isEmpty()) {
        return std::nullopt;
    }

    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);

    QueryRequest request;

    // 会话 ID
    stream >> request.sessionId;

    // SQL 语句
    request.sql = decodeString(stream);

    if (stream.status() != QDataStream::Ok) {
        return std::nullopt;
    }

    return request;
}

// ========== QUERY_RESPONSE 编解码 ==========

QByteArray MessageCodec::encodeQueryResponse(const QueryResponse& result) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    // 状态码 (1 byte)
    stream << static_cast<uint8_t>(result.status);

    // 结果类型 (1 byte)
    stream << static_cast<uint8_t>(result.resultType);

    // 影响的行数 (8 bytes)
    stream << result.rowsAffected;

    // 列数 (4 bytes)
    stream << static_cast<uint32_t>(result.columns.size());

    // 行数 (4 bytes)
    stream << static_cast<uint32_t>(result.rows.size());

    // 列定义部分
    for (const ColumnInfo& col : result.columns) {
        encodeString(stream, col.name);
        stream << col.type;
    }

    // 行数据部分
    for (const QVector<QVariant>& row : result.rows) {
        for (int i = 0; i < row.size(); i++) {
            const QVariant& value = row[i];

            // 是否为 NULL (1 byte)
            bool isNull = value.isNull();
            stream << static_cast<uint8_t>(isNull ? 1 : 0);

            if (!isNull) {
                // 编码值
                uint8_t colType = (i < result.columns.size()) ? result.columns[i].type : 0;
                encodeVariant(stream, value);
            }
        }
    }

    // 当前数据库名称（用于客户端提示符更新）
    encodeString(stream, result.currentDatabase);

    return encodeMessage(MessageType::QUERY_RESPONSE, payload);
}

std::optional<QueryResponse> MessageCodec::decodeQueryResponse(const QByteArray& payload) {
    if (payload.isEmpty()) {
        return std::nullopt;
    }

    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);

    QueryResponse result;

    // 状态码
    uint8_t status;
    stream >> status;
    result.status = static_cast<QueryStatus>(status);

    // 结果类型
    uint8_t resultType;
    stream >> resultType;
    result.resultType = static_cast<ResultType>(resultType);

    // 影响的行数
    stream >> result.rowsAffected;

    // 列数
    uint32_t columnCount;
    stream >> columnCount;

    // 行数
    uint32_t rowCount;
    stream >> rowCount;

    // 读取列定义
    result.columns.reserve(columnCount);
    for (uint32_t i = 0; i < columnCount; i++) {
        ColumnInfo col;
        col.name = decodeString(stream);
        stream >> col.type;
        result.columns.append(col);
    }

    // 读取行数据
    result.rows.reserve(rowCount);
    for (uint32_t i = 0; i < rowCount; i++) {
        QVector<QVariant> row;
        row.reserve(columnCount);

        for (uint32_t j = 0; j < columnCount; j++) {
            // 是否为 NULL
            uint8_t isNull;
            stream >> isNull;

            if (isNull) {
                row.append(QVariant());
            } else {
                uint8_t colType = result.columns[j].type;
                QVariant value = decodeVariant(stream, colType);
                row.append(value);
            }
        }

        result.rows.append(row);
    }

    // 读取当前数据库名称
    result.currentDatabase = decodeString(stream);

    if (stream.status() != QDataStream::Ok) {
        return std::nullopt;
    }

    return result;
}

// ========== DATABASE_SWITCH 编解码 ==========

QByteArray MessageCodec::encodeDatabaseSwitch(const DatabaseSwitchMessage& message) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    // 数据库名
    encodeString(stream, message.databaseName);

    return encodeMessage(MessageType::DATABASE_SWITCH, payload);
}

std::optional<DatabaseSwitchMessage> MessageCodec::decodeDatabaseSwitch(const QByteArray& payload) {
    if (payload.isEmpty()) {
        return std::nullopt;
    }

    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);

    DatabaseSwitchMessage message;

    // 数据库名
    message.databaseName = decodeString(stream);

    if (stream.status() != QDataStream::Ok) {
        return std::nullopt;
    }

    return message;
}

// ========== ERROR_RESPONSE 编解码 ==========

QByteArray MessageCodec::encodeErrorResponse(const ErrorResponse& error) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    // 错误码 (4 bytes)
    stream << error.errorCode;

    // 错误消息
    encodeString(stream, error.message);

    // 详细信息
    encodeString(stream, error.detail);

    return encodeMessage(MessageType::ERROR_RESPONSE, payload);
}

std::optional<ErrorResponse> MessageCodec::decodeErrorResponse(const QByteArray& payload) {
    if (payload.isEmpty()) {
        return std::nullopt;
    }

    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);

    ErrorResponse error;

    // 错误码
    stream >> error.errorCode;

    // 错误消息
    error.message = decodeString(stream);

    // 详细信息
    error.detail = decodeString(stream);

    if (stream.status() != QDataStream::Ok) {
        return std::nullopt;
    }

    return error;
}

// ========== 辅助方法 ==========

void MessageCodec::encodeString(QDataStream& stream, const QString& str) {
    QByteArray utf8 = str.toUtf8();

    // 写入长度 (4 bytes)
    stream << static_cast<uint32_t>(utf8.size());

    // 写入 UTF-8 数据
    if (!utf8.isEmpty()) {
        stream.writeRawData(utf8.constData(), utf8.size());
    }
}

QString MessageCodec::decodeString(QDataStream& stream) {
    // 读取长度
    uint32_t length;
    stream >> length;

    if (length == 0) {
        return QString();
    }

    // 读取 UTF-8 数据
    QByteArray utf8;
    utf8.resize(length);
    stream.readRawData(utf8.data(), length);

    return QString::fromUtf8(utf8);
}

void MessageCodec::encodeVariant(QDataStream& stream, const QVariant& value) {
    // 简化版本：仅支持基础类型
    // TODO: 支持更多数据类型（DATE, TIME, BLOB 等）

    QByteArray data;
    QDataStream dataStream(&data, QIODevice::WriteOnly);
    dataStream.setByteOrder(QDataStream::BigEndian);

    switch (value.typeId()) {
    case QMetaType::Int:
        dataStream << value.toInt();
        break;
    case QMetaType::LongLong:
        dataStream << value.toLongLong();
        break;
    case QMetaType::Double:
        dataStream << value.toDouble();
        break;
    case QMetaType::QString:
        encodeString(dataStream, value.toString());
        break;
    case QMetaType::Bool:
        dataStream << static_cast<uint8_t>(value.toBool() ? 1 : 0);
        break;
    default:
        // 默认使用 QString 表示
        encodeString(dataStream, value.toString());
        break;
    }

    // 写入数据长度 + 数据
    stream << static_cast<uint32_t>(data.size());
    stream.writeRawData(data.constData(), data.size());
}

QVariant MessageCodec::decodeVariant(QDataStream& stream, uint8_t type) {
    // 读取数据长度
    uint32_t length;
    stream >> length;

    if (length == 0) {
        return QVariant();
    }

    // 读取数据
    QByteArray data;
    data.resize(length);
    stream.readRawData(data.data(), length);

    QDataStream dataStream(data);
    dataStream.setByteOrder(QDataStream::BigEndian);

    // 根据类型解码（简化版本）
    // TODO: 根据 DataType 枚举正确解码
    DataType dt = static_cast<DataType>(type);

    if (dt == DataType::INT || dt == DataType::BIGINT || dt == DataType::SMALLINT) {
        int value;
        dataStream >> value;
        return QVariant(value);
    } else if (dt == DataType::DOUBLE || dt == DataType::FLOAT) {
        double value;
        dataStream >> value;
        return QVariant(value);
    } else if (dt == DataType::BOOL) {
        uint8_t value;
        dataStream >> value;
        return QVariant(value != 0);
    } else {
        // 默认作为字符串
        return QVariant(decodeString(dataStream));
    }
}

} // namespace qindb
