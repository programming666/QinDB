#ifndef QINDB_TOKENIZER_H
#define QINDB_TOKENIZER_H

#include <QString>
#include <QStringList>
#include <QSet>
#include <QRegularExpression>

namespace qindb {

/**
 * @brief 文本分词器
 *
 * 支持：
 * - 英文分词（基于空格和标点）
 * - 简单中文分词（单字分词）
 * - 停用词过滤
 * - 大小写归一化
 * - 标点符号过滤
 */
class Tokenizer {
public:
    /**
     * @brief 分词模式
     */
    enum class Mode {
        ENGLISH,        // 英文模式（基于空格分词）
        CHINESE,        // 中文模式（单字分词）
        MIXED           // 混合模式（中英文混合）
    };

    /**
     * @brief 构造函数
     * @param mode 分词模式
     * @param enableStopWords 是否启用停用词过滤
     */
    explicit Tokenizer(Mode mode = Mode::MIXED, bool enableStopWords = true);

    /**
     * @brief 对文本进行分词
     * @param text 待分词文本
     * @return 词项列表（已去重）
     */
    QStringList tokenize(const QString& text) const;

    /**
     * @brief 对文本进行分词（保留重复词）
     * @param text 待分词文本
     * @return 词项列表（包含重复词）
     */
    QStringList tokenizeWithDuplicates(const QString& text) const;

    /**
     * @brief 归一化词项（转小写、去除标点等）
     * @param term 词项
     * @return 归一化后的词项
     */
    static QString normalize(const QString& term);

    /**
     * @brief 检查是否为停用词
     * @param term 词项
     * @return true if 停用词
     */
    bool isStopWord(const QString& term) const;

    /**
     * @brief 添加自定义停用词
     * @param stopWord 停用词
     */
    void addStopWord(const QString& stopWord);

    /**
     * @brief 移除停用词
     * @param stopWord 停用词
     */
    void removeStopWord(const QString& stopWord);

    /**
     * @brief 获取当前分词模式
     */
    Mode getMode() const { return mode_; }

    /**
     * @brief 设置分词模式
     */
    void setMode(Mode mode) { mode_ = mode; }

    /**
     * @brief 是否启用停用词过滤
     */
    bool isStopWordsEnabled() const { return enableStopWords_; }

    /**
     * @brief 设置是否启用停用词过滤
     */
    void setStopWordsEnabled(bool enabled) { enableStopWords_ = enabled; }

private:
    /**
     * @brief 英文分词
     */
    QStringList tokenizeEnglish(const QString& text) const;

    /**
     * @brief 中文分词（简单单字分词）
     */
    QStringList tokenizeChinese(const QString& text) const;

    /**
     * @brief 混合分词（中英文混合）
     */
    QStringList tokenizeMixed(const QString& text) const;

    /**
     * @brief 初始化默认停用词表
     */
    void initializeDefaultStopWords();

    /**
     * @brief 检查字符是否为中文字符
     */
    static bool isChineseChar(QChar ch);

    Mode mode_;                         // 分词模式
    bool enableStopWords_;              // 是否启用停用词过滤
    QSet<QString> stopWords_;           // 停用词集合
    QRegularExpression wordRegex_;      // 英文单词正则表达式
};

} // namespace qindb

#endif // QINDB_TOKENIZER_H
