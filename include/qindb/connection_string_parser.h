#ifndef QINDB_CONNECTION_STRING_PARSER_H  // 防止头文件重复包含
#define QINDB_CONNECTION_STRING_PARSER_H  // 定义头文件保护宏

#include <QString>        // 包含Qt字符串类
#include <QRegularExpression>
#include <optional>       // 包含可选值类型

namespace qindb {  // 定义qindb命名空间

/**
 * @brief 连接字符串解析结果
 * 
 * 该结构体存储解析连接字符串后得到的各项参数
 */
struct ConnectionParams {
    QString host;           // 主机地址
    uint16_t port;          // 端口号
    QString username;       // 用户名
    QString password;       // 密码
    bool sslEnabled;        // 是否启用SSL

    ConnectionParams()
        : port(24678)
        , sslEnabled(false) {}
};

/**
 * @brief 连接字符串解析器
 *
 * 解析格式：qindb://主机:端口?usr=用户名&pswd=密码&ssl=是否启用
 */
class ConnectionStringParser {
public:
    /**
     * @brief 解析连接字符串
     * @param connectionString 连接字符串
     * @return 解析结果，如果解析失败返回std::nullopt
     */
    static std::optional<ConnectionParams> parse(const QString& connectionString);

    /**
     * @brief 验证连接字符串格式是否正确
     * @param connectionString 连接字符串
     * @return 是否有效
     */
    static bool isValid(const QString& connectionString);

private:
    /**
     * @brief 解析查询参数
     * @param query 查询字符串
     * @param params 输出参数
     */
    static void parseQueryParams(const QString& query, ConnectionParams& params);
    
    /**
     * @brief 解析SSL值
     * @param value SSL值字符串
     * @return 解析后的布尔值
     */
    static bool parseSslValue(const QString& value);
};

} // namespace qindb

#endif // QINDB_CONNECTION_STRING_PARSER_H
