#ifndef QINDB_QUERY_RESULT_H  // 防止头文件被重复包含
#define QINDB_QUERY_RESULT_H

#include "qindb/common.h"  // 引入项目通用头文件
#include <QString>        // Qt字符串类
#include <QVector>        // Qt动态数组容器
#include <QVariant>       // Qt通用数据类型

namespace qindb {  // 定义qindb命名空间

/**
 * @brief 查询结果集
 * 
 * 该结构体用于存储数据库查询的结果，包括列名、数据行、
 * 消息、成功状态和错误信息等。
 */
struct QueryResult {
    QVector<QString> columnNames;        // 列名集合，存储查询结果的列名
    QVector<QVector<QVariant>> rows;     // 数据行集合，每行是一个QVariant的动态数组
    QString message;                     // 消息（用于DDL等）
    QString currentDatabase;             // 当前使用的数据库名（用于USE DATABASE等操作）
    bool success;                        // 是否成功
    Error error;                         // 错误信息

    QueryResult()
        : columnNames()
        , rows()
        , message("")
        , currentDatabase("")
        , success(false)
        , error(ErrorCode::SUCCESS, "")
    {}
};

} // namespace qindb

#endif // QINDB_QUERY_RESULT_H
