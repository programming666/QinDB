#include "qindb/type_serializer.h"
#include "qindb/logger.h"
#include <QDateTime>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>

namespace qindb {

// ============ 公共接口实现 ============

bool TypeSerializer::serialize(const QVariant& value, DataType type, QByteArray& output) {
    output.clear();
    QDataStream stream(&output, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    return serializeToStream(value, type, stream);
}

bool TypeSerializer::deserialize(const QByteArray& data, DataType type, QVariant& value) {
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    return deserializeFromStream(stream, type, value);
}

uint32_t TypeSerializer::getSerializedSize(const QVariant& value, DataType type) {
    QByteArray dummy;
    if (serialize(value, type, dummy)) {
        return static_cast<uint32_t>(dummy.size());
    }
    return 0;
}

bool TypeSerializer::isFixedLengthType(DataType type) {
    return getFixedTypeSize(type) > 0;
}

bool TypeSerializer::serializeToStream(const QVariant& value, DataType type, QDataStream& stream) {
    // NULL 值处理
    if (value.isNull()) {
        stream << static_cast<uint8_t>(1); // NULL 标志
        return true;
    }
    stream << static_cast<uint8_t>(0); // 非 NULL 标志

    // 根据类型分发
    if (isIntegerType(type)) {
        return serializeInteger(value, type, stream);
    } else if (isFloatType(type)) {
        return serializeFloat(value, type, stream);
    } else if (type == DataType::DECIMAL || type == DataType::NUMERIC) {
        return serializeDecimal(value, stream);
    } else if (isStringType(type)) {
        return serializeString(value, type, stream);
    } else if (isBinaryType(type)) {
        return serializeBinary(value, type, stream);
    } else if (isDateTimeType(type)) {
        return serializeDateTime(value, type, stream);
    } else if (type == DataType::BOOLEAN || type == DataType::BOOL) {
        return serializeBoolean(value, stream);
    } else if (type == DataType::JSON || type == DataType::JSONB || type == DataType::XML) {
        return serializeJsonXml(value, type, stream);
    } else if (type == DataType::UUID || type == DataType::UNIQUEIDENTIFIER ||
               type == DataType::GEOMETRY || type == DataType::GEOGRAPHY ||
               type == DataType::HIERARCHYID || type == DataType::ROWID) {
        return serializeSpecial(value, type, stream);
    }

    LOG_ERROR(QString("Unsupported data type for serialization: %1").arg(static_cast<int>(type)));
    return false;
}

bool TypeSerializer::deserializeFromStream(QDataStream& stream, DataType type, QVariant& value) {
    // 读取 NULL 标志
    uint8_t isNull;
    stream >> isNull;
    if (isNull) {
        value = QVariant();
        return true;
    }

    // 根据类型分发
    if (isIntegerType(type)) {
        return deserializeInteger(stream, type, value);
    } else if (isFloatType(type)) {
        return deserializeFloat(stream, type, value);
    } else if (type == DataType::DECIMAL || type == DataType::NUMERIC) {
        return deserializeDecimal(stream, value);
    } else if (isStringType(type)) {
        return deserializeString(stream, type, value);
    } else if (isBinaryType(type)) {
        return deserializeBinary(stream, type, value);
    } else if (isDateTimeType(type)) {
        return deserializeDateTime(stream, type, value);
    } else if (type == DataType::BOOLEAN || type == DataType::BOOL) {
        return deserializeBoolean(stream, value);
    } else if (type == DataType::JSON || type == DataType::JSONB || type == DataType::XML) {
        return deserializeJsonXml(stream, type, value);
    } else if (type == DataType::UUID || type == DataType::UNIQUEIDENTIFIER ||
               type == DataType::GEOMETRY || type == DataType::GEOGRAPHY ||
               type == DataType::HIERARCHYID || type == DataType::ROWID) {
        return deserializeSpecial(stream, type, value);
    }

    LOG_ERROR(QString("Unsupported data type for deserialization: %1").arg(static_cast<int>(type)));
    return false;
}

// ============ 整数类型序列化 ============

bool TypeSerializer::serializeInteger(const QVariant& value, DataType type, QDataStream& stream) {
    bool ok = false;

    switch (type) {
        case DataType::TINYINT: {
            int8_t v = static_cast<int8_t>(value.toInt(&ok));
            if (ok) stream.writeRawData(reinterpret_cast<const char*>(&v), 1);
            break;
        }
        case DataType::SMALLINT: {
            int16_t v = static_cast<int16_t>(value.toInt(&ok));
            if (ok) stream << v;
            break;
        }
        case DataType::MEDIUMINT: {
            int32_t v = value.toInt(&ok);
            if (ok) {
                // MEDIUMINT 使用 3 字节存储
                uint8_t bytes[3];
                bytes[0] = static_cast<uint8_t>(v & 0xFF);
                bytes[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
                bytes[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
                stream.writeRawData(reinterpret_cast<const char*>(bytes), 3);
            }
            break;
        }
        case DataType::INT:
        case DataType::INTEGER:
        case DataType::SERIAL: {
            int32_t v = value.toInt(&ok);
            if (ok) stream << v;
            break;
        }
        case DataType::BIGINT:
        case DataType::BIGSERIAL: {
            int64_t v = value.toLongLong(&ok);
            if (ok) stream << v;
            break;
        }
        default:
            LOG_ERROR(QString("Invalid integer type: %1").arg(static_cast<int>(type)));
            return false;
    }

    return ok;
}

bool TypeSerializer::deserializeInteger(QDataStream& stream, DataType type, QVariant& value) {
    switch (type) {
        case DataType::TINYINT: {
            int8_t v;
            if (stream.readRawData(reinterpret_cast<char*>(&v), 1) == 1) {
                value = static_cast<int>(v);
                return true;
            }
            break;
        }
        case DataType::SMALLINT: {
            int16_t v;
            stream >> v;
            if (stream.status() == QDataStream::Ok) {
                value = static_cast<int>(v);
                return true;
            }
            break;
        }
        case DataType::MEDIUMINT: {
            uint8_t bytes[3];
            if (stream.readRawData(reinterpret_cast<char*>(bytes), 3) == 3) {
                int32_t v = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16);
                // 处理符号扩展（如果最高位为1，则是负数）
                if (bytes[2] & 0x80) {
                    v |= 0xFF000000;
                }
                value = v;
                return true;
            }
            break;
        }
        case DataType::INT:
        case DataType::INTEGER:
        case DataType::SERIAL: {
            int32_t v;
            stream >> v;
            if (stream.status() == QDataStream::Ok) {
                value = v;
                return true;
            }
            break;
        }
        case DataType::BIGINT:
        case DataType::BIGSERIAL: {
            int64_t v;
            stream >> v;
            if (stream.status() == QDataStream::Ok) {
                value = v;
                return true;
            }
            break;
        }
        default:
            LOG_ERROR(QString("Invalid integer type: %1").arg(static_cast<int>(type)));
            return false;
    }

    return false;
}

// ============ 浮点类型序列化 ============

bool TypeSerializer::serializeFloat(const QVariant& value, DataType type, QDataStream& stream) {
    bool ok = false;

    switch (type) {
        case DataType::FLOAT:
        case DataType::REAL:
        case DataType::BINARY_FLOAT: {
            float v = value.toFloat(&ok);
            if (ok) stream << v;
            break;
        }
        case DataType::DOUBLE:
        case DataType::DOUBLE_PRECISION:
        case DataType::BINARY_DOUBLE: {
            double v = value.toDouble(&ok);
            if (ok) stream << v;
            break;
        }
        default:
            LOG_ERROR(QString("Invalid float type: %1").arg(static_cast<int>(type)));
            return false;
    }

    return ok;
}

bool TypeSerializer::deserializeFloat(QDataStream& stream, DataType type, QVariant& value) {
    switch (type) {
        case DataType::FLOAT:
        case DataType::REAL:
        case DataType::BINARY_FLOAT: {
            float v;
            stream >> v;
            if (stream.status() == QDataStream::Ok) {
                value = v;
                return true;
            }
            break;
        }
        case DataType::DOUBLE:
        case DataType::DOUBLE_PRECISION:
        case DataType::BINARY_DOUBLE: {
            double v;
            stream >> v;
            if (stream.status() == QDataStream::Ok) {
                value = v;
                return true;
            }
            break;
        }
        default:
            LOG_ERROR(QString("Invalid float type: %1").arg(static_cast<int>(type)));
            return false;
    }

    return false;
}

// ============ 定点数类型序列化 ============

bool TypeSerializer::serializeDecimal(const QVariant& value, QDataStream& stream) {
    // DECIMAL 存储为字符串以保持精度
    QString decimalStr = value.toString();
    QByteArray encoded = encodeDecimal(decimalStr);

    // 写入长度（2字节）+ 数据
    uint16_t len = static_cast<uint16_t>(encoded.size());
    stream << len;
    stream.writeRawData(encoded.constData(), len);

    return stream.status() == QDataStream::Ok;
}

bool TypeSerializer::deserializeDecimal(QDataStream& stream, QVariant& value) {
    uint16_t len;
    stream >> len;

    if (stream.status() != QDataStream::Ok || len > 1024) { // 限制最大长度
        return false;
    }

    QByteArray encoded(len, '\0');
    if (stream.readRawData(encoded.data(), len) != len) {
        return false;
    }

    QString decimalStr = decodeDecimal(encoded);
    value = decimalStr;
    return true;
}

QByteArray TypeSerializer::encodeDecimal(const QString& decimalStr) {
    // 简化实现：直接使用 UTF-8 编码
    // 生产环境应使用 BCD (Binary Coded Decimal) 编码
    return decimalStr.toUtf8();
}

QString TypeSerializer::decodeDecimal(const QByteArray& data) {
    return QString::fromUtf8(data);
}

// ============ 字符串类型序列化 ============

bool TypeSerializer::serializeString(const QVariant& value, DataType type, QDataStream& stream, int maxLength) {
    QString str = value.toString();

    // 根据类型处理长度限制
    if (type == DataType::CHAR && maxLength > 0) {
        // CHAR 类型：填充到固定长度
        str = str.leftJustified(maxLength, ' ', true);
    } else if (type == DataType::TINYTEXT && str.length() > 255) {
        str = str.left(255);
    }

    // 转换为 UTF-8
    QByteArray utf8 = str.toUtf8();

    // 写入长度（2字节）+ 数据
    uint16_t len = static_cast<uint16_t>(utf8.size());
    stream << len;
    stream.writeRawData(utf8.constData(), len);

    return stream.status() == QDataStream::Ok;
}

bool TypeSerializer::deserializeString(QDataStream& stream, DataType type, QVariant& value) {
    uint16_t len;
    stream >> len;

    if (stream.status() != QDataStream::Ok) {
        return false;
    }

    // Note: len is uint16_t, so it's always <= 65535
    QByteArray utf8(len, '\0');
    if (stream.readRawData(utf8.data(), len) != len) {
        return false;
    }

    QString str = QString::fromUtf8(utf8);

    // CHAR 类型：去除尾部空格
    if (type == DataType::CHAR) {
        str = str.trimmed();
    }

    value = str;
    return true;
}

// ============ 二进制类型序列化 ============

bool TypeSerializer::serializeBinary(const QVariant& value, DataType type, QDataStream& stream, int maxLength) {
    Q_UNUSED(maxLength);  // Reserved for future use

    QByteArray data = value.toByteArray();

    // 根据类型处理长度限制
    if (type == DataType::TINYBLOB && data.size() > 255) {
        data = data.left(255);
    }

    // 写入长度（4字节）+ 数据
    uint32_t len = static_cast<uint32_t>(data.size());
    stream << len;
    stream.writeRawData(data.constData(), len);

    return stream.status() == QDataStream::Ok;
}

bool TypeSerializer::deserializeBinary(QDataStream& stream, DataType type, QVariant& value) {
    Q_UNUSED(type);

    uint32_t len;
    stream >> len;

    if (stream.status() != QDataStream::Ok) {
        return false;
    }

    // 防止过大的分配（限制为 16MB）
    if (len > 16 * 1024 * 1024) {
        LOG_ERROR(QString("Binary data too large: %1").arg(len));
        return false;
    }

    QByteArray data(len, '\0');
    if (stream.readRawData(data.data(), len) != static_cast<int>(len)) {
        return false;
    }

    value = data;
    return true;
}

// ============ 日期时间类型序列化 ============

bool TypeSerializer::serializeDateTime(const QVariant& value, DataType type, QDataStream& stream) {
    switch (type) {
        case DataType::DATE: {
            // DATE: 存储为自 epoch 以来的天数（4字节）
            QDate date = value.toDate();
            if (!date.isValid()) {
                return false;
            }
            int32_t days = QDate(1970, 1, 1).daysTo(date);
            stream << days;
            break;
        }
        case DataType::TIME: {
            // TIME: 存储为自午夜以来的秒数（4字节）
            QTime time = value.toTime();
            if (!time.isValid()) {
                return false;
            }
            int32_t seconds = QTime(0, 0, 0).secsTo(time);
            stream << seconds;
            break;
        }
        case DataType::DATETIME:
        case DataType::DATETIME2:
        case DataType::TIMESTAMP:
        case DataType::TIMESTAMP_TZ:
        case DataType::DATETIMEOFFSET: {
            // DATETIME/TIMESTAMP: 存储为 Unix 微秒时间戳（8字节）
            QDateTime dateTime = value.toDateTime();
            if (!dateTime.isValid()) {
                return false;
            }
            int64_t microsSinceEpoch = dateTime.toMSecsSinceEpoch() * 1000;
            stream << microsSinceEpoch;
            break;
        }
        case DataType::SMALLDATETIME: {
            // SMALLDATETIME: 存储为 Unix 秒时间戳（4字节）
            QDateTime dateTime = value.toDateTime();
            if (!dateTime.isValid()) {
                return false;
            }
            int32_t secsSinceEpoch = static_cast<int32_t>(dateTime.toSecsSinceEpoch());
            stream << secsSinceEpoch;
            break;
        }
        default:
            LOG_ERROR(QString("Invalid datetime type: %1").arg(static_cast<int>(type)));
            return false;
    }

    return stream.status() == QDataStream::Ok;
}

bool TypeSerializer::deserializeDateTime(QDataStream& stream, DataType type, QVariant& value) {
    switch (type) {
        case DataType::DATE: {
            int32_t days;
            stream >> days;
            if (stream.status() == QDataStream::Ok) {
                QDate date = QDate(1970, 1, 1).addDays(days);
                value = date;
                return true;
            }
            break;
        }
        case DataType::TIME: {
            int32_t seconds;
            stream >> seconds;
            if (stream.status() == QDataStream::Ok) {
                QTime time = QTime(0, 0, 0).addSecs(seconds);
                value = time;
                return true;
            }
            break;
        }
        case DataType::DATETIME:
        case DataType::DATETIME2:
        case DataType::TIMESTAMP:
        case DataType::TIMESTAMP_TZ:
        case DataType::DATETIMEOFFSET: {
            int64_t microsSinceEpoch;
            stream >> microsSinceEpoch;
            if (stream.status() == QDataStream::Ok) {
                QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(microsSinceEpoch / 1000);
                value = dateTime;
                return true;
            }
            break;
        }
        case DataType::SMALLDATETIME: {
            int32_t secsSinceEpoch;
            stream >> secsSinceEpoch;
            if (stream.status() == QDataStream::Ok) {
                QDateTime dateTime = QDateTime::fromSecsSinceEpoch(secsSinceEpoch);
                value = dateTime;
                return true;
            }
            break;
        }
        default:
            LOG_ERROR(QString("Invalid datetime type: %1").arg(static_cast<int>(type)));
            return false;
    }

    return false;
}

// ============ 布尔类型序列化 ============

bool TypeSerializer::serializeBoolean(const QVariant& value, QDataStream& stream) {
    uint8_t v = value.toBool() ? 1 : 0;
    stream.writeRawData(reinterpret_cast<const char*>(&v), 1);
    return stream.status() == QDataStream::Ok;
}

bool TypeSerializer::deserializeBoolean(QDataStream& stream, QVariant& value) {
    uint8_t v;
    if (stream.readRawData(reinterpret_cast<char*>(&v), 1) == 1) {
        value = (v != 0);
        return true;
    }
    return false;
}

// ============ JSON/XML 类型序列化 ============

bool TypeSerializer::serializeJsonXml(const QVariant& value, DataType type, QDataStream& stream) {
    QString str;

    if (type == DataType::JSON || type == DataType::JSONB) {
        // JSON: 验证并压缩
        QJsonDocument doc = QJsonDocument::fromJson(value.toString().toUtf8());
        if (doc.isNull()) {
            // 如果不是有效的JSON，直接存储字符串
            str = value.toString();
        } else {
            // 存储压缩的JSON
            // 注意：Qt6移除了toBinaryData()，所以JSONB和JSON都使用相同的文本格式
            str = doc.toJson(QJsonDocument::Compact);
        }
    } else {
        // XML: 直接存储为字符串
        str = value.toString();
    }

    // 字符串存储（类似 TEXT 类型）
    QByteArray utf8 = str.toUtf8();
    uint32_t len = static_cast<uint32_t>(utf8.size());
    stream << len;
    stream.writeRawData(utf8.constData(), len);

    return stream.status() == QDataStream::Ok;
}

bool TypeSerializer::deserializeJsonXml(QDataStream& stream, DataType type, QVariant& value) {
    Q_UNUSED(type);  // Type is not used in current implementation

    // JSONB/JSON/XML: 都读取字符串格式
    uint32_t len;
    stream >> len;

    if (stream.status() != QDataStream::Ok || len > 16 * 1024 * 1024) {
        return false;
    }

    QByteArray utf8(len, '\0');
    if (stream.readRawData(utf8.data(), len) != static_cast<int>(len)) {
        return false;
    }

    value = QString::fromUtf8(utf8);
    return true;
}

// ============ 特殊类型序列化 ============

bool TypeSerializer::serializeSpecial(const QVariant& value, DataType type, QDataStream& stream) {
    switch (type) {
        case DataType::UUID:
        case DataType::UNIQUEIDENTIFIER: {
            // UUID: 存储为 16 字节
            QString uuidStr = value.toString();
            QByteArray uuidData = parseUUID(uuidStr);
            if (uuidData.size() != 16) {
                LOG_ERROR(QString("Invalid UUID: %1").arg(uuidStr));
                return false;
            }
            stream.writeRawData(uuidData.constData(), 16);
            break;
        }
        case DataType::ROWID: {
            // ROWID: 存储为 8 字节整数
            int64_t rowid = value.toLongLong();
            stream << rowid;
            break;
        }
        case DataType::GEOMETRY:
        case DataType::GEOGRAPHY: {
            // 空间类型: 存储为 WKB (Well-Known Binary)
            QString wkt = value.toString();
            QByteArray wkb = parseWKB(wkt);
            uint32_t len = static_cast<uint32_t>(wkb.size());
            stream << len;
            stream.writeRawData(wkb.constData(), len);
            break;
        }
        case DataType::HIERARCHYID: {
            // HIERARCHYID: 存储为字符串
            QString str = value.toString();
            QByteArray utf8 = str.toUtf8();
            uint16_t len = static_cast<uint16_t>(utf8.size());
            stream << len;
            stream.writeRawData(utf8.constData(), len);
            break;
        }
        default:
            LOG_ERROR(QString("Unsupported special type: %1").arg(static_cast<int>(type)));
            return false;
    }

    return stream.status() == QDataStream::Ok;
}

bool TypeSerializer::deserializeSpecial(QDataStream& stream, DataType type, QVariant& value) {
    switch (type) {
        case DataType::UUID:
        case DataType::UNIQUEIDENTIFIER: {
            QByteArray uuidData(16, '\0');
            if (stream.readRawData(uuidData.data(), 16) == 16) {
                QString uuidStr = formatUUID(uuidData);
                value = uuidStr;
                return true;
            }
            break;
        }
        case DataType::ROWID: {
            int64_t rowid;
            stream >> rowid;
            if (stream.status() == QDataStream::Ok) {
                value = rowid;
                return true;
            }
            break;
        }
        case DataType::GEOMETRY:
        case DataType::GEOGRAPHY: {
            uint32_t len;
            stream >> len;

            if (stream.status() != QDataStream::Ok || len > 16 * 1024 * 1024) {
                return false;
            }

            QByteArray wkb(len, '\0');
            if (stream.readRawData(wkb.data(), len) != static_cast<int>(len)) {
                return false;
            }

            QString wkt = formatWKT(wkb);
            value = wkt;
            return true;
        }
        case DataType::HIERARCHYID: {
            uint16_t len;
            stream >> len;

            if (stream.status() != QDataStream::Ok || len > 1024) {
                return false;
            }

            QByteArray utf8(len, '\0');
            if (stream.readRawData(utf8.data(), len) != len) {
                return false;
            }

            value = QString::fromUtf8(utf8);
            return true;
        }
        default:
            LOG_ERROR(QString("Unsupported special type: %1").arg(static_cast<int>(type)));
            return false;
    }

    return false;
}

// ============ UUID 辅助函数 ============

QByteArray TypeSerializer::parseUUID(const QString& uuidStr) {
    // 移除大括号和连字符
    QString cleaned = uuidStr;
    cleaned.remove('{').remove('}').remove('-');

    // 转换为16字节
    QByteArray result;
    result.reserve(16);

    for (int i = 0; i < 32 && i < cleaned.length(); i += 2) {
        QString byteStr = cleaned.mid(i, 2);
        bool ok;
        uint8_t byte = static_cast<uint8_t>(byteStr.toUInt(&ok, 16));
        if (ok) {
            result.append(byte);
        }
    }

    // 填充到16字节
    while (result.size() < 16) {
        result.append('\0');
    }

    return result;
}

QString TypeSerializer::formatUUID(const QByteArray& uuidData) {
    if (uuidData.size() != 16) {
        return QString();
    }

    // 格式: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    QString result;
    for (int i = 0; i < 16; ++i) {
        result += QString("%1").arg(static_cast<uint8_t>(uuidData[i]), 2, 16, QChar('0'));
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            result += '-';
        }
    }

    return result.toUpper();
}

// ============ WKB 辅助函数（空间类型）============

QByteArray TypeSerializer::parseWKB(const QString& wktStr) {
    // 简化实现：仅支持 POINT 类型
    // 生产环境应使用完整的 WKT/WKB 解析库（如 GEOS）

    // 示例 WKT: "POINT(1.0 2.0)"
    if (wktStr.startsWith("POINT", Qt::CaseInsensitive)) {
        // 解析坐标
        QString coords = wktStr.mid(6); // 跳过 "POINT("
        coords = coords.remove('(').remove(')').trimmed();

        QStringList parts = coords.split(' ', Qt::SkipEmptyParts);
        if (parts.size() == 2) {
            double x = parts[0].toDouble();
            double y = parts[1].toDouble();

            // WKB 格式: [byte order][type][x][y]
            QByteArray wkb;
            QDataStream stream(&wkb, QIODevice::WriteOnly);
            stream.setByteOrder(QDataStream::LittleEndian);

            stream << static_cast<uint8_t>(1); // Little Endian
            stream << static_cast<uint32_t>(1); // Point type
            stream << x;
            stream << y;

            return wkb;
        }
    }

    // 其他类型：返回空数组
    LOG_WARN(QString("Unsupported WKT format: %1").arg(wktStr));
    return QByteArray();
}

QString TypeSerializer::formatWKT(const QByteArray& wkbData) {
    // 简化实现：仅支持 POINT 类型
    if (wkbData.size() < 21) {
        return QString();
    }

    QDataStream stream(wkbData);

    uint8_t byteOrder;
    stream >> byteOrder;

    if (byteOrder == 1) {
        stream.setByteOrder(QDataStream::LittleEndian);
    } else {
        stream.setByteOrder(QDataStream::BigEndian);
    }

    uint32_t geomType;
    stream >> geomType;

    if (geomType == 1) { // Point
        double x, y;
        stream >> x >> y;
        return QString("POINT(%1 %2)").arg(x).arg(y);
    }

    LOG_WARN(QString("Unsupported WKB geometry type: %1").arg(geomType));
    return QString();
}

} // namespace qindb
