#include "qindb/inverted_index.h"
#include "qindb/logger.h"
#include <QMutexLocker>
#include <QtMath>
#include <algorithm>

namespace qindb {

InvertedIndex::InvertedIndex(const QString& indexName,
                             BufferPoolManager* bufferPool,
                             Tokenizer* tokenizer)
    : indexName_(indexName)
    , bufferPool_(bufferPool)
    , tokenizer_(tokenizer)
    , ownTokenizer_(false)
    , totalDocuments_(0)
    , rootPageId_(INVALID_PAGE_ID)
{
    // 如果没有提供分词器，创建默认分词器
    if (!tokenizer_) {
        tokenizer_ = new Tokenizer(Tokenizer::Mode::MIXED, true);
        ownTokenizer_ = true;
    }

    LOG_INFO(QString("InvertedIndex created: %1").arg(indexName_));
}

InvertedIndex::~InvertedIndex() {
    if (ownTokenizer_ && tokenizer_) {
        delete tokenizer_;
    }
    LOG_INFO(QString("InvertedIndex destroyed: %1").arg(indexName_));
}

bool InvertedIndex::insert(RowId docId, const QString& text) {
    if (docId == INVALID_ROW_ID || text.isEmpty()) {
        return false;
    }

    QMutexLocker locker(&mutex_);

    // 检查文档是否已存在
    if (docLengths_.contains(docId)) {
        LOG_WARN(QString("Document %1 already exists in index, use update() instead").arg(docId));
        return false;
    }

    // 分词（保留重复词以计算词频）
    QStringList tokens = tokenizer_->tokenizeWithDuplicates(text);

    if (tokens.isEmpty()) {
        LOG_DEBUG(QString("No tokens extracted from document %1").arg(docId));
        return true;  // 空文档也算成功
    }

    // 计算词频
    QMap<QString, uint32_t> termFrequencies;
    for (const QString& term : tokens) {
        termFrequencies[term]++;
    }

    // 文档长度（总词数）
    uint32_t docLength = static_cast<uint32_t>(tokens.size());
    docLengths_[docId] = docLength;

    // 更新倒排索引
    for (auto it = termFrequencies.constBegin(); it != termFrequencies.constEnd(); ++it) {
        const QString& term = it.key();
        uint32_t tf = it.value();

        // 获取或创建倒排列表
        if (!index_.contains(term)) {
            index_[term] = PostingList(term);
        }

        PostingList& postingList = index_[term];

        // 添加倒排项
        Posting posting(docId, tf);
        postingList.postings.append(posting);
        postingList.df++;
    }

    totalDocuments_++;

    LOG_DEBUG(QString("Inserted document %1: %2 unique terms, %3 total terms")
                 .arg(docId)
                 .arg(termFrequencies.size())
                 .arg(docLength));

    return true;
}

bool InvertedIndex::remove(RowId docId) {
    if (docId == INVALID_ROW_ID) {
        return false;
    }

    QMutexLocker locker(&mutex_);

    // 检查文档是否存在
    if (!docLengths_.contains(docId)) {
        LOG_WARN(QString("Document %1 not found in index").arg(docId));
        return false;
    }

    // 从每个倒排列表中删除该文档
    for (auto it = index_.begin(); it != index_.end();) {
        PostingList& postingList = it.value();

        // 查找并删除该文档的倒排项
        for (int i = 0; i < postingList.postings.size(); ++i) {
            if (postingList.postings[i].docId == docId) {
                postingList.postings.removeAt(i);
                postingList.df--;
                break;
            }
        }

        // 如果倒排列表为空，删除该词项
        if (postingList.postings.isEmpty()) {
            it = index_.erase(it);
        } else {
            ++it;
        }
    }

    // 删除文档长度记录
    docLengths_.remove(docId);
    totalDocuments_--;

    LOG_DEBUG(QString("Removed document %1 from index").arg(docId));

    return true;
}

bool InvertedIndex::update(RowId docId, const QString& newText) {
    QMutexLocker locker(&mutex_);

    // 先删除旧文档
    locker.unlock();
    bool removed = remove(docId);
    locker.relock();

    if (!removed) {
        LOG_DEBUG(QString("Document %1 not found, treating update as insert").arg(docId));
    }

    // 插入新文档
    locker.unlock();
    return insert(docId, newText);
}

QVector<SearchResult> InvertedIndex::search(const QString& query, int limit) {
    if (query.isEmpty()) {
        return QVector<SearchResult>();
    }

    QMutexLocker locker(&mutex_);

    // 分词查询
    QStringList queryTerms = tokenizer_->tokenize(query);

    if (queryTerms.isEmpty()) {
        LOG_DEBUG("No valid query terms after tokenization");
        return QVector<SearchResult>();
    }

    // 默认使用 OR 模式（任意词匹配）
    locker.unlock();
    return searchOr(queryTerms, limit);
}

QVector<SearchResult> InvertedIndex::searchAnd(const QStringList& queryTerms, int limit) {
    if (queryTerms.isEmpty()) {
        return QVector<SearchResult>();
    }

    QMutexLocker locker(&mutex_);

    QVector<QVector<SearchResult>> resultSets;

    // 为每个查询词生成结果集
    for (const QString& term : queryTerms) {
        if (!index_.contains(term)) {
            // AND 查询：如果任一词不存在，返回空结果
            return QVector<SearchResult>();
        }

        const PostingList& postingList = index_[term];
        QVector<SearchResult> results;

        for (const Posting& posting : postingList.postings) {
            double score = calculateTfIdf(term, posting.docId);
            results.append(SearchResult(posting.docId, score));
        }

        resultSets.append(results);
    }

    // 交集运算
    QVector<SearchResult> finalResults = intersectResults(resultSets);

    // 按得分排序
    std::sort(finalResults.begin(), finalResults.end());

    // 应用限制
    if (limit > 0 && finalResults.size() > limit) {
        finalResults.resize(limit);
    }

    LOG_DEBUG(QString("AND search for %1 terms: %2 results")
                 .arg(queryTerms.size())
                 .arg(finalResults.size()));

    return finalResults;
}

QVector<SearchResult> InvertedIndex::searchOr(const QStringList& queryTerms, int limit) {
    if (queryTerms.isEmpty()) {
        return QVector<SearchResult>();
    }

    QMutexLocker locker(&mutex_);

    QVector<QVector<SearchResult>> resultSets;

    // 为每个查询词生成结果集
    for (const QString& term : queryTerms) {
        if (!index_.contains(term)) {
            continue;  // OR 查询：跳过不存在的词
        }

        const PostingList& postingList = index_[term];
        QVector<SearchResult> results;

        for (const Posting& posting : postingList.postings) {
            double score = calculateTfIdf(term, posting.docId);
            results.append(SearchResult(posting.docId, score));
        }

        resultSets.append(results);
    }

    if (resultSets.isEmpty()) {
        return QVector<SearchResult>();
    }

    // 合并运算
    QVector<SearchResult> finalResults = mergeResults(resultSets);

    // 按得分排序
    std::sort(finalResults.begin(), finalResults.end());

    // 应用限制
    if (limit > 0 && finalResults.size() > limit) {
        finalResults.resize(limit);
    }

    LOG_DEBUG(QString("OR search for %1 terms: %2 results")
                 .arg(queryTerms.size())
                 .arg(finalResults.size()));

    return finalResults;
}

double InvertedIndex::calculateTfIdf(const QString& term, RowId docId) {
    if (!index_.contains(term) || !docLengths_.contains(docId)) {
        return 0.0;
    }

    const PostingList& postingList = index_[term];

    // 查找该文档的词频
    uint32_t tf = 0;
    for (const Posting& posting : postingList.postings) {
        if (posting.docId == docId) {
            tf = posting.tf;
            break;
        }
    }

    if (tf == 0) {
        return 0.0;
    }

    uint32_t docLength = docLengths_[docId];
    uint32_t df = postingList.df;

    // TF-IDF = TF * IDF
    double tfScore = calculateTF(tf, docLength);
    double idfScore = calculateIDF(df);

    return tfScore * idfScore;
}

double InvertedIndex::calculateTF(uint32_t tf, uint32_t docLength) const {
    if (docLength == 0) {
        return 0.0;
    }

    // 归一化词频：tf / docLength
    // 使用 log(1 + tf) 避免过度惩罚短文档
    return std::log(1.0 + static_cast<double>(tf)) / static_cast<double>(docLength);
}

double InvertedIndex::calculateIDF(uint32_t df) const {
    if (df == 0 || totalDocuments_ == 0) {
        return 0.0;
    }

    // IDF = log(N / df)
    // 使用 log(1 + N/df) 避免除零
    return std::log(1.0 + static_cast<double>(totalDocuments_) / static_cast<double>(df));
}

uint32_t InvertedIndex::getDocumentFrequency(const QString& term) const {
    QMutexLocker locker(&mutex_);

    if (!index_.contains(term)) {
        return 0;
    }

    return index_[term].df;
}

InvertedIndex::Statistics InvertedIndex::getStatistics() const {
    QMutexLocker locker(&mutex_);

    Statistics stats;
    stats.numTerms = static_cast<uint32_t>(index_.size());
    stats.numDocuments = totalDocuments_;
    stats.totalPostings = 0;

    for (const PostingList& postingList : index_) {
        stats.totalPostings += static_cast<uint32_t>(postingList.postings.size());
    }

    // 计算平均文档长度
    if (totalDocuments_ > 0) {
        uint64_t totalLength = 0;
        for (uint32_t length : docLengths_) {
            totalLength += length;
        }
        stats.avgDocLength = static_cast<double>(totalLength) / totalDocuments_;
    } else {
        stats.avgDocLength = 0.0;
    }

    return stats;
}

QMap<QString, uint32_t> InvertedIndex::getDocumentTerms(RowId docId) const {
    QMap<QString, uint32_t> termFrequencies;

    for (const PostingList& postingList : index_) {
        for (const Posting& posting : postingList.postings) {
            if (posting.docId == docId) {
                termFrequencies[postingList.term] = posting.tf;
                break;
            }
        }
    }

    return termFrequencies;
}

QVector<SearchResult> InvertedIndex::mergeResults(const QVector<QVector<SearchResult>>& resultSets) {
    if (resultSets.isEmpty()) {
        return QVector<SearchResult>();
    }

    if (resultSets.size() == 1) {
        return resultSets[0];
    }

    // 使用 QMap 聚合得分（同一文档的得分相加）
    QMap<RowId, double> scoreMap;

    for (const QVector<SearchResult>& resultSet : resultSets) {
        for (const SearchResult& result : resultSet) {
            scoreMap[result.docId] += result.score;
        }
    }

    // 转换为 QVector
    QVector<SearchResult> merged;
    for (auto it = scoreMap.constBegin(); it != scoreMap.constEnd(); ++it) {
        merged.append(SearchResult(it.key(), it.value()));
    }

    return merged;
}

QVector<SearchResult> InvertedIndex::intersectResults(const QVector<QVector<SearchResult>>& resultSets) {
    if (resultSets.isEmpty()) {
        return QVector<SearchResult>();
    }

    if (resultSets.size() == 1) {
        return resultSets[0];
    }

    // 找到最小的结果集作为初始集合
    int minIndex = 0;
    int minSize = resultSets[0].size();
    for (int i = 1; i < resultSets.size(); ++i) {
        if (resultSets[i].size() < minSize) {
            minSize = resultSets[i].size();
            minIndex = i;
        }
    }

    // 从最小集合开始，逐步求交集
    QSet<RowId> docIdSet;
    for (const SearchResult& result : resultSets[minIndex]) {
        docIdSet.insert(result.docId);
    }

    // 与其他集合求交集
    for (int i = 0; i < resultSets.size(); ++i) {
        if (i == minIndex) continue;

        QSet<RowId> currentSet;
        for (const SearchResult& result : resultSets[i]) {
            currentSet.insert(result.docId);
        }

        docIdSet.intersect(currentSet);

        if (docIdSet.isEmpty()) {
            break;  // 早期退出优化
        }
    }

    // 计算交集文档的总得分
    QMap<RowId, double> scoreMap;
    for (RowId docId : docIdSet) {
        double totalScore = 0.0;

        for (const QVector<SearchResult>& resultSet : resultSets) {
            for (const SearchResult& result : resultSet) {
                if (result.docId == docId) {
                    totalScore += result.score;
                    break;
                }
            }
        }

        scoreMap[docId] = totalScore;
    }

    // 转换为 QVector
    QVector<SearchResult> intersected;
    for (auto it = scoreMap.constBegin(); it != scoreMap.constEnd(); ++it) {
        intersected.append(SearchResult(it.key(), it.value()));
    }

    return intersected;
}

} // namespace qindb
