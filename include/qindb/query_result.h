#ifndef QINDB_QUERY_RESULT_H
#define QINDB_QUERY_RESULT_H

#include "qindb/common.h"
#include <QString>
#include <QVector>
#include <QVariant>

namespace qindb {

/**
 * @brief 查询结果集
 */
struct QueryResult {
    QVector<QString> columnNames;        // 列名
    QVector<QVector<QVariant>> rows;     // 数据行
    QString message;                     // 消息（用于DDL等）
    bool success;                        // 是否成功
    Error error;                         // 错误信息

    QueryResult()
        : success(false)
        , error(ErrorCode::SUCCESS, "")
    {}
};

} // namespace qindb

#endif // QINDB_QUERY_RESULT_H
