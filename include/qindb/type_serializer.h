#ifndef QINDB_TYPE_SERIALIZER_H
#define QINDB_TYPE_SERIALIZER_H

#include "common.h"
#include <QVariant>
#include <QByteArray>
#include <QDataStream>

namespace qindb {

/**
 * @brief 数据类型序列化器
 *
 * 负责将所有支持的数据类型序列化为字节流，以及从字节流反序列化
 * 支持 60+ 种 SQL 数据类型
 */
class TypeSerializer {
public:
    /**
     * @brief 序列化 QVariant 到字节流
     * @param value 要序列化的值
     * @param type 数据类型
     * @param output 输出的字节数组
     * @return 成功返回 true
     */
    static bool serialize(const QVariant& value, DataType type, QByteArray& output);

    /**
     * @brief 从字节流反序列化
     * @param data 输入字节数组
     * @param type 数据类型
     * @param value 输出的 QVariant
     * @return 成功返回 true
     */
    static bool deserialize(const QByteArray& data, DataType type, QVariant& value);

    /**
     * @brief 计算序列化后的大小
     * @param value 要序列化的值
     * @param type 数据类型
     * @return 字节数（0表示错误）
     */
    static uint32_t getSerializedSize(const QVariant& value, DataType type);

    /**
     * @brief 判断类型是否为定长类型
     */
    static bool isFixedLengthType(DataType type);

    /**
     * @brief 序列化到 QDataStream（用于 table_page.cpp）
     */
    static bool serializeToStream(const QVariant& value, DataType type, QDataStream& stream);

    /**
     * @brief 从 QDataStream 反序列化（用于 table_page.cpp）
     */
    static bool deserializeFromStream(QDataStream& stream, DataType type, QVariant& value);

private:
    // === 整数类型序列化 ===
    static bool serializeInteger(const QVariant& value, DataType type, QDataStream& stream);
    static bool deserializeInteger(QDataStream& stream, DataType type, QVariant& value);

    // === 浮点类型序列化 ===
    static bool serializeFloat(const QVariant& value, DataType type, QDataStream& stream);
    static bool deserializeFloat(QDataStream& stream, DataType type, QVariant& value);

    // === 定点数类型序列化 ===
    static bool serializeDecimal(const QVariant& value, QDataStream& stream);
    static bool deserializeDecimal(QDataStream& stream, QVariant& value);

    // === 字符串类型序列化 ===
    static bool serializeString(const QVariant& value, DataType type, QDataStream& stream, int maxLength = 0);
    static bool deserializeString(QDataStream& stream, DataType type, QVariant& value);

    // === 二进制类型序列化 ===
    static bool serializeBinary(const QVariant& value, DataType type, QDataStream& stream, int maxLength = 0);
    static bool deserializeBinary(QDataStream& stream, DataType type, QVariant& value);

    // === 日期时间类型序列化 ===
    static bool serializeDateTime(const QVariant& value, DataType type, QDataStream& stream);
    static bool deserializeDateTime(QDataStream& stream, DataType type, QVariant& value);

    // === 布尔类型序列化 ===
    static bool serializeBoolean(const QVariant& value, QDataStream& stream);
    static bool deserializeBoolean(QDataStream& stream, QVariant& value);

    // === JSON/XML 类型序列化 ===
    static bool serializeJsonXml(const QVariant& value, DataType type, QDataStream& stream);
    static bool deserializeJsonXml(QDataStream& stream, DataType type, QVariant& value);

    // === 特殊类型序列化 ===
    static bool serializeSpecial(const QVariant& value, DataType type, QDataStream& stream);
    static bool deserializeSpecial(QDataStream& stream, DataType type, QVariant& value);

    // === 辅助函数 ===
    static QByteArray encodeDecimal(const QString& decimalStr);
    static QString decodeDecimal(const QByteArray& data);

    // UUID 辅助
    static QByteArray parseUUID(const QString& uuidStr);
    static QString formatUUID(const QByteArray& uuidData);

    // WKB (Well-Known Binary) 辅助（用于空间类型）
    static QByteArray parseWKB(const QString& wktStr);
    static QString formatWKT(const QByteArray& wkbData);
};

} // namespace qindb

#endif // QINDB_TYPE_SERIALIZER_H
