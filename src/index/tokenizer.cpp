#include "qindb/tokenizer.h"
#include "qindb/logger.h"
#include <QRegularExpression>
#include <QSet>

namespace qindb {

Tokenizer::Tokenizer(Mode mode, bool enableStopWords)
    : mode_(mode)
    , enableStopWords_(enableStopWords)
    , wordRegex_("\\b[a-zA-Z]+\\b")  // 匹配英文单词
{
    initializeDefaultStopWords();
    LOG_DEBUG(QString("Tokenizer created: mode=%1, stopWords=%2")
                 .arg(static_cast<int>(mode))
                 .arg(enableStopWords));
}

QStringList Tokenizer::tokenize(const QString& text) const {
    QStringList tokens;

    // 根据模式选择分词方法
    switch (mode_) {
    case Mode::ENGLISH:
        tokens = tokenizeEnglish(text);
        break;
    case Mode::CHINESE:
        tokens = tokenizeChinese(text);
        break;
    case Mode::MIXED:
        tokens = tokenizeMixed(text);
        break;
    }

    // 去重
    QSet<QString> uniqueTokens;
    for (const QString& token : tokens) {
        if (!token.isEmpty()) {
            uniqueTokens.insert(token);
        }
    }

    return uniqueTokens.values();
}

QStringList Tokenizer::tokenizeWithDuplicates(const QString& text) const {
    QStringList tokens;

    // 根据模式选择分词方法
    switch (mode_) {
    case Mode::ENGLISH:
        tokens = tokenizeEnglish(text);
        break;
    case Mode::CHINESE:
        tokens = tokenizeChinese(text);
        break;
    case Mode::MIXED:
        tokens = tokenizeMixed(text);
        break;
    }

    return tokens;
}

QString Tokenizer::normalize(const QString& term) {
    if (term.isEmpty()) {
        return QString();
    }

    QString normalized = term.trimmed().toLower();

    // 移除前后标点符号
    while (!normalized.isEmpty() && !normalized[0].isLetterOrNumber() && !isChineseChar(normalized[0])) {
        normalized = normalized.mid(1);
    }

    while (!normalized.isEmpty() && !normalized[normalized.length() - 1].isLetterOrNumber()
           && !isChineseChar(normalized[normalized.length() - 1])) {
        normalized.chop(1);
    }

    return normalized;
}

bool Tokenizer::isStopWord(const QString& term) const {
    if (!enableStopWords_) {
        return false;
    }

    QString normalized = normalize(term);
    return stopWords_.contains(normalized);
}

void Tokenizer::addStopWord(const QString& stopWord) {
    QString normalized = normalize(stopWord);
    if (!normalized.isEmpty()) {
        stopWords_.insert(normalized);
    }
}

void Tokenizer::removeStopWord(const QString& stopWord) {
    QString normalized = normalize(stopWord);
    stopWords_.remove(normalized);
}

QStringList Tokenizer::tokenizeEnglish(const QString& text) const {
    QStringList tokens;

    // 使用正则表达式匹配英文单词
    QRegularExpressionMatchIterator it = wordRegex_.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString word = match.captured(0);

        // 归一化
        QString normalized = normalize(word);

        // 过滤停用词和空词
        if (!normalized.isEmpty() && !isStopWord(normalized)) {
            tokens.append(normalized);
        }
    }

    return tokens;
}

QStringList Tokenizer::tokenizeChinese(const QString& text) const {
    QStringList tokens;

    // 简单单字分词
    for (int i = 0; i < text.length(); ++i) {
        QChar ch = text[i];

        if (isChineseChar(ch)) {
            QString token = QString(ch);

            // 过滤停用词
            if (!isStopWord(token)) {
                tokens.append(token);
            }
        }
    }

    return tokens;
}

QStringList Tokenizer::tokenizeMixed(const QString& text) const {
    QStringList tokens;

    // 先提取英文单词
    QStringList englishTokens = tokenizeEnglish(text);
    tokens.append(englishTokens);

    // 再提取中文字符
    for (int i = 0; i < text.length(); ++i) {
        QChar ch = text[i];

        if (isChineseChar(ch)) {
            QString token = QString(ch);

            // 过滤停用词
            if (!isStopWord(token)) {
                tokens.append(token);
            }
        }
    }

    return tokens;
}

void Tokenizer::initializeDefaultStopWords() {
    // 英文停用词（常见的100个）
    QStringList englishStopWords = {
        "a", "an", "and", "are", "as", "at", "be", "but", "by",
        "for", "if", "in", "into", "is", "it",
        "no", "not", "of", "on", "or", "such",
        "that", "the", "their", "then", "there", "these",
        "they", "this", "to", "was", "will", "with",
        "i", "you", "he", "she", "we", "they",
        "am", "is", "are", "was", "were", "been", "being",
        "have", "has", "had", "do", "does", "did",
        "can", "could", "may", "might", "must", "shall", "should", "will", "would",
        "about", "after", "all", "also", "any", "because", "before", "both",
        "each", "from", "has", "have", "her", "here", "him", "his", "how",
        "its", "just", "more", "most", "my", "now", "only", "other", "our",
        "out", "over", "same", "so", "some", "than", "them", "through",
        "up", "very", "what", "when", "where", "which", "who", "why", "your"
    };

    // 中文停��词（常见的50个）
    QStringList chineseStopWords = {
        "的", "了", "在", "是", "我", "有", "和", "就", "不", "人",
        "都", "一", "一个", "上", "也", "很", "到", "说", "要", "去",
        "你", "会", "着", "没有", "看", "好", "自己", "这", "那", "个",
        "们", "中", "来", "为", "能", "对", "生", "于", "子", "得",
        "出", "以", "里", "后", "自", "大", "多", "然", "可", "下"
    };

    for (const QString& word : englishStopWords) {
        stopWords_.insert(word);
    }

    for (const QString& word : chineseStopWords) {
        stopWords_.insert(word);
    }

    LOG_DEBUG(QString("Initialized %1 stop words").arg(stopWords_.size()));
}

bool Tokenizer::isChineseChar(QChar ch) {
    // Unicode 中文字符范围
    ushort unicode = ch.unicode();
    return (unicode >= 0x4E00 && unicode <= 0x9FFF) ||  // CJK 统一汉字
           (unicode >= 0x3400 && unicode <= 0x4DBF) ||  // CJK 扩展 A
           (unicode >= 0x20000 && unicode <= 0x2A6DF);  // CJK 扩展 B
}

} // namespace qindb
