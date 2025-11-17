#ifndef QINDB_RESULT_EXPORTER_H
#define QINDB_RESULT_EXPORTER_H

#include "qindb/query_result.h"
#include <QString>
#include <QVector>
#include <QVariant>

namespace qindb {

/**
 * @brief 导出格式枚举类
 * 定义了支持的三种导出格式类型
 */
enum class ExportFormat {
    JSON,       // JSON格式
    CSV,        // CSV格式（逗号分隔值）
    XML         // XML格式
};

/**
 * @brief 结果导出器
 *
 * 职责：
 * - 将QueryResult转换为不同的导出格式
 * - 支持JSON、CSV、XML格式
 * - 处理NULL值、特殊字符转义
 */
class ResultExporter {
public:
    /**
     * @brief 导出查询结果为指定格式
     *
     * @param result 查询结果
     * @param format 导出格式
     * @return QString 导出的字符串内容
     */
    static QString exportToString(const QueryResult& result, ExportFormat format);

    /**
     * @brief 导出查询结果到文件
     *
     * @param result 查询结果
     * @param format 导出格式
     * @param filePath 输出文件路径
     * @return bool 是否成功
     */
    static bool exportToFile(const QueryResult& result, ExportFormat format, const QString& filePath);

    /**
     * @brief 导出为JSON格式
     *
     * JSON格式示例：
     * {
     *   "columns": ["id", "name", "age"],
     *   "rows": [
     *     {"id": 1, "name": "Alice", "age": 30},
     *     {"id": 2, "name": "Bob", "age": 25}
     *   ],
     *   "rowCount": 2
     * }
     *
     * @param result 查询结果
     * @return QString JSON字符串
     */
    static QString exportToJSON(const QueryResult& result);

    /**
     * @brief 导出为CSV格式
     *
     * CSV格式示例：
     * id,name,age
     * 1,"Alice",30
     * 2,"Bob",25
     *
     * @param result 查询结果
     * @return QString CSV字符串
     */
    static QString exportToCSV(const QueryResult& result);

    /**
     * @brief 导出为XML格式
     *
     * XML格式示例：
     * <?xml version="1.0" encoding="UTF-8"?>
     * <resultset>
     *   <columns>
     *     <column>id</column>
     *     <column>name</column>
     *     <column>age</column>
     *   </columns>
     *   <rows>
     *     <row>
     *       <id>1</id>
     *       <name>Alice</name>
     *       <age>30</age>
     *     </row>
     *     <row>
     *       <id>2</id>
     *       <name>Bob</name>
     *       <age>25</age>
     *     </row>
     *   </rows>
     *   <rowCount>2</rowCount>
     * </resultset>
     *
     * @param result 查询结果
     * @return QString XML字符串
     */
    static QString exportToXML(const QueryResult& result);

private:
    /**
     * @brief 将QVariant转换为JSON值字符串
     *
     * @param value QVariant值
     * @return QString JSON格式的值字符串
     */
    static QString variantToJSON(const QVariant& value);

    /**
     * @brief 将QVariant转换为CSV值字符串（处理引号转义）
     *
     * @param value QVariant值
     * @return QString CSV格式的值字符串
     */
    static QString variantToCSV(const QVariant& value);

    /**
     * @brief 将QVariant转换为XML值字符串（处理XML转义）
     *
     * @param value QVariant值
     * @return QString XML格式的值字符串
     */
    static QString variantToXML(const QVariant& value);

    /**
     * @brief 转义JSON字符串中的特殊字符
     *
     * @param str 原始字符串
     * @return QString 转义后的字符串
     */
    static QString escapeJSON(const QString& str);

    /**
     * @brief 转义CSV字符串中的特殊字符（双引号）
     *
     * @param str 原始字符串
     * @return QString 转义后的字符串
     */
    static QString escapeCSV(const QString& str);

    /**
     * @brief 转义XML字符串中的特殊字符（<, >, &, ", '）
     *
     * @param str 原始字符串
     * @return QString 转义后的字符串
     */
    static QString escapeXML(const QString& str);
};

} // namespace qindb

#endif // QINDB_RESULT_EXPORTER_H
