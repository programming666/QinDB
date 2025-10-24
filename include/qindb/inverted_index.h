#ifndef QINDB_INVERTED_INDEX_H
#define QINDB_INVERTED_INDEX_H

#include "qindb/common.h"
#include "qindb/tokenizer.h"
#include "qindb/buffer_pool_manager.h"
#include <QString>
#include <QMap>
#include <QVector>
#include <QPair>
#include <QMutex>

namespace qindb {

/**
 * @brief 倒排索引项（Posting）
 *
 * 存储文档ID和词频信息
 */
struct Posting {
    RowId docId;        // 文档ID（行ID）
    uint32_t tf;        // 词频（Term Frequency）
    QVector<uint32_t> positions;  // 词在文档中的位置（可选，用于短语查询）

    Posting() : docId(INVALID_ROW_ID), tf(0) {}
    Posting(RowId id, uint32_t frequency) : docId(id), tf(frequency) {}
};

/**
 * @brief 倒排列表（Posting List）
 *
 * 存储包含某个词的所有文档
 */
struct PostingList {
    QString term;                   // 词项
    uint32_t df;                    // 文档频率（Document Frequency）
    QVector<Posting> postings;      // 倒排列表

    PostingList() : df(0) {}
    explicit PostingList(const QString& t) : term(t), df(0) {}
};

/**
 * @brief 查询结果（带相关性得分）
 */
struct SearchResult {
    RowId docId;        // 文档ID
    double score;       // 相关性得分（TF-IDF）

    SearchResult() : docId(INVALID_ROW_ID), score(0.0) {}
    SearchResult(RowId id, double s) : docId(id), score(s) {}

    bool operator<(const SearchResult& other) const {
        return score > other.score;  // 降序排序（得分高的在前）
    }
};

/**
 * @brief 倒排索引
 *
 * 支持：
 * - 全文搜索
 * - TF-IDF 相关性排序
 * - 布尔查询（AND, OR, NOT）
 * - 短语查询（可选）
 */
class InvertedIndex {
public:
    /**
     * @brief 构造函数
     * @param indexName 索引名称
     * @param bufferPool 缓冲池管理器
     * @param tokenizer 分词器（可选，默认使用混合模式）
     */
    InvertedIndex(const QString& indexName,
                  BufferPoolManager* bufferPool,
                  Tokenizer* tokenizer = nullptr);

    ~InvertedIndex();

    /**
     * @brief 插入文档到索引
     * @param docId 文档ID
     * @param text 文档文本
     * @return true if 成功
     */
    bool insert(RowId docId, const QString& text);

    /**
     * @brief 从索引中删除文档
     * @param docId 文档ID
     * @return true if 成功
     */
    bool remove(RowId docId);

    /**
     * @brief 更新文档
     * @param docId 文档ID
     * @param newText 新文本
     * @return true if 成功
     */
    bool update(RowId docId, const QString& newText);

    /**
     * @brief 全文搜索（单词查询）
     * @param query 查询词
     * @param limit 返回结果数量限制（0表示无限制）
     * @return 搜索结果（按相关性得分降序排列）
     */
    QVector<SearchResult> search(const QString& query, int limit = 0);

    /**
     * @brief 全文搜索（多词查询，AND 模式）
     * @param queryTerms 查询词列表
     * @param limit 返回结果数量限制
     * @return 搜索结果（按相关性得分降序排列）
     */
    QVector<SearchResult> searchAnd(const QStringList& queryTerms, int limit = 0);

    /**
     * @brief 全文搜索（多词查询，OR 模式）
     * @param queryTerms 查询词列表
     * @param limit 返回结果数量限制
     * @return 搜索结果（按相关性得分降序排列）
     */
    QVector<SearchResult> searchOr(const QStringList& queryTerms, int limit = 0);

    /**
     * @brief 计算 TF-IDF 得分
     * @param term 词项
     * @param docId 文档ID
     * @return TF-IDF 得分
     */
    double calculateTfIdf(const QString& term, RowId docId);

    /**
     * @brief 获取词项的文档频率
     * @param term 词项
     * @return 文档频率（包含该词的文档数量）
     */
    uint32_t getDocumentFrequency(const QString& term) const;

    /**
     * @brief 获取总文档数
     */
    uint32_t getTotalDocuments() const { return totalDocuments_; }

    /**
     * @brief 获取索引统计信息
     */
    struct Statistics {
        uint32_t numTerms;          // 词项数量
        uint32_t numDocuments;      // 文档数量
        uint32_t totalPostings;     // 总倒排项数量
        double avgDocLength;        // 平均文档长度
    };
    Statistics getStatistics() const;

    /**
     * @brief 获取根页面ID（用于持久化）
     */
    PageId getRootPageId() const { return rootPageId_; }

    /**
     * @brief 设置根页面ID（用于加载索引）
     */
    void setRootPageId(PageId pageId) { rootPageId_ = pageId; }

private:
    /**
     * @brief 计算 TF（词频）
     * @param tf 词在文档中出现的次数
     * @param docLength 文档总词数
     * @return TF 得分
     */
    double calculateTF(uint32_t tf, uint32_t docLength) const;

    /**
     * @brief 计算 IDF（逆文档频率）
     * @param df 文档频率
     * @return IDF 得分
     */
    double calculateIDF(uint32_t df) const;

    /**
     * @brief 获取文档的词项频率映射
     * @param docId 文档ID
     * @return 词项 -> 词频映射
     */
    QMap<QString, uint32_t> getDocumentTerms(RowId docId) const;

    /**
     * @brief 合并搜索结果（用于 OR 查询）
     */
    QVector<SearchResult> mergeResults(const QVector<QVector<SearchResult>>& resultSets);

    /**
     * @brief 交集搜索结果（用于 AND 查询）
     */
    QVector<SearchResult> intersectResults(const QVector<QVector<SearchResult>>& resultSets);

    QString indexName_;                         // 索引名称
    BufferPoolManager* bufferPool_;             // 缓冲池管理器
    Tokenizer* tokenizer_;                      // 分词器
    bool ownTokenizer_;                         // 是否拥有分词器（需要释放）

    // 倒排索引数据结构（内存中）
    QMap<QString, PostingList> index_;          // 词项 -> 倒排列表
    QMap<RowId, uint32_t> docLengths_;          // 文档ID -> 文档长度
    uint32_t totalDocuments_;                   // 总文档数

    PageId rootPageId_;                         // 根页面ID（预留，用于持久化）
    mutable QMutex mutex_;                      // 线程安全锁
};

} // namespace qindb

#endif // QINDB_INVERTED_INDEX_H
