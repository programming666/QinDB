#include "qindb/result_exporter.h"
#include "qindb/logger.h"
#include <QFile>
#include <QTextStream>
#include <QMetaType>
#include <QDateTime>

namespace qindb {

QString ResultExporter::exportToString(const QueryResult& result, ExportFormat format) {
    switch (format) {
        case ExportFormat::JSON:
            return exportToJSON(result);
        case ExportFormat::CSV:
            return exportToCSV(result);
        case ExportFormat::XML:
            return exportToXML(result);
        default:
            LOG_ERROR("Unknown export format");
            return QString();
    }
}

bool ResultExporter::exportToFile(const QueryResult& result, ExportFormat format, const QString& filePath) {
    QString content = exportToString(result, format);
    if (content.isEmpty() && result.rows.size() > 0) {
        LOG_ERROR(QString("Failed to export result to format %1").arg(static_cast<int>(format)));
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_ERROR(QString("Failed to open file for writing: %1").arg(filePath));
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << content;
    file.close();

    LOG_INFO(QString("Successfully exported %1 rows to %2")
                 .arg(result.rows.size())
                 .arg(filePath));
    return true;
}

QString ResultExporter::exportToJSON(const QueryResult& result) {
    QString json = "{\n";
    json += "  \"columns\": [";

    // 列名
    for (int i = 0; i < result.columnNames.size(); ++i) {
        if (i > 0) json += ", ";
        json += "\"" + escapeJSON(result.columnNames[i]) + "\"";
    }
    json += "],\n";

    // 数据行
    json += "  \"rows\": [\n";
    for (int rowIdx = 0; rowIdx < result.rows.size(); ++rowIdx) {
        if (rowIdx > 0) json += ",\n";
        const auto& row = result.rows[rowIdx];

        json += "    {";
        for (int colIdx = 0; colIdx < result.columnNames.size() && colIdx < row.size(); ++colIdx) {
            if (colIdx > 0) json += ", ";

            const QString& colName = result.columnNames[colIdx];
            const QVariant& value = row[colIdx];

            json += "\"" + escapeJSON(colName) + "\": " + variantToJSON(value);
        }
        json += "}";
    }
    json += "\n  ],\n";

    // 行数
    json += QString("  \"rowCount\": %1\n").arg(result.rows.size());
    json += "}";

    return json;
}

QString ResultExporter::exportToCSV(const QueryResult& result) {
    QString csv;

    // 列名
    for (int i = 0; i < result.columnNames.size(); ++i) {
        if (i > 0) csv += ",";
        csv += "\"" + escapeCSV(result.columnNames[i]) + "\"";
    }
    csv += "\n";

    // 数据行
    for (const auto& row : result.rows) {
        for (int i = 0; i < row.size() && i < result.columnNames.size(); ++i) {
            if (i > 0) csv += ",";
            csv += variantToCSV(row[i]);
        }
        csv += "\n";
    }

    return csv;
}

QString ResultExporter::exportToXML(const QueryResult& result) {
    QString xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<resultset>\n";

    // 列名
    xml += "  <columns>\n";
    for (const QString& colName : result.columnNames) {
        xml += "    <column>" + escapeXML(colName) + "</column>\n";
    }
    xml += "  </columns>\n";

    // 数据行
    xml += "  <rows>\n";
    for (const auto& row : result.rows) {
        xml += "    <row>\n";

        for (int i = 0; i < result.columnNames.size() && i < row.size(); ++i) {
            const QString& colName = result.columnNames[i];
            const QVariant& value = row[i];

            xml += "      <" + escapeXML(colName) + ">";
            xml += variantToXML(value);
            xml += "</" + escapeXML(colName) + ">\n";
        }

        xml += "    </row>\n";
    }
    xml += "  </rows>\n";

    // 行数
    xml += QString("  <rowCount>%1</rowCount>\n").arg(result.rows.size());
    xml += "</resultset>";

    return xml;
}

QString ResultExporter::variantToJSON(const QVariant& value) {
    if (value.isNull()) {
        return "null";
    }

    switch (value.metaType().id()) {
        case QMetaType::Bool:
            return value.toBool() ? "true" : "false";

        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::UInt:
        case QMetaType::ULongLong:
        case QMetaType::Short:
        case QMetaType::UShort:
        case QMetaType::Char:
        case QMetaType::UChar:
            return QString::number(value.toLongLong());

        case QMetaType::Float:
        case QMetaType::Double:
            return QString::number(value.toDouble(), 'g', 15);

        case QMetaType::QString:
            return "\"" + escapeJSON(value.toString()) + "\"";

        case QMetaType::QByteArray:
            // Base64编码二进制数据
            return "\"" + value.toByteArray().toBase64() + "\"";

        case QMetaType::QDate:
            return "\"" + value.toDate().toString(Qt::ISODate) + "\"";

        case QMetaType::QTime:
            return "\"" + value.toTime().toString(Qt::ISODate) + "\"";

        case QMetaType::QDateTime:
            return "\"" + value.toDateTime().toString(Qt::ISODate) + "\"";

        default:
            // 其他类型转换为字符串
            return "\"" + escapeJSON(value.toString()) + "\"";
    }
}

QString ResultExporter::variantToCSV(const QVariant& value) {
    if (value.isNull()) {
        return "NULL";
    }

    switch (value.metaType().id()) {
        case QMetaType::Bool:
            return value.toBool() ? "true" : "false";

        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::UInt:
        case QMetaType::ULongLong:
        case QMetaType::Short:
        case QMetaType::UShort:
        case QMetaType::Char:
        case QMetaType::UChar:
            return QString::number(value.toLongLong());

        case QMetaType::Float:
        case QMetaType::Double:
            return QString::number(value.toDouble(), 'g', 15);

        case QMetaType::QString:
            // CSV字符串需要用双引号包围并转义内部双引号
            return "\"" + escapeCSV(value.toString()) + "\"";

        case QMetaType::QByteArray:
            // Base64编码二进制数据
            return "\"" + value.toByteArray().toBase64() + "\"";

        case QMetaType::QDate:
            return "\"" + value.toDate().toString(Qt::ISODate) + "\"";

        case QMetaType::QTime:
            return "\"" + value.toTime().toString(Qt::ISODate) + "\"";

        case QMetaType::QDateTime:
            return "\"" + value.toDateTime().toString(Qt::ISODate) + "\"";

        default:
            // 其他类型转换为字符串并加引号
            return "\"" + escapeCSV(value.toString()) + "\"";
    }
}

QString ResultExporter::variantToXML(const QVariant& value) {
    if (value.isNull()) {
        return "";  // XML中空标签表示NULL
    }

    switch (value.metaType().id()) {
        case QMetaType::Bool:
            return value.toBool() ? "true" : "false";

        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::UInt:
        case QMetaType::ULongLong:
        case QMetaType::Short:
        case QMetaType::UShort:
        case QMetaType::Char:
        case QMetaType::UChar:
            return QString::number(value.toLongLong());

        case QMetaType::Float:
        case QMetaType::Double:
            return QString::number(value.toDouble(), 'g', 15);

        case QMetaType::QString:
            return escapeXML(value.toString());

        case QMetaType::QByteArray:
            // Base64编码二进制数据
            return value.toByteArray().toBase64();

        case QMetaType::QDate:
            return value.toDate().toString(Qt::ISODate);

        case QMetaType::QTime:
            return value.toTime().toString(Qt::ISODate);

        case QMetaType::QDateTime:
            return value.toDateTime().toString(Qt::ISODate);

        default:
            // 其他类型转换为字符串
            return escapeXML(value.toString());
    }
}

QString ResultExporter::escapeJSON(const QString& str) {
    QString escaped;
    escaped.reserve(str.size() * 2);

    for (const QChar& ch : str) {
        switch (ch.unicode()) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (ch.unicode() < 0x20) {
                    // 控制字符转换为\uXXXX格式
                    escaped += QString("\\u%1").arg(static_cast<int>(ch.unicode()), 4, 16, QChar('0'));
                } else {
                    escaped += ch;
                }
        }
    }

    return escaped;
}

QString ResultExporter::escapeCSV(const QString& str) {
    // CSV规则：双引号转义为两个双引号
    QString escaped = str;
    escaped.replace("\"", "\"\"");
    return escaped;
}

QString ResultExporter::escapeXML(const QString& str) {
    QString escaped;
    escaped.reserve(str.size() * 2);

    for (const QChar& ch : str) {
        switch (ch.unicode()) {
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '&':
                escaped += "&amp;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&apos;";
                break;
            default:
                escaped += ch;
        }
    }

    return escaped;
}

} // namespace qindb
