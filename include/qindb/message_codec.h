#ifndef QINDB_MESSAGE_CODEC_H
#define QINDB_MESSAGE_CODEC_H

#include "qindb/protocol.h"
#include <QtCore/QByteArray>
#include <QtCore/QDataStream>
#include <optional>

namespace qindb {

/**
 * @brief 消息编解码器
 *
 * 负责将协议消息序列化为字节流，以及从字节流反序列化为消息对象。
 * 所有多字节整数使用网络字节序（Big-Endian）。
 */
class MessageCodec {
public:
    /**
     * @brief 编码通用消息头部
     * @param type 消息类型
     * @param payload 消息负载数据
     * @return 完整的消息字节流（包含长度前缀）
     */
    static QByteArray encodeMessage(MessageType type, const QByteArray& payload);

    /**
     * @brief 解码通用消息头部
     * @param data 消息字节流
     * @param type [out] 消息类型
     * @param payload [out] 消息负载数据
     * @return 是否解码成功
     */
    static bool decodeMessage(const QByteArray& data, MessageType& type, QByteArray& payload);

    // ========== AUTH_REQUEST 编解码 ==========

    /**
     * @brief 编码 AUTH_REQUEST 消息
     */
    static QByteArray encodeAuthRequest(const AuthRequest& request);

    /**
     * @brief 解码 AUTH_REQUEST 消息
     */
    static std::optional<AuthRequest> decodeAuthRequest(const QByteArray& payload);

    // ========== AUTH_RESPONSE 编解码 ==========

    /**
     * @brief 编码 AUTH_RESPONSE 消息
     */
    static QByteArray encodeAuthResponse(const AuthResponse& response);

    /**
     * @brief 解码 AUTH_RESPONSE 消息
     */
    static std::optional<AuthResponse> decodeAuthResponse(const QByteArray& payload);

    // ========== QUERY_REQUEST 编解码 ==========

    /**
     * @brief 编码 QUERY_REQUEST 消息
     */
    static QByteArray encodeQueryRequest(const QueryRequest& request);

    /**
     * @brief 解码 QUERY_REQUEST 消息
     */
    static std::optional<QueryRequest> decodeQueryRequest(const QByteArray& payload);

    // ========== QUERY_RESPONSE 编解码 ==========

    /**
     * @brief 编码 QUERY_RESPONSE 消息
     */
    static QByteArray encodeQueryResponse(const QueryResponse& result);

    /**
     * @brief 解码 QUERY_RESPONSE 消息
     */
    static std::optional<QueryResponse> decodeQueryResponse(const QByteArray& payload);

    // ========== ERROR_RESPONSE 编解码 ==========

    /**
     * @brief 编码 ERROR_RESPONSE 消息
     */
    static QByteArray encodeErrorResponse(const ErrorResponse& error);

    /**
     * @brief 解码 ERROR_RESPONSE 消息
     */
    static std::optional<ErrorResponse> decodeErrorResponse(const QByteArray& payload);

    // ========== 辅助方法 ==========

    /**
     * @brief 编码字符串（长度前缀 + UTF-8 数据）
     */
    static void encodeString(QDataStream& stream, const QString& str);

    /**
     * @brief 解码字符串
     */
    static QString decodeString(QDataStream& stream);

    /**
     * @brief 编码 QVariant（支持数据库的所有数据类型）
     */
    static void encodeVariant(QDataStream& stream, const QVariant& value);

    /**
     * @brief 解码 QVariant
     */
    static QVariant decodeVariant(QDataStream& stream, uint8_t type);
};

} // namespace qindb

#endif // QINDB_MESSAGE_CODEC_H
