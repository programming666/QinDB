#include "qindb/generic_bplustree.h"
#include "qindb/bplus_tree.h"  // 包含BPlusTreePageHeader定义
#include "qindb/logger.h"
#include <QDataStream>
#include <algorithm>

namespace qindb {

// ============ 构造函数和析构函数 ============

GenericBPlusTree::GenericBPlusTree(BufferPoolManager* bufferPoolManager,
                                   DataType keyType,
                                   PageId rootPageId,
                                   int maxKeysPerPage)
    : bufferPoolManager_(bufferPoolManager)
    , keyType_(keyType)
    , rootPageId_(rootPageId)
    , maxKeysPerPage_(maxKeysPerPage)
{
    if (rootPageId_ == INVALID_PAGE_ID) {
        // 创建新的根节点（初始为空叶子节点）
        PageId newPageId;
        Page* rootPage = bufferPoolManager_->newPage(&newPageId);
        if (rootPage) {
            initializeLeafPage(rootPage, newPageId);
            rootPageId_ = newPageId;
            bufferPoolManager_->unpinPage(newPageId, true);
            LOG_INFO(QString("Created new B+ tree root page: %1, keyType: %2")
                        .arg(rootPageId_)
                        .arg(getDataTypeName(keyType_)));
        } else {
            LOG_ERROR("Failed to create root page for B+ tree");
        }
    }
}

GenericBPlusTree::~GenericBPlusTree() {
    // 析构函数不需要做特殊处理，页面由缓冲池管理
}

// ============ 键序列化/反序列化 ============

QByteArray GenericBPlusTree::serializeKey(const QVariant& key) {
    QByteArray result;
    if (!TypeSerializer::serialize(key, keyType_, result)) {
        LOG_ERROR(QString("Failed to serialize key of type %1").arg(getDataTypeName(keyType_)));
        return QByteArray();
    }
    return result;
}

QVariant GenericBPlusTree::deserializeKey(const QByteArray& serializedKey) {
    QVariant result;
    if (!TypeSerializer::deserialize(serializedKey, keyType_, result)) {
        LOG_ERROR(QString("Failed to deserialize key of type %1").arg(getDataTypeName(keyType_)));
        return QVariant();
    }
    return result;
}

int GenericBPlusTree::compareKeys(const QByteArray& key1, const QByteArray& key2) {
    return KeyComparator::compareSerialized(key1, key2, keyType_);
}

// ============ 公共接口实现 ============

bool GenericBPlusTree::insert(const QVariant& key, RowId value) {
    QMutexLocker locker(&mutex_);

    if (key.isNull()) {
        LOG_ERROR("Cannot insert NULL key into B+ tree");
        return false;
    }

    QByteArray serializedKey = serializeKey(key);
    if (serializedKey.isEmpty()) {
        return false;
    }

    // 查找叶子节点
    PageId leafPageId = findLeafPage(serializedKey);
    if (leafPageId == INVALID_PAGE_ID) {
        LOG_ERROR("Failed to find leaf page for insertion");
        return false;
    }

    // 插入到叶子节点
    return insertIntoLeaf(leafPageId, serializedKey, value);
}

bool GenericBPlusTree::search(const QVariant& key, RowId& value) {
    QMutexLocker locker(&mutex_);

    if (key.isNull()) {
        return false;
    }

    QByteArray serializedKey = serializeKey(key);
    if (serializedKey.isEmpty()) {
        return false;
    }

    // 查找叶子节点
    PageId leafPageId = findLeafPage(serializedKey);
    if (leafPageId == INVALID_PAGE_ID) {
        return false;
    }

    // 在叶子节点中查找
    Page* page = bufferPoolManager_->fetchPage(leafPageId);
    if (!page) {
        return false;
    }

    QVector<KeyValuePair> entries;
    bool success = readLeafEntries(page, entries);
    bufferPoolManager_->unpinPage(leafPageId, false);

    if (!success) {
        return false;
    }

    // 二分查找
    int pos = findKeyPositionInLeaf(entries, serializedKey);
    if (pos < entries.size() && compareKeys(entries[pos].serializedKey, serializedKey) == 0) {
        value = entries[pos].value;
        return true;
    }

    return false;
}

bool GenericBPlusTree::remove(const QVariant& key) {
    QMutexLocker locker(&mutex_);

    if (key.isNull()) {
        LOG_ERROR("Cannot remove NULL key from B+ tree");
        return false;
    }

    QByteArray serializedKey = serializeKey(key);
    if (serializedKey.isEmpty()) {
        return false;
    }

    // 1. 查找包含键的叶子节点
    PageId leafPageId = findLeafPage(serializedKey);
    if (leafPageId == INVALID_PAGE_ID) {
        LOG_ERROR("Failed to find leaf page for deletion");
        return false;
    }

    // 2. 从叶子节点删除键
    if (!deleteKeyFromLeaf(leafPageId, serializedKey)) {
        return false;
    }

    // 3. 检查是否下溢并处理
    Page* leafPage = bufferPoolManager_->fetchPage(leafPageId);
    if (!leafPage) {
        return false;
    }

    bool underflow = isUnderflow(leafPage);
    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(leafPage->getData());
    PageId parentPageId = header->parentPageId;
    bufferPoolManager_->unpinPage(leafPageId, false);

    if (underflow && parentPageId != INVALID_PAGE_ID) {
        // 处理下溢（递归处理父节点）
        handleUnderflow(leafPageId, parentPageId);
    }

    // 4. 更新根节点（如果根为空）
    updateRootIfEmpty();

    LOG_DEBUG(QString("Successfully removed key from B+ tree"));
    return true;
}

bool GenericBPlusTree::rangeSearch(const QVariant& minKey, const QVariant& maxKey,
                                  QVector<QPair<QVariant, RowId>>& results) {
    QMutexLocker locker(&mutex_);

    results.clear();

    QByteArray serializedMinKey = serializeKey(minKey);
    QByteArray serializedMaxKey = serializeKey(maxKey);

    if (serializedMinKey.isEmpty() || serializedMaxKey.isEmpty()) {
        return false;
    }

    // 查找起始叶子节点
    PageId leafPageId = findLeafPage(serializedMinKey);
    if (leafPageId == INVALID_PAGE_ID) {
        return false;
    }

    // 遍历叶子节点链表
    while (leafPageId != INVALID_PAGE_ID) {
        Page* page = bufferPoolManager_->fetchPage(leafPageId);
        if (!page) {
            break;
        }

        BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
        QVector<KeyValuePair> entries;
        bool success = readLeafEntries(page, entries);
        PageId nextPageId = header->nextPageId;

        // 优化: 预取下一页到缓冲池中,减少后续I/O等待
        if (nextPageId != INVALID_PAGE_ID) {
            Page* nextPage = bufferPoolManager_->fetchPage(nextPageId);
            if (nextPage) {
                bufferPoolManager_->unpinPage(nextPageId, false);
            }
        }

        bufferPoolManager_->unpinPage(leafPageId, false);

        if (!success) {
            break;
        }

        // 收集范围内的键值对
        bool shouldStop = false;
        for (const auto& entry : entries) {
            int cmpMin = compareKeys(entry.serializedKey, serializedMinKey);
            int cmpMax = compareKeys(entry.serializedKey, serializedMaxKey);

            if (cmpMin >= 0 && cmpMax <= 0) {
                // 在范围内
                QVariant key = deserializeKey(entry.serializedKey);
                results.append(qMakePair(key, entry.value));
            } else if (cmpMax > 0) {
                // 已超过最大值，停止搜索
                shouldStop = true;
                break;
            }
        }

        if (shouldStop) {
            return true;
        }

        leafPageId = nextPageId;
    }

    return true;
}

// ============ 内部辅助函数 ============

PageId GenericBPlusTree::findLeafPage(const QByteArray& serializedKey) {
    PageId currentPageId = rootPageId_;

    while (currentPageId != INVALID_PAGE_ID) {
        Page* page = bufferPoolManager_->fetchPage(currentPageId);
        if (!page) {
            return INVALID_PAGE_ID;
        }

        BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());

        if (header->nodeType == BPlusTreeNodeType::LEAF_NODE) {
            bufferPoolManager_->unpinPage(currentPageId, false);
            return currentPageId;
        }

        // 内部节点：查找子节点
        QVector<InternalEntry> entries;
        PageId firstChild;
        bool success = readInternalEntries(page, entries, firstChild);
        bufferPoolManager_->unpinPage(currentPageId, false);

        if (!success) {
            return INVALID_PAGE_ID;
        }

        int pos = findChildPosition(entries, serializedKey);
        if (pos == 0) {
            currentPageId = firstChild;
        } else {
            currentPageId = entries[pos - 1].childPageId;
        }
    }

    return INVALID_PAGE_ID;
}

bool GenericBPlusTree::insertIntoLeaf(PageId leafPageId, const QByteArray& serializedKey, RowId value) {
    Page* page = bufferPoolManager_->fetchPage(leafPageId);
    if (!page) {
        return false;
    }

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    QVector<KeyValuePair> entries;

    if (!readLeafEntries(page, entries)) {
        bufferPoolManager_->unpinPage(leafPageId, false);
        return false;
    }

    // 查找插入位置
    int pos = findKeyPositionInLeaf(entries, serializedKey);

    // 检查键是否已存在
    if (pos < entries.size() && compareKeys(entries[pos].serializedKey, serializedKey) == 0) {
        // 键已存在，更新值
        entries[pos].value = value;
        bool success = writeLeafEntries(page, entries);
        bufferPoolManager_->unpinPage(leafPageId, success);
        return success;
    }

    // 插入新键值对
    entries.insert(pos, KeyValuePair(serializedKey, value));

    // 检查是否需要分裂
    if (entries.size() <= maxKeysPerPage_) {
        // 不需要分裂，直接写入
        bool success = writeLeafEntries(page, entries);
        bufferPoolManager_->unpinPage(leafPageId, success);
        return success;
    }

    // 需要分裂
    // 关键修复：先将新插入的键值对写入页面，然后再分裂
    bool success = writeLeafEntries(page, entries);
    bufferPoolManager_->unpinPage(leafPageId, success);
    if (!success) {
        return false;
    }

    PageId newLeafPageId;
    QByteArray middleKey;
    if (!splitLeafNode(leafPageId, newLeafPageId, middleKey)) {
        return false;
    }

    // 更新父节点
    page = bufferPoolManager_->fetchPage(leafPageId);
    if (!page) {
        return false;
    }

    header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    PageId parentPageId = header->parentPageId;
    bufferPoolManager_->unpinPage(leafPageId, false);

    if (parentPageId == INVALID_PAGE_ID) {
        // 需要创建新根节点
        rootPageId_ = createNewRoot(leafPageId, middleKey, newLeafPageId);
        return rootPageId_ != INVALID_PAGE_ID;
    } else {
        // 插入到父节点
        return insertIntoParent(parentPageId, middleKey, leafPageId, newLeafPageId);
    }
}

bool GenericBPlusTree::splitLeafNode(PageId leafPageId, PageId& newLeafPageId, QByteArray& middleKey) {
    // 读取原叶子节点
    Page* oldPage = bufferPoolManager_->fetchPage(leafPageId);
    if (!oldPage) {
        return false;
    }

    BPlusTreePageHeader* oldHeader = reinterpret_cast<BPlusTreePageHeader*>(oldPage->getData());
    QVector<KeyValuePair> entries;

    if (!readLeafEntries(oldPage, entries)) {
        bufferPoolManager_->unpinPage(leafPageId, false);
        return false;
    }

    PageId parentPageId = oldHeader->parentPageId;
    PageId nextPageId = oldHeader->nextPageId;
    bufferPoolManager_->unpinPage(leafPageId, false);

    // 创建新叶子节点
    Page* newPage = bufferPoolManager_->newPage(&newLeafPageId);
    if (!newPage) {
        return false;
    }

    initializeLeafPage(newPage, newLeafPageId);

    // 分裂：前一半留在旧节点，后一半放到新节点
    int mid = entries.size() / 2;
    QVector<KeyValuePair> leftEntries = entries.mid(0, mid);
    QVector<KeyValuePair> rightEntries = entries.mid(mid);

    // 中间键（右半部分的第一个键）
    middleKey = rightEntries.first().serializedKey;

    // 写入新节点
    if (!writeLeafEntries(newPage, rightEntries)) {
        bufferPoolManager_->unpinPage(newLeafPageId, false);
        bufferPoolManager_->deletePage(newLeafPageId);
        return false;
    }

    // 更新新节点的链接
    BPlusTreePageHeader* newHeader = reinterpret_cast<BPlusTreePageHeader*>(newPage->getData());
    newHeader->parentPageId = parentPageId;
    newHeader->nextPageId = nextPageId;
    newHeader->prevPageId = leafPageId;
    bufferPoolManager_->unpinPage(newLeafPageId, true);

    // 更新旧节点
    oldPage = bufferPoolManager_->fetchPage(leafPageId);
    if (!oldPage) {
        return false;
    }

    if (!writeLeafEntries(oldPage, leftEntries)) {
        bufferPoolManager_->unpinPage(leafPageId, false);
        return false;
    }

    oldHeader = reinterpret_cast<BPlusTreePageHeader*>(oldPage->getData());
    oldHeader->nextPageId = newLeafPageId;
    bufferPoolManager_->unpinPage(leafPageId, true);

    // 更新下一个叶子节点的 prevPageId
    if (nextPageId != INVALID_PAGE_ID) {
        Page* nextPage = bufferPoolManager_->fetchPage(nextPageId);
        if (nextPage) {
            BPlusTreePageHeader* nextHeader = reinterpret_cast<BPlusTreePageHeader*>(nextPage->getData());
            nextHeader->prevPageId = newLeafPageId;
            bufferPoolManager_->unpinPage(nextPageId, true);
        }
    }

    LOG_DEBUG(QString("Split leaf node %1, created new leaf %2, middle key size: %3")
                 .arg(leafPageId).arg(newLeafPageId).arg(middleKey.size()));

    return true;
}

bool GenericBPlusTree::splitInternalNode(PageId internalPageId, PageId& newInternalPageId, QByteArray& middleKey) {
    // 读取原内部节点
    Page* oldPage = bufferPoolManager_->fetchPage(internalPageId);
    if (!oldPage) {
        return false;
    }

    BPlusTreePageHeader* oldHeader = reinterpret_cast<BPlusTreePageHeader*>(oldPage->getData());
    QVector<InternalEntry> entries;
    PageId firstChild;

    if (!readInternalEntries(oldPage, entries, firstChild)) {
        bufferPoolManager_->unpinPage(internalPageId, false);
        return false;
    }

    PageId parentPageId = oldHeader->parentPageId;
    bufferPoolManager_->unpinPage(internalPageId, false);

    // 创建新内部节点
    Page* newPage = bufferPoolManager_->newPage(&newInternalPageId);
    if (!newPage) {
        return false;
    }

    initializeInternalPage(newPage, newInternalPageId);

    // 分裂：前一半留在旧节点，后一半放到新节点
    int mid = entries.size() / 2;

    // 中间键会被提升到父节点
    middleKey = entries[mid].serializedKey;

    QVector<InternalEntry> leftEntries = entries.mid(0, mid);
    QVector<InternalEntry> rightEntries = entries.mid(mid + 1);
    PageId newFirstChild = entries[mid].childPageId;

    // 写入新节点
    if (!writeInternalEntries(newPage, rightEntries, newFirstChild)) {
        bufferPoolManager_->unpinPage(newInternalPageId, false);
        bufferPoolManager_->deletePage(newInternalPageId);
        return false;
    }

    BPlusTreePageHeader* newHeader = reinterpret_cast<BPlusTreePageHeader*>(newPage->getData());
    newHeader->parentPageId = parentPageId;
    bufferPoolManager_->unpinPage(newInternalPageId, true);

    // 更新旧节点
    oldPage = bufferPoolManager_->fetchPage(internalPageId);
    if (!oldPage) {
        return false;
    }

    if (!writeInternalEntries(oldPage, leftEntries, firstChild)) {
        bufferPoolManager_->unpinPage(internalPageId, false);
        return false;
    }

    bufferPoolManager_->unpinPage(internalPageId, true);

    // 更新子节点的父指针
    // 左侧子节点
    for (const auto& entry : leftEntries) {
        Page* childPage = bufferPoolManager_->fetchPage(entry.childPageId);
        if (childPage) {
            BPlusTreePageHeader* childHeader = reinterpret_cast<BPlusTreePageHeader*>(childPage->getData());
            childHeader->parentPageId = internalPageId;
            bufferPoolManager_->unpinPage(entry.childPageId, true);
        }
    }

    // 右侧子节点
    Page* firstChildPage = bufferPoolManager_->fetchPage(newFirstChild);
    if (firstChildPage) {
        BPlusTreePageHeader* childHeader = reinterpret_cast<BPlusTreePageHeader*>(firstChildPage->getData());
        childHeader->parentPageId = newInternalPageId;
        bufferPoolManager_->unpinPage(newFirstChild, true);
    }

    for (const auto& entry : rightEntries) {
        Page* childPage = bufferPoolManager_->fetchPage(entry.childPageId);
        if (childPage) {
            BPlusTreePageHeader* childHeader = reinterpret_cast<BPlusTreePageHeader*>(childPage->getData());
            childHeader->parentPageId = newInternalPageId;
            bufferPoolManager_->unpinPage(entry.childPageId, true);
        }
    }

    LOG_DEBUG(QString("Split internal node %1, created new internal %2")
                 .arg(internalPageId).arg(newInternalPageId));

    return true;
}

PageId GenericBPlusTree::createNewRoot(PageId leftPageId, const QByteArray& key, PageId rightPageId) {
    PageId newRootPageId;
    Page* newRootPage = bufferPoolManager_->newPage(&newRootPageId);
    if (!newRootPage) {
        return INVALID_PAGE_ID;
    }

    initializeInternalPage(newRootPage, newRootPageId);

    // 新根节点包含一个键和两个子节点
    QVector<InternalEntry> entries;
    entries.append(InternalEntry(key, rightPageId));

    if (!writeInternalEntries(newRootPage, entries, leftPageId)) {
        bufferPoolManager_->unpinPage(newRootPageId, false);
        bufferPoolManager_->deletePage(newRootPageId);
        return INVALID_PAGE_ID;
    }

    bufferPoolManager_->unpinPage(newRootPageId, true);

    // 更新左右子节点的父指针
    Page* leftPage = bufferPoolManager_->fetchPage(leftPageId);
    if (leftPage) {
        BPlusTreePageHeader* leftHeader = reinterpret_cast<BPlusTreePageHeader*>(leftPage->getData());
        leftHeader->parentPageId = newRootPageId;
        bufferPoolManager_->unpinPage(leftPageId, true);
    }

    Page* rightPage = bufferPoolManager_->fetchPage(rightPageId);
    if (rightPage) {
        BPlusTreePageHeader* rightHeader = reinterpret_cast<BPlusTreePageHeader*>(rightPage->getData());
        rightHeader->parentPageId = newRootPageId;
        bufferPoolManager_->unpinPage(rightPageId, true);
    }

    LOG_DEBUG(QString("Created new root page %1").arg(newRootPageId));

    return newRootPageId;
}

bool GenericBPlusTree::insertIntoParent(PageId parentPageId, const QByteArray& key,
                                       PageId leftPageId, PageId rightPageId) {
    Page* page = bufferPoolManager_->fetchPage(parentPageId);
    if (!page) {
        return false;
    }

    QVector<InternalEntry> entries;
    PageId firstChild;

    if (!readInternalEntries(page, entries, firstChild)) {
        bufferPoolManager_->unpinPage(parentPageId, false);
        return false;
    }

    bufferPoolManager_->unpinPage(parentPageId, false);

    // 查找插入位置
    int pos = findChildPosition(entries, key);

    // 插入新条目
    entries.insert(pos, InternalEntry(key, rightPageId));

    // 检查是否需要分裂
    if (entries.size() <= maxKeysPerPage_) {
        // 不需要分裂，直接写入
        page = bufferPoolManager_->fetchPage(parentPageId);
        if (!page) {
            return false;
        }

        bool success = writeInternalEntries(page, entries, firstChild);
        bufferPoolManager_->unpinPage(parentPageId, success);
        return success;
    }

    // 需要分裂内部节点
    // 关键修复：先将新插入的条目写入页面，然后再分裂
    page = bufferPoolManager_->fetchPage(parentPageId);
    if (!page) {
        return false;
    }

    if (!writeInternalEntries(page, entries, firstChild)) {
        bufferPoolManager_->unpinPage(parentPageId, false);
        return false;
    }
    bufferPoolManager_->unpinPage(parentPageId, true);

    PageId newInternalPageId;
    QByteArray middleKey;

    if (!splitInternalNode(parentPageId, newInternalPageId, middleKey)) {
        return false;
    }

    // 获取祖父节点
    page = bufferPoolManager_->fetchPage(parentPageId);
    if (!page) {
        return false;
    }

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    PageId grandParentPageId = header->parentPageId;
    bufferPoolManager_->unpinPage(parentPageId, false);

    if (grandParentPageId == INVALID_PAGE_ID) {
        // 需要创建新根节点
        rootPageId_ = createNewRoot(parentPageId, middleKey, newInternalPageId);
        return rootPageId_ != INVALID_PAGE_ID;
    } else {
        // 递归插入到祖父节点
        return insertIntoParent(grandParentPageId, middleKey, parentPageId, newInternalPageId);
    }
}

// ============ 页面初始化 ============

void GenericBPlusTree::initializeLeafPage(Page* page, PageId pageId) {
    page->reset();

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    header->nodeType = BPlusTreeNodeType::LEAF_NODE;
    header->numKeys = 0;
    header->maxKeys = maxKeysPerPage_;
    header->pageId = pageId;
    header->parentPageId = INVALID_PAGE_ID;
    header->nextPageId = INVALID_PAGE_ID;
    header->prevPageId = INVALID_PAGE_ID;
}

void GenericBPlusTree::initializeInternalPage(Page* page, PageId pageId) {
    page->reset();

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    header->nodeType = BPlusTreeNodeType::INTERNAL_NODE;
    header->numKeys = 0;
    header->maxKeys = maxKeysPerPage_;
    header->pageId = pageId;
    header->parentPageId = INVALID_PAGE_ID;
    header->nextPageId = INVALID_PAGE_ID;
    header->prevPageId = INVALID_PAGE_ID;
}

// ============ 页面读写 ============

bool GenericBPlusTree::readLeafEntries(Page* page, QVector<KeyValuePair>& entries) {
    entries.clear();

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    if (header->nodeType != BPlusTreeNodeType::LEAF_NODE) {
        LOG_ERROR("Trying to read leaf entries from non-leaf page");
        return false;
    }

    char* data = page->getData() + sizeof(BPlusTreePageHeader);
    QDataStream stream(QByteArray::fromRawData(data, PAGE_SIZE - sizeof(BPlusTreePageHeader)));
    stream.setByteOrder(QDataStream::LittleEndian);

    // 读取条目数量
    uint16_t numKeys;
    stream >> numKeys;

    // 读取每个键值对
    for (uint16_t i = 0; i < numKeys; ++i) {
        uint16_t keySize;
        stream >> keySize;

        if (keySize == 0 || keySize > 4096) {  // 限制键大小
            LOG_ERROR(QString("Invalid key size: %1").arg(keySize));
            return false;
        }

        QByteArray key(keySize, '\0');
        if (stream.readRawData(key.data(), keySize) != keySize) {
            LOG_ERROR("Failed to read key data");
            return false;
        }

        RowId value;
        stream >> value;

        entries.append(KeyValuePair(key, value));
    }

    return true;
}

bool GenericBPlusTree::writeLeafEntries(Page* page, const QVector<KeyValuePair>& entries) {
    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    if (header->nodeType != BPlusTreeNodeType::LEAF_NODE) {
        LOG_ERROR("Trying to write leaf entries to non-leaf page");
        return false;
    }

    // 计算所需空间
    int totalSize = sizeof(uint16_t);  // 条目数量
    for (const auto& entry : entries) {
        totalSize += sizeof(uint16_t);  // 键大小
        totalSize += entry.serializedKey.size();  // 键数据
        totalSize += sizeof(RowId);  // 值
    }

    if (totalSize > PAGE_SIZE - sizeof(BPlusTreePageHeader)) {
        LOG_ERROR(QString("Leaf entries too large: %1 bytes").arg(totalSize));
        return false;
    }

    // 写入数据
    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << static_cast<uint16_t>(entries.size());

    for (const auto& entry : entries) {
        stream << static_cast<uint16_t>(entry.serializedKey.size());
        stream.writeRawData(entry.serializedKey.constData(), entry.serializedKey.size());
        stream << entry.value;
    }

    // 复制到页面
    char* data = page->getData() + sizeof(BPlusTreePageHeader);
    memcpy(data, buffer.constData(), buffer.size());

    header->numKeys = entries.size();

    return true;
}

bool GenericBPlusTree::readInternalEntries(Page* page, QVector<InternalEntry>& entries, PageId& firstChild) {
    entries.clear();
    firstChild = INVALID_PAGE_ID;

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    if (header->nodeType != BPlusTreeNodeType::INTERNAL_NODE) {
        LOG_ERROR("Trying to read internal entries from non-internal page");
        return false;
    }

    char* data = page->getData() + sizeof(BPlusTreePageHeader);
    QDataStream stream(QByteArray::fromRawData(data, PAGE_SIZE - sizeof(BPlusTreePageHeader)));
    stream.setByteOrder(QDataStream::LittleEndian);

    // 读取第一个子节点
    stream >> firstChild;

    // 读取条目数量
    uint16_t numKeys;
    stream >> numKeys;

    // 读取每个条目
    for (uint16_t i = 0; i < numKeys; ++i) {
        uint16_t keySize;
        stream >> keySize;

        if (keySize == 0 || keySize > 4096) {
            LOG_ERROR(QString("Invalid key size: %1").arg(keySize));
            return false;
        }

        QByteArray key(keySize, '\0');
        if (stream.readRawData(key.data(), keySize) != keySize) {
            LOG_ERROR("Failed to read key data");
            return false;
        }

        PageId childPageId;
        stream >> childPageId;

        entries.append(InternalEntry(key, childPageId));
    }

    return true;
}

bool GenericBPlusTree::writeInternalEntries(Page* page, const QVector<InternalEntry>& entries, PageId firstChild) {
    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    if (header->nodeType != BPlusTreeNodeType::INTERNAL_NODE) {
        LOG_ERROR("Trying to write internal entries to non-internal page");
        return false;
    }

    // 计算所需空间
    int totalSize = sizeof(PageId) + sizeof(uint16_t);  // firstChild + 条目数量
    for (const auto& entry : entries) {
        totalSize += sizeof(uint16_t);  // 键大小
        totalSize += entry.serializedKey.size();  // 键数据
        totalSize += sizeof(PageId);  // 子页面ID
    }

    if (totalSize > PAGE_SIZE - sizeof(BPlusTreePageHeader)) {
        LOG_ERROR(QString("Internal entries too large: %1 bytes").arg(totalSize));
        return false;
    }

    // 写入数据
    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << firstChild;
    stream << static_cast<uint16_t>(entries.size());

    for (const auto& entry : entries) {
        stream << static_cast<uint16_t>(entry.serializedKey.size());
        stream.writeRawData(entry.serializedKey.constData(), entry.serializedKey.size());
        stream << entry.childPageId;
    }

    // 复制到页面
    char* data = page->getData() + sizeof(BPlusTreePageHeader);
    memcpy(data, buffer.constData(), buffer.size());

    header->numKeys = entries.size();

    return true;
}

// ============ 查找辅助函数 ============

int GenericBPlusTree::findKeyPositionInLeaf(const QVector<KeyValuePair>& entries, const QByteArray& key) {
    // 二分查找
    int left = 0;
    int right = entries.size();

    while (left < right) {
        int mid = (left + right) / 2;
        int cmp = compareKeys(entries[mid].serializedKey, key);

        if (cmp < 0) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return left;
}

int GenericBPlusTree::findChildPosition(const QVector<InternalEntry>& entries, const QByteArray& key) {
    // 查找应该进入哪个子节点
    int pos = 0;
    for (int i = 0; i < entries.size(); ++i) {
        if (compareKeys(key, entries[i].serializedKey) < 0) {
            break;
        }
        pos = i + 1;
    }
    return pos;
}

// ============ 统计和调试 ============

GenericBPlusTree::Stats GenericBPlusTree::getStats() const {
    QMutexLocker locker(&mutex_);

    Stats stats;
    stats.numKeys = 0;
    stats.numLeafPages = 0;
    stats.numInternalPages = 0;
    stats.treeHeight = 0;
    stats.totalKeySize = 0;

    // TODO: 实现完整的统计功能
    // 需要遍历整棵树

    return stats;
}

void GenericBPlusTree::printTree() const {
    QMutexLocker locker(&mutex_);

    LOG_INFO("=== B+ Tree Structure ===");
    LOG_INFO(QString("Key Type: %1").arg(getDataTypeName(keyType_)));
    LOG_INFO(QString("Root Page ID: %1").arg(rootPageId_));
    LOG_INFO(QString("Max Keys Per Page: %1").arg(maxKeysPerPage_));

    if (rootPageId_ != INVALID_PAGE_ID) {
        printTreeRecursive(rootPageId_, 0);
    }

    LOG_INFO("=========================");
}

void GenericBPlusTree::printTreeRecursive(PageId pageId, int level) const {
    Page* page = bufferPoolManager_->fetchPage(pageId);
    if (!page) {
        return;
    }

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    QString indent = QString(level * 2, ' ');

    if (header->nodeType == BPlusTreeNodeType::LEAF_NODE) {
        LOG_INFO(QString("%1Leaf Page %2 (numKeys=%3)")
                    .arg(indent).arg(pageId).arg(header->numKeys));
    } else {
        LOG_INFO(QString("%1Internal Page %2 (numKeys=%3)")
                    .arg(indent).arg(pageId).arg(header->numKeys));

        QVector<InternalEntry> entries;
        PageId firstChild;

        // 需要使用 const_cast 因为 readInternalEntries 需要非 const Page*
        if (const_cast<GenericBPlusTree*>(this)->readInternalEntries(page, entries, firstChild)) {
            bufferPoolManager_->unpinPage(pageId, false);

            // 打印第一个子节点
            printTreeRecursive(firstChild, level + 1);

            // 打印其他子节点
            for (const auto& entry : entries) {
                printTreeRecursive(entry.childPageId, level + 1);
            }
            return;
        }
    }

    bufferPoolManager_->unpinPage(pageId, false);
}

// ============ 删除操作辅助函数 ============

bool GenericBPlusTree::isUnderflow(Page* page) {
    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());

    // 根节点特殊处理
    if (header->parentPageId == INVALID_PAGE_ID) {
        // 根节点：如果是叶子节点，至少需要 0 个键（可以为空）
        // 如果是内部节点，至少需要 1 个键（2 个子节点）
        if (header->nodeType == BPlusTreeNodeType::LEAF_NODE) {
            return false;  // 根叶子节点可以为空
        } else {
            return header->numKeys < 1;  // 根内部节点至少 1 个键
        }
    }

    // 非根节点：检查是否小于最小键数
    bool isLeaf = (header->nodeType == BPlusTreeNodeType::LEAF_NODE);
    int minKeys = getMinKeys(isLeaf);
    return header->numKeys < minKeys;
}

int GenericBPlusTree::getMinKeys(bool isLeaf) {
    if (isLeaf) {
        // 叶子节点：⌈(maxKeys+1)/2⌉ - 1 = ⌈maxKeys/2⌉
        return (maxKeysPerPage_ + 1) / 2;
    } else {
        // 内部节点：⌈maxKeys/2⌉
        return (maxKeysPerPage_ + 1) / 2;
    }
}

bool GenericBPlusTree::deleteKeyFromLeaf(PageId leafPageId, const QByteArray& serializedKey) {
    Page* page = bufferPoolManager_->fetchPage(leafPageId);
    if (!page) {
        return false;
    }

    QVector<KeyValuePair> entries;
    if (!readLeafEntries(page, entries)) {
        bufferPoolManager_->unpinPage(leafPageId, false);
        return false;
    }

    // 查找要删除的键
    int pos = findKeyPositionInLeaf(entries, serializedKey);
    if (pos >= entries.size() || compareKeys(entries[pos].serializedKey, serializedKey) != 0) {
        // 键不存在
        bufferPoolManager_->unpinPage(leafPageId, false);
        LOG_WARN("Key not found in B+ tree for deletion");
        return false;
    }

    // 删除键
    entries.remove(pos);

    // 写回
    bool success = writeLeafEntries(page, entries);
    bufferPoolManager_->unpinPage(leafPageId, success);

    return success;
}

bool GenericBPlusTree::getSiblings(PageId nodePageId, PageId parentPageId,
                                   PageId& leftSiblingPageId, PageId& rightSiblingPageId,
                                   int& keyIndexInParent) {
    leftSiblingPageId = INVALID_PAGE_ID;
    rightSiblingPageId = INVALID_PAGE_ID;
    keyIndexInParent = -1;

    Page* parentPage = bufferPoolManager_->fetchPage(parentPageId);
    if (!parentPage) {
        return false;
    }

    QVector<InternalEntry> entries;
    PageId firstChild;
    if (!readInternalEntries(parentPage, entries, firstChild)) {
        bufferPoolManager_->unpinPage(parentPageId, false);
        return false;
    }

    // 查找当前节点在父节点中的位置
    if (firstChild == nodePageId) {
        // 当前节点是第一个子节点
        keyIndexInParent = 0;
        if (entries.size() > 0) {
            rightSiblingPageId = entries[0].childPageId;
        }
    } else {
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i].childPageId == nodePageId) {
                keyIndexInParent = i + 1;
                // 左兄弟
                if (i == 0) {
                    leftSiblingPageId = firstChild;
                } else {
                    leftSiblingPageId = entries[i - 1].childPageId;
                }
                // 右兄弟
                if (i + 1 < entries.size()) {
                    rightSiblingPageId = entries[i + 1].childPageId;
                }
                break;
            }
        }
    }

    bufferPoolManager_->unpinPage(parentPageId, false);
    return (keyIndexInParent >= 0);
}

bool GenericBPlusTree::borrowFromLeftSiblingLeaf(PageId nodePageId, PageId leftSiblingPageId,
                                                 PageId parentPageId, int keyIndexInParent) {
    // 读取左兄弟
    Page* leftPage = bufferPoolManager_->fetchPage(leftSiblingPageId);
    if (!leftPage) {
        return false;
    }

    QVector<KeyValuePair> leftEntries;
    if (!readLeafEntries(leftPage, leftEntries) || leftEntries.isEmpty()) {
        bufferPoolManager_->unpinPage(leftSiblingPageId, false);
        return false;
    }

    // 检查左兄弟是否有多余的键
    if (leftEntries.size() <= getMinKeys(true)) {
        bufferPoolManager_->unpinPage(leftSiblingPageId, false);
        return false;  // 左兄弟没有多余的键
    }

    bufferPoolManager_->unpinPage(leftSiblingPageId, false);

    // 读取当前节点
    Page* nodePage = bufferPoolManager_->fetchPage(nodePageId);
    if (!nodePage) {
        return false;
    }

    QVector<KeyValuePair> nodeEntries;
    if (!readLeafEntries(nodePage, nodeEntries)) {
        bufferPoolManager_->unpinPage(nodePageId, false);
        return false;
    }

    bufferPoolManager_->unpinPage(nodePageId, false);

    // 从左兄弟借用最后一个键
    KeyValuePair borrowedPair = leftEntries.last();
    leftEntries.removeLast();

    // 插入到当前节点开头
    nodeEntries.insert(0, borrowedPair);

    // 写回左兄弟
    leftPage = bufferPoolManager_->fetchPage(leftSiblingPageId);
    if (!leftPage || !writeLeafEntries(leftPage, leftEntries)) {
        if (leftPage) bufferPoolManager_->unpinPage(leftSiblingPageId, false);
        return false;
    }
    bufferPoolManager_->unpinPage(leftSiblingPageId, true);

    // 写回当前节点
    nodePage = bufferPoolManager_->fetchPage(nodePageId);
    if (!nodePage || !writeLeafEntries(nodePage, nodeEntries)) {
        if (nodePage) bufferPoolManager_->unpinPage(nodePageId, false);
        return false;
    }
    bufferPoolManager_->unpinPage(nodePageId, true);

    // 更新父节点中的分隔键（当前节点的第一个键）
    Page* parentPage = bufferPoolManager_->fetchPage(parentPageId);
    if (!parentPage) {
        return false;
    }

    QVector<InternalEntry> parentEntries;
    PageId firstChild;
    if (!readInternalEntries(parentPage, parentEntries, firstChild)) {
        bufferPoolManager_->unpinPage(parentPageId, false);
        return false;
    }

    // 更新分隔键
    if (keyIndexInParent > 0 && keyIndexInParent - 1 < parentEntries.size()) {
        parentEntries[keyIndexInParent - 1].serializedKey = nodeEntries.first().serializedKey;
    }

    bool success = writeInternalEntries(parentPage, parentEntries, firstChild);
    bufferPoolManager_->unpinPage(parentPageId, success);

    LOG_DEBUG(QString("Borrowed from left sibling: node=%1, leftSibling=%2")
                 .arg(nodePageId).arg(leftSiblingPageId));
    return true;
}

bool GenericBPlusTree::borrowFromRightSiblingLeaf(PageId nodePageId, PageId rightSiblingPageId,
                                                  PageId parentPageId, int keyIndexInParent) {
    // 读取右兄弟
    Page* rightPage = bufferPoolManager_->fetchPage(rightSiblingPageId);
    if (!rightPage) {
        return false;
    }

    QVector<KeyValuePair> rightEntries;
    if (!readLeafEntries(rightPage, rightEntries) || rightEntries.isEmpty()) {
        bufferPoolManager_->unpinPage(rightSiblingPageId, false);
        return false;
    }

    // 检查右兄弟是否有多余的键
    if (rightEntries.size() <= getMinKeys(true)) {
        bufferPoolManager_->unpinPage(rightSiblingPageId, false);
        return false;
    }

    bufferPoolManager_->unpinPage(rightSiblingPageId, false);

    // 读取当前节点
    Page* nodePage = bufferPoolManager_->fetchPage(nodePageId);
    if (!nodePage) {
        return false;
    }

    QVector<KeyValuePair> nodeEntries;
    if (!readLeafEntries(nodePage, nodeEntries)) {
        bufferPoolManager_->unpinPage(nodePageId, false);
        return false;
    }

    bufferPoolManager_->unpinPage(nodePageId, false);

    // 从右兄弟借用第一个键
    KeyValuePair borrowedPair = rightEntries.first();
    rightEntries.removeFirst();

    // 插入到当前节点末尾
    nodeEntries.append(borrowedPair);

    // 写回右兄弟
    rightPage = bufferPoolManager_->fetchPage(rightSiblingPageId);
    if (!rightPage || !writeLeafEntries(rightPage, rightEntries)) {
        if (rightPage) bufferPoolManager_->unpinPage(rightSiblingPageId, false);
        return false;
    }
    bufferPoolManager_->unpinPage(rightSiblingPageId, true);

    // 写回当前节点
    nodePage = bufferPoolManager_->fetchPage(nodePageId);
    if (!nodePage || !writeLeafEntries(nodePage, nodeEntries)) {
        if (nodePage) bufferPoolManager_->unpinPage(nodePageId, false);
        return false;
    }
    bufferPoolManager_->unpinPage(nodePageId, true);

    // 更新父节点中的分隔键（右兄弟的第一个键）
    Page* parentPage = bufferPoolManager_->fetchPage(parentPageId);
    if (!parentPage) {
        return false;
    }

    QVector<InternalEntry> parentEntries;
    PageId firstChild;
    if (!readInternalEntries(parentPage, parentEntries, firstChild)) {
        bufferPoolManager_->unpinPage(parentPageId, false);
        return false;
    }

    // 更新分隔键
    if (keyIndexInParent < parentEntries.size()) {
        parentEntries[keyIndexInParent].serializedKey = rightEntries.first().serializedKey;
    }

    bool success = writeInternalEntries(parentPage, parentEntries, firstChild);
    bufferPoolManager_->unpinPage(parentPageId, success);

    LOG_DEBUG(QString("Borrowed from right sibling: node=%1, rightSibling=%2")
                 .arg(nodePageId).arg(rightSiblingPageId));
    return true;
}

bool GenericBPlusTree::mergeWithLeftSiblingLeaf(PageId nodePageId, PageId leftSiblingPageId,
                                                PageId parentPageId, int keyIndexInParent) {
    // 读取左兄弟
    Page* leftPage = bufferPoolManager_->fetchPage(leftSiblingPageId);
    if (!leftPage) {
        return false;
    }

    QVector<KeyValuePair> leftEntries;
    if (!readLeafEntries(leftPage, leftEntries)) {
        bufferPoolManager_->unpinPage(leftSiblingPageId, false);
        return false;
    }

    BPlusTreePageHeader* leftHeader = reinterpret_cast<BPlusTreePageHeader*>(leftPage->getData());
    PageId leftNext = leftHeader->nextPageId;
    bufferPoolManager_->unpinPage(leftSiblingPageId, false);

    // 读取当前节点
    Page* nodePage = bufferPoolManager_->fetchPage(nodePageId);
    if (!nodePage) {
        return false;
    }

    QVector<KeyValuePair> nodeEntries;
    if (!readLeafEntries(nodePage, nodeEntries)) {
        bufferPoolManager_->unpinPage(nodePageId, false);
        return false;
    }

    BPlusTreePageHeader* nodeHeader = reinterpret_cast<BPlusTreePageHeader*>(nodePage->getData());
    PageId nodeNext = nodeHeader->nextPageId;
    bufferPoolManager_->unpinPage(nodePageId, false);

    // 合并：将当前节点的所有键移到左兄弟
    leftEntries.append(nodeEntries);

    // 写回左兄弟
    leftPage = bufferPoolManager_->fetchPage(leftSiblingPageId);
    if (!leftPage || !writeLeafEntries(leftPage, leftEntries)) {
        if (leftPage) bufferPoolManager_->unpinPage(leftSiblingPageId, false);
        return false;
    }

    // 更新链表指针
    leftHeader = reinterpret_cast<BPlusTreePageHeader*>(leftPage->getData());
    leftHeader->nextPageId = nodeNext;
    bufferPoolManager_->unpinPage(leftSiblingPageId, true);

    // 更新 nodeNext 的 prevPageId
    if (nodeNext != INVALID_PAGE_ID) {
        Page* nextPage = bufferPoolManager_->fetchPage(nodeNext);
        if (nextPage) {
            BPlusTreePageHeader* nextHeader = reinterpret_cast<BPlusTreePageHeader*>(nextPage->getData());
            nextHeader->prevPageId = leftSiblingPageId;
            bufferPoolManager_->unpinPage(nodeNext, true);
        }
    }

    // 从父节点删除分隔键
    if (!deleteKeyFromInternal(parentPageId, nodeEntries.first().serializedKey)) {
        LOG_WARN("Failed to delete key from parent after merge");
    }

    // 释放当前节点页面
    bufferPoolManager_->deletePage(nodePageId);

    LOG_DEBUG(QString("Merged with left sibling: node=%1, leftSibling=%2")
                 .arg(nodePageId).arg(leftSiblingPageId));
    return true;
}

bool GenericBPlusTree::mergeWithRightSiblingLeaf(PageId nodePageId, PageId rightSiblingPageId,
                                                 PageId parentPageId, int keyIndexInParent) {
    // 简单实现：将右兄弟合并到当前节点
    // 读取当前节点
    Page* nodePage = bufferPoolManager_->fetchPage(nodePageId);
    if (!nodePage) {
        return false;
    }

    QVector<KeyValuePair> nodeEntries;
    if (!readLeafEntries(nodePage, nodeEntries)) {
        bufferPoolManager_->unpinPage(nodePageId, false);
        return false;
    }

    BPlusTreePageHeader* nodeHeader = reinterpret_cast<BPlusTreePageHeader*>(nodePage->getData());
    bufferPoolManager_->unpinPage(nodePageId, false);

    // 读取右兄弟
    Page* rightPage = bufferPoolManager_->fetchPage(rightSiblingPageId);
    if (!rightPage) {
        return false;
    }

    QVector<KeyValuePair> rightEntries;
    if (!readLeafEntries(rightPage, rightEntries)) {
        bufferPoolManager_->unpinPage(rightSiblingPageId, false);
        return false;
    }

    BPlusTreePageHeader* rightHeader = reinterpret_cast<BPlusTreePageHeader*>(rightPage->getData());
    PageId rightNext = rightHeader->nextPageId;
    bufferPoolManager_->unpinPage(rightSiblingPageId, false);

    // 合并
    nodeEntries.append(rightEntries);

    // 写回当前节点
    nodePage = bufferPoolManager_->fetchPage(nodePageId);
    if (!nodePage || !writeLeafEntries(nodePage, nodeEntries)) {
        if (nodePage) bufferPoolManager_->unpinPage(nodePageId, false);
        return false;
    }

    // 更新链表指针
    nodeHeader = reinterpret_cast<BPlusTreePageHeader*>(nodePage->getData());
    nodeHeader->nextPageId = rightNext;
    bufferPoolManager_->unpinPage(nodePageId, true);

    // 更新 rightNext 的 prevPageId
    if (rightNext != INVALID_PAGE_ID) {
        Page* nextPage = bufferPoolManager_->fetchPage(rightNext);
        if (nextPage) {
            BPlusTreePageHeader* nextHeader = reinterpret_cast<BPlusTreePageHeader*>(nextPage->getData());
            nextHeader->prevPageId = nodePageId;
            bufferPoolManager_->unpinPage(rightNext, true);
        }
    }

    // 从父节点删除分隔键
    if (!deleteKeyFromInternal(parentPageId, rightEntries.first().serializedKey)) {
        LOG_WARN("Failed to delete key from parent after merge");
    }

    // 释放右兄弟页面
    bufferPoolManager_->deletePage(rightSiblingPageId);

    LOG_DEBUG(QString("Merged with right sibling: node=%1, rightSibling=%2")
                 .arg(nodePageId).arg(rightSiblingPageId));
    return true;
}

bool GenericBPlusTree::deleteKeyFromInternal(PageId internalPageId, const QByteArray& serializedKey) {
    Page* page = bufferPoolManager_->fetchPage(internalPageId);
    if (!page) {
        return false;
    }

    QVector<InternalEntry> entries;
    PageId firstChild;
    if (!readInternalEntries(page, entries, firstChild)) {
        bufferPoolManager_->unpinPage(internalPageId, false);
        return false;
    }

    // 查找并删除键
    bool found = false;
    for (int i = 0; i < entries.size(); ++i) {
        if (compareKeys(entries[i].serializedKey, serializedKey) == 0) {
            entries.remove(i);
            found = true;
            break;
        }
    }

    if (!found) {
        bufferPoolManager_->unpinPage(internalPageId, false);
        return false;
    }

    // 写回
    bool success = writeInternalEntries(page, entries, firstChild);
    bufferPoolManager_->unpinPage(internalPageId, success);

    return success;
}

bool GenericBPlusTree::handleUnderflow(PageId nodePageId, PageId parentPageId) {
    // 获取兄弟节点
    PageId leftSiblingPageId, rightSiblingPageId;
    int keyIndexInParent;

    if (!getSiblings(nodePageId, parentPageId, leftSiblingPageId, rightSiblingPageId, keyIndexInParent)) {
        return false;
    }

    // 尝试从左兄弟借用
    if (leftSiblingPageId != INVALID_PAGE_ID) {
        if (borrowFromLeftSiblingLeaf(nodePageId, leftSiblingPageId, parentPageId, keyIndexInParent)) {
            return true;
        }
    }

    // 尝试从右兄弟借用
    if (rightSiblingPageId != INVALID_PAGE_ID) {
        if (borrowFromRightSiblingLeaf(nodePageId, rightSiblingPageId, parentPageId, keyIndexInParent)) {
            return true;
        }
    }

    // 无法借用，进行合并
    if (leftSiblingPageId != INVALID_PAGE_ID) {
        if (mergeWithLeftSiblingLeaf(nodePageId, leftSiblingPageId, parentPageId, keyIndexInParent)) {
            // 检查父节点是否下溢
            Page* parentPage = bufferPoolManager_->fetchPage(parentPageId);
            if (parentPage) {
                bool parentUnderflow = isUnderflow(parentPage);
                BPlusTreePageHeader* parentHeader = reinterpret_cast<BPlusTreePageHeader*>(parentPage->getData());
                PageId grandParentPageId = parentHeader->parentPageId;
                bufferPoolManager_->unpinPage(parentPageId, false);

                if (parentUnderflow && grandParentPageId != INVALID_PAGE_ID) {
                    handleUnderflow(parentPageId, grandParentPageId);
                }
            }
            return true;
        }
    } else if (rightSiblingPageId != INVALID_PAGE_ID) {
        if (mergeWithRightSiblingLeaf(nodePageId, rightSiblingPageId, parentPageId, keyIndexInParent)) {
            // 检查父节点是否下溢
            Page* parentPage = bufferPoolManager_->fetchPage(parentPageId);
            if (parentPage) {
                bool parentUnderflow = isUnderflow(parentPage);
                BPlusTreePageHeader* parentHeader = reinterpret_cast<BPlusTreePageHeader*>(parentPage->getData());
                PageId grandParentPageId = parentHeader->parentPageId;
                bufferPoolManager_->unpinPage(parentPageId, false);

                if (parentUnderflow && grandParentPageId != INVALID_PAGE_ID) {
                    handleUnderflow(parentPageId, grandParentPageId);
                }
            }
            return true;
        }
    }

    return false;
}

void GenericBPlusTree::updateRootIfEmpty() {
    if (rootPageId_ == INVALID_PAGE_ID) {
        return;
    }

    Page* rootPage = bufferPoolManager_->fetchPage(rootPageId_);
    if (!rootPage) {
        return;
    }

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(rootPage->getData());

    // 如果根节点是内部节点且只有一个子节点，提升该子节点为新根
    if (header->nodeType == BPlusTreeNodeType::INTERNAL_NODE && header->numKeys == 0) {
        QVector<InternalEntry> entries;
        PageId firstChild;
        if (readInternalEntries(rootPage, entries, firstChild)) {
            PageId oldRootId = rootPageId_;
            rootPageId_ = firstChild;

            // 更新新根的父指针
            Page* newRootPage = bufferPoolManager_->fetchPage(firstChild);
            if (newRootPage) {
                BPlusTreePageHeader* newRootHeader = reinterpret_cast<BPlusTreePageHeader*>(newRootPage->getData());
                newRootHeader->parentPageId = INVALID_PAGE_ID;
                bufferPoolManager_->unpinPage(firstChild, true);
            }

            bufferPoolManager_->unpinPage(oldRootId, false);
            bufferPoolManager_->deletePage(oldRootId);

            LOG_DEBUG(QString("Updated root from %1 to %2").arg(oldRootId).arg(rootPageId_));
            return;
        }
    }

    bufferPoolManager_->unpinPage(rootPageId_, false);
}

} // namespace qindb
