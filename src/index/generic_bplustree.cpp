#include "qindb/generic_bplustree.h"  // 包含通用B+树类的定义
#include "qindb/bplus_tree.h"  // 包含BPlusTreePageHeader定义
#include "qindb/logger.h"  // 日志记录功能
#include <QDataStream>  // Qt数据流，用于序列化
#include <algorithm>  // 标准算法库

namespace qindb {

// ============ 构造函数和析构函数 ============

/**
 * @brief 通用B+树构造函数
 * @param bufferPoolManager 缓冲池管理器指针，用于页面的分配和管理
 * @param keyType 键的数据类型（如INT、VARCHAR等）
 * @param rootPageId 根节点页面ID，如果为INVALID_PAGE_ID则创建新树
 * @param maxKeysPerPage 每个页面最多存储的键数量
 *
 * 功能说明：
 * 1. 初始化B+树的基本参数
 * 2. 如果rootPageId无效，创建一个新的空叶子节点作为根节点
 * 3. 新创建的根节点会被初始化并记录到日志
 */
GenericBPlusTree::GenericBPlusTree(BufferPoolManager* bufferPoolManager,
                                   DataType keyType,
                                   PageId rootPageId,
                                   int maxKeysPerPage)
    : bufferPoolManager_(bufferPoolManager)  // 缓冲池管理器
    , keyType_(keyType)  // 键类型
    , rootPageId_(rootPageId)  // 根节点页面ID
    , maxKeysPerPage_(maxKeysPerPage)  // 每页最大键数
{
    if (rootPageId_ == INVALID_PAGE_ID) {
        // 创建新的根节点（初始为空叶子节点）
        PageId newPageId;
        Page* rootPage = bufferPoolManager_->newPage(&newPageId);
        if (rootPage) {
            // 初始化为叶子节点
            initializeLeafPage(rootPage, newPageId);
            rootPageId_ = newPageId;
            // 解除页面固定，标记为脏页（需要写回磁盘）
            bufferPoolManager_->unpinPage(newPageId, true);
            LOG_INFO(QString("Created new B+ tree root page: %1, keyType: %2")
                        .arg(rootPageId_)
                        .arg(getDataTypeName(keyType_)));
        } else {
            LOG_ERROR("Failed to create root page for B+ tree");
        }
    }
}

/**
 * @brief 析构函数
 *
 * 说明：不需要特殊处理，所有页面的生命周期由缓冲池管理器负责
 */
GenericBPlusTree::~GenericBPlusTree() {
    // 析构函数不需要做特殊处理，页面由缓冲池管理
}

// ============ 键序列化/反序列化 ============

/**
 * @brief 将QVariant类型的键序列化为字节数组
 * @param key 要序列化的键值
 * @return 序列化后的字节数组，失败返回空数组
 *
 * 说明：使用TypeSerializer根据键类型进行序列化
 */
QByteArray GenericBPlusTree::serializeKey(const QVariant& key) {
    QByteArray result;
    if (!TypeSerializer::serialize(key, keyType_, result)) {
        LOG_ERROR(QString("Failed to serialize key of type %1").arg(getDataTypeName(keyType_)));
        return QByteArray();
    }
    return result;
}

/**
 * @brief 将字节数组反序列化为QVariant类型的键
 * @param serializedKey 序列化的字节数组
 * @return 反序列化后的键值，失败返回空QVariant
 *
 * 说明：使用TypeSerializer根据键类型进行反序列化
 */
QVariant GenericBPlusTree::deserializeKey(const QByteArray& serializedKey) {
    QVariant result;
    if (!TypeSerializer::deserialize(serializedKey, keyType_, result)) {
        LOG_ERROR(QString("Failed to deserialize key of type %1").arg(getDataTypeName(keyType_)));
        return QVariant();
    }
    return result;
}

/**
 * @brief 比较两个序列化的键
 * @param key1 第一个键的字节数组
 * @param key2 第二个键的字节数组
 * @return 比较结果：< 0表示key1 < key2，0表示相等，> 0表示key1 > key2
 *
 * 说明：使用KeyComparator根据键类型进行比较
 */
int GenericBPlusTree::compareKeys(const QByteArray& key1, const QByteArray& key2) {
    return KeyComparator::compareSerialized(key1, key2, keyType_);
}

// ============ 公共接口实现 ============

/**
 * @brief 向B+树插入键值对
 * @param key 要插入的键
 * @param value 对应的行ID（RowId）
 * @return 插入成功返回true，失败返回false
 *
 * 功能流程：
 * 1. 加互斥锁保证线程安全
 * 2. 验证键的有效性（不能为NULL）
 * 3. 序列化键为字节数组
 * 4. 查找应该插入的叶子节点
 * 5. 调用insertIntoLeaf执行实际插入操作
 *
 * 注意：如果键已存在，会更新对应的值
 */
bool GenericBPlusTree::insert(const QVariant& key, RowId value) {
    QMutexLocker locker(&mutex_);  // 自动加锁，离开作用域自动解锁

    if (key.isNull()) {
        LOG_ERROR("Cannot insert NULL key into B+ tree");
        return false;
    }

    // 将键序列化为字节数组
    QByteArray serializedKey = serializeKey(key);
    if (serializedKey.isEmpty()) {
        return false;
    }

    // 查找应该插入的叶子节点
    PageId leafPageId = findLeafPage(serializedKey);
    if (leafPageId == INVALID_PAGE_ID) {
        LOG_ERROR("Failed to find leaf page for insertion");
        return false;
    }

    // 插入到叶子节点（可能触发节点分裂）
    return insertIntoLeaf(leafPageId, serializedKey, value);
}

/**
 * @brief 在B+树中查找键对应的值
 * @param key 要查找的键
 * @param value 输出参数，找到时存储对应的行ID
 * @return 找到返回true，未找到返回false
 *
 * 功能流程：
 * 1. 加互斥锁保证线程安全
 * 2. 验证键的有效性
 * 3. 序列化键
 * 4. 查找包含该键的叶子节点
 * 5. 读取叶子节点的所有条目
 * 6. 使用二分查找定位键的位置
 * 7. 如果找到，返回对应的值
 */
bool GenericBPlusTree::search(const QVariant& key, RowId& value) {
    QMutexLocker locker(&mutex_);  // 线程安全锁

    if (key.isNull()) {
        return false;
    }

    // 序列化键
    QByteArray serializedKey = serializeKey(key);
    if (serializedKey.isEmpty()) {
        return false;
    }

    // 查找包含该键的叶子节点
    PageId leafPageId = findLeafPage(serializedKey);
    if (leafPageId == INVALID_PAGE_ID) {
        return false;
    }

    // 从缓冲池获取叶子节点页面
    Page* page = bufferPoolManager_->fetchPage(leafPageId);
    if (!page) {
        return false;
    }

    // 读取叶子节点的所有键值对
    QVector<KeyValuePair> entries;
    bool success = readLeafEntries(page, entries);
    bufferPoolManager_->unpinPage(leafPageId, false);  // 解除固定，不标记为脏页

    if (!success) {
        return false;
    }

    // 使用二分查找定位键
    int pos = findKeyPositionInLeaf(entries, serializedKey);
    if (pos < entries.size() && compareKeys(entries[pos].serializedKey, serializedKey) == 0) {
        value = entries[pos].value;  // 找到，返回对应的值
        return true;
    }

    return false;  // 未找到
}

/**
 * @brief 从B+树中删除键
 * @param key 要删除的键
 * @return 删除成功返回true，失败返回false
 *
 * 功能流程：
 * 1. 加互斥锁保证线程安全
 * 2. 验证键的有效性
 * 3. 序列化键
 * 4. 查找包含该键的叶子节点
 * 5. 从叶子节点删除键
 * 6. 检查是否发生下溢（键数量少于最小值）
 * 7. 如果下溢，通过借用或合并操作恢复B+树性质
 * 8. 更新根节点（如果根节点变空）
 *
 * 注意：删除操作可能触发节点合并和树高度减少
 */
bool GenericBPlusTree::remove(const QVariant& key) {
    QMutexLocker locker(&mutex_);  // 线程安全锁

    if (key.isNull()) {
        LOG_ERROR("Cannot remove NULL key from B+ tree");
        return false;
    }

    // 序列化键
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

    // 3. 检查是否下溢（键数量少于最小要求）
    Page* leafPage = bufferPoolManager_->fetchPage(leafPageId);
    if (!leafPage) {
        return false;
    }

    bool underflow = isUnderflow(leafPage);  // 检查是否下溢
    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(leafPage->getData());
    PageId parentPageId = header->parentPageId;
    bufferPoolManager_->unpinPage(leafPageId, false);

    if (underflow && parentPageId != INVALID_PAGE_ID) {
        // 处理下溢：尝试从兄弟节点借用或与兄弟节点合并
        // 这个过程可能递归向上传播到父节点
        handleUnderflow(leafPageId, parentPageId);
    }

    // 4. 更新根节点（如果根节点只剩一个子节点，提升子节点为新根）
    updateRootIfEmpty();

    LOG_DEBUG(QString("Successfully removed key from B+ tree"));
    return true;
}

/**
 * @brief 范围查询：查找指定范围内的所有键值对
 * @param minKey 范围的最小键（包含）
 * @param maxKey 范围的最大键（包含）
 * @param results 输出参数，存储查询结果的键值对列表
 * @return 查询成功返回true，失败返回false
 *
 * 功能流程：
 * 1. 加互斥锁保证线程安全
 * 2. 序列化最小键和最大键
 * 3. 查找包含最小键的起始叶子节点
 * 4. 沿着叶子节点链表向右遍历
 * 5. 收集范围内的所有键值对
 * 6. 当遇到超过最大键的值时停止
 *
 * 优化：使用预取技术，提前加载下一个叶子节点到缓冲池
 *
 * 注意：B+树的叶子节点通过链表连接，支持高效的范围查询
 */
bool GenericBPlusTree::rangeSearch(const QVariant& minKey, const QVariant& maxKey,
                                  QVector<QPair<QVariant, RowId>>& results) {
    QMutexLocker locker(&mutex_);  // 线程安全锁

    results.clear();  // 清空结果集

    // 序列化最小键和最大键
    QByteArray serializedMinKey = serializeKey(minKey);
    QByteArray serializedMaxKey = serializeKey(maxKey);

    if (serializedMinKey.isEmpty() || serializedMaxKey.isEmpty()) {
        return false;
    }

    // 查找包含最小键的起始叶子节点
    PageId leafPageId = findLeafPage(serializedMinKey);
    if (leafPageId == INVALID_PAGE_ID) {
        return false;
    }

    // 沿着叶子节点链表向右遍历，收集范围内的所有键值对
    while (leafPageId != INVALID_PAGE_ID) {
        Page* page = bufferPoolManager_->fetchPage(leafPageId);
        if (!page) {
            break;
        }

        BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
        QVector<KeyValuePair> entries;
        bool success = readLeafEntries(page, entries);
        PageId nextPageId = header->nextPageId;  // 获取下一个叶子节点的ID

        // 性能优化：预取下一页到缓冲池中，减少后续I/O等待时间
        // 这是一种常见的数据库优化技术
        if (nextPageId != INVALID_PAGE_ID) {
            Page* nextPage = bufferPoolManager_->fetchPage(nextPageId);
            if (nextPage) {
                bufferPoolManager_->unpinPage(nextPageId, false);  // 立即解除固定
            }
        }

        bufferPoolManager_->unpinPage(leafPageId, false);

        if (!success) {
            break;
        }

        // 遍历当前叶子节点的所有条目，收集范围内的键值对
        bool shouldStop = false;
        for (const auto& entry : entries) {
            int cmpMin = compareKeys(entry.serializedKey, serializedMinKey);
            int cmpMax = compareKeys(entry.serializedKey, serializedMaxKey);

            if (cmpMin >= 0 && cmpMax <= 0) {
                // 键在范围内 [minKey, maxKey]
                QVariant key = deserializeKey(entry.serializedKey);
                results.append(qMakePair(key, entry.value));
            } else if (cmpMax > 0) {
                // 键已超过最大值，停止搜索（因为后续的键更大）
                shouldStop = true;
                break;
            }
            // 如果 cmpMin < 0，说明键小于最小值，继续查找
        }

        if (shouldStop) {
            return true;  // 已找到所有范围内的键
        }

        leafPageId = nextPageId;  // 移动到下一个叶子节点
    }

    return true;
}

// ============ 内部辅助函数 ============

/**
 * @brief 查找包含指定键的叶子节点
 * @param serializedKey 序列化后的键
 * @return 叶子节点的页面ID，失败返回INVALID_PAGE_ID
 *
 * 功能流程：
 * 1. 从根节点开始
 * 2. 如果当前节点是叶子节点，返回其ID
 * 3. 如果是内部节点，根据键值确定应该进入哪个子节点
 * 4. 重复步骤2-3，直到找到叶子节点
 *
 * 算法说明：
 * - 内部节点存储分隔键和子节点指针
 * - 通过比较键值，决定向左还是向右子树搜索
 * - 时间复杂度：O(log n)，其中n是树中的键数量
 */
PageId GenericBPlusTree::findLeafPage(const QByteArray& serializedKey) {
    PageId currentPageId = rootPageId_;  // 从根节点开始

    // 从根节点向下遍历，直到找到叶子节点
    while (currentPageId != INVALID_PAGE_ID) {
        Page* page = bufferPoolManager_->fetchPage(currentPageId);
        if (!page) {
            return INVALID_PAGE_ID;
        }

        BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());

        // 如果是叶子节点，找到目标
        if (header->nodeType == BPlusTreeNodeType::LEAF_NODE) {
            bufferPoolManager_->unpinPage(currentPageId, false);
            return currentPageId;
        }

        // 内部节点：读取所有条目，确定应该进入哪个子节点
        QVector<InternalEntry> entries;
        PageId firstChild;
        bool success = readInternalEntries(page, entries, firstChild);
        bufferPoolManager_->unpinPage(currentPageId, false);

        if (!success) {
            return INVALID_PAGE_ID;
        }

        // 根据键值确定子节点位置
        int pos = findChildPosition(entries, serializedKey);
        if (pos == 0) {
            // 键小于所有分隔键，进入第一个子节点
            currentPageId = firstChild;
        } else {
            // 进入对应的子节点
            currentPageId = entries[pos - 1].childPageId;
        }
    }

    return INVALID_PAGE_ID;
}

/**
 * @brief 将键值对插入到叶子节点
 * @param leafPageId 目标叶子节点的页面ID
 * @param serializedKey 序列化后的键
 * @param value 对应的行ID
 * @return 插入成功返回true，失败返回false
 *
 * 功能流程：
 * 1. 读取叶子节点的所有条目
 * 2. 使用二分查找确定插入位置
 * 3. 如果键已存在，更新其值
 * 4. 如果键不存在，插入新键值对
 * 5. 检查节点是否需要分裂（键数量超过maxKeysPerPage_）
 * 6. 如果需要分裂，执行节点分裂并更新父节点
 * 7. 如果父节点不存在，创建新的根节点
 *
 * 节点分裂说明：
 * - 当节点键数量超过最大值时触发
 * - 将节点分为左右两部分
 * - 中间键提升到父节点
 * - 可能递归向上传播分裂操作
 */
bool GenericBPlusTree::insertIntoLeaf(PageId leafPageId, const QByteArray& serializedKey, RowId value) {
    Page* page = bufferPoolManager_->fetchPage(leafPageId);
    if (!page) {
        return false;
    }

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    QVector<KeyValuePair> entries;

    // 读取叶子节点的所有键值对
    if (!readLeafEntries(page, entries)) {
        bufferPoolManager_->unpinPage(leafPageId, false);
        return false;
    }

    // 使用二分查找确定插入位置
    int pos = findKeyPositionInLeaf(entries, serializedKey);

    // 检查键是否已存在
    if (pos < entries.size() && compareKeys(entries[pos].serializedKey, serializedKey) == 0) {
        // 键已存在，更新值（B+树不允许重复键）
        entries[pos].value = value;
        bool success = writeLeafEntries(page, entries);
        bufferPoolManager_->unpinPage(leafPageId, success);
        return success;
    }

    // 插入新键值对到正确位置（保持有序）
    entries.insert(pos, KeyValuePair(serializedKey, value));

    // 检查是否需要分裂节点
    if (entries.size() <= maxKeysPerPage_) {
        // 节点未满，不需要分裂，直接写入
        bool success = writeLeafEntries(page, entries);
        bufferPoolManager_->unpinPage(leafPageId, success);
        return success;
    }

    // 节点已满，需要分裂
    // 关键修复：先将新插入的键值对写入页面，然后再分裂
    // 这样可以确保分裂时数据的完整性
    bool success = writeLeafEntries(page, entries);
    bufferPoolManager_->unpinPage(leafPageId, success);
    if (!success) {
        return false;
    }

    // 执行叶子节点分裂
    PageId newLeafPageId;
    QByteArray middleKey;  // 分裂后的中间键
    if (!splitLeafNode(leafPageId, newLeafPageId, middleKey)) {
        return false;
    }

    // 获取父节点ID，准备更新父节点
    page = bufferPoolManager_->fetchPage(leafPageId);
    if (!page) {
        return false;
    }

    header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    PageId parentPageId = header->parentPageId;
    bufferPoolManager_->unpinPage(leafPageId, false);

    if (parentPageId == INVALID_PAGE_ID) {
        // 当前节点是根节点，需要创建新的根节点
        // 新根节点包含中间键和两个子节点指针
        rootPageId_ = createNewRoot(leafPageId, middleKey, newLeafPageId);
        return rootPageId_ != INVALID_PAGE_ID;
    } else {
        // 将中间键插入到父节点
        // 这可能递归触发父节点的分裂
        return insertIntoParent(parentPageId, middleKey, /*leafPageId, */ newLeafPageId);
    }
}

/**
 * @brief 分裂叶子节点
 * @param leafPageId 要分裂的叶子节点ID
 * @param newLeafPageId 输出参数，新创建的叶子节点ID
 * @param middleKey 输出参数，分裂后的中间键（提升到父节点）
 * @return 分裂成功返回true，失败返回false
 *
 * 功能流程：
 * 1. 读取原叶子节点的所有条目
 * 2. 创建新的叶子节点
 * 3. 将条目分为两部分：前一半留在原节点，后一半移到新节点
 * 4. 更新叶子节点链表的指针（prev和next）
 * 5. 返回中间键，用于插入到父节点
 *
 * 分裂策略：
 * - 使用中点分裂（mid = size / 2）
 * - 左节点：[0, mid)
 * - 右节点：[mid, size)
 * - 中间键：右节点的第一个键
 *
 * 链表维护：
 * - 叶子节点通过双向链表连接
 * - 分裂后需要更新prev/next指针
 * - 保证范围查询的正确性
 */
bool GenericBPlusTree::splitLeafNode(PageId leafPageId, PageId& newLeafPageId, QByteArray& middleKey) {
    // 读取原叶子节点
    Page* oldPage = bufferPoolManager_->fetchPage(leafPageId);
    if (!oldPage) {
        return false;
    }

    BPlusTreePageHeader* oldHeader = reinterpret_cast<BPlusTreePageHeader*>(oldPage->getData());
    QVector<KeyValuePair> entries;

    // 读取所有键值对
    if (!readLeafEntries(oldPage, entries)) {
        bufferPoolManager_->unpinPage(leafPageId, false);
        return false;
    }

    // 保存父节点和下一个节点的ID
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
    QVector<KeyValuePair> leftEntries = entries.mid(0, mid);  // [0, mid)
    QVector<KeyValuePair> rightEntries = entries.mid(mid);    // [mid, size)

    // 中间键是右半部分的第一个键，将被提升到父节点
    middleKey = rightEntries.first().serializedKey;

    // 写入新节点（右半部分）
    if (!writeLeafEntries(newPage, rightEntries)) {
        bufferPoolManager_->unpinPage(newLeafPageId, false);
        bufferPoolManager_->deletePage(newLeafPageId);
        return false;
    }

    // 更新新节点的链接指针
    BPlusTreePageHeader* newHeader = reinterpret_cast<BPlusTreePageHeader*>(newPage->getData());
    newHeader->parentPageId = parentPageId;  // 继承父节点
    newHeader->nextPageId = nextPageId;      // 指向原节点的下一个节点
    newHeader->prevPageId = leafPageId;      // 指向原节点
    bufferPoolManager_->unpinPage(newLeafPageId, true);

    // 更新旧节点（左半部分）
    oldPage = bufferPoolManager_->fetchPage(leafPageId);
    if (!oldPage) {
        return false;
    }

    if (!writeLeafEntries(oldPage, leftEntries)) {
        bufferPoolManager_->unpinPage(leafPageId, false);
        return false;
    }

    // 更新旧节点的next指针，指向新节点
    oldHeader = reinterpret_cast<BPlusTreePageHeader*>(oldPage->getData());
    oldHeader->nextPageId = newLeafPageId;
    bufferPoolManager_->unpinPage(leafPageId, true);

    // 更新下一个叶子节点的 prevPageId，使其指向新节点
    // 维护双向链表的完整性
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
                                       /*PageId leftPageId, */ PageId rightPageId) {
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
        return insertIntoParent(grandParentPageId, middleKey, /*parentPageId, */ newInternalPageId);
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

/**
 * @brief 在内部节点中查找键应该进入的子节点位置
 * @param entries 内部节点的所有条目
 * @param key 要查找的键
 * @return 子节点的位置索引
 *
 * 算法说明：
 * 线性扫描，找到第一个大于key的分隔键
 * 返回值：
 * - 0: 进入第一个子节点（firstChild）
 * - i: 进入entries[i-1].childPageId
 */
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

/**
 * @brief 获取B+树的统计信息
 * @return Stats结构，包含树的各种统计数据
 *
 * 注意：当前为简化实现，返回空统计信息
 * TODO: 实现完整的统计功能，需要遍历整棵树
 */
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

/**
 * @brief 打印B+树的结构（用于调试）
 *
 * 功能说明：
 * 递归打印整棵树的结构，包括：
 * - 键类型
 * - 根节点ID
 * - 每页最大键数
 * - 每个节点的类型和键数量
 */
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

/**
 * @brief 递归打印树的结构
 * @param pageId 当前节点的页面ID
 * @param level 当前节点的层级（用于缩进）
 *
 * 功能说明：
 * 深度优先遍历树，打印每个节点的信息
 * 对于内部节点，递归打印所有子节点
 */
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

/**
 * @brief 检查节点是否发生下溢
 * @param page 要检查的页面
 * @return 下溢返回true，否则返回false
 *
 * 下溢条件：
 * - 根节点：叶子节点可以为空，内部节点至少1个键
 * - 非根节点：键数量小于最小值 ⌈maxKeys/2⌉
 *
 * 注意：下溢需要通过借用或合并操作来恢复B+树性质
 */
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

/**
 * @brief 获取节点的最小键数量
 * @param isLeaf 是否为叶子节点
 * @return 最小键数量
 *
 * 计算公式：
 * - 叶子节点和内部节点：⌈(maxKeys+1)/2⌉
 *
 * 说明：这是B+树保持平衡的关键参数
 */
int GenericBPlusTree::getMinKeys(bool isLeaf) {
    if (isLeaf) {
        // 叶子节点：⌈(maxKeys+1)/2⌉ - 1 = ⌈maxKeys/2⌉
        return (maxKeysPerPage_ + 1) / 2;
    } else {
        // 内部节点：⌈maxKeys/2⌉
        return (maxKeysPerPage_ + 1) / 2;
    }
}

/**
 * @brief 从叶子节点删除指定的键
 * @param leafPageId 叶子节点的页面ID
 * @param serializedKey 要删除的键（序列化后）
 * @return 删除成功返回true，失败返回false
 *
 * 功能流程：
 * 1. 读取叶子节点的所有条目
 * 2. 查找要删除的键
 * 3. 如果找到，从条目列表中移除
 * 4. 将更新后的条目写回页面
 *
 * 注意：此函数只负责删除键，不处理下溢
 */
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

/**
 * @brief 获取节点的左右兄弟节点
 * @param nodePageId 当前节点的页面ID
 * @param parentPageId 父节点的页面ID
 * @param leftSiblingPageId 输出参数，左兄弟节点ID
 * @param rightSiblingPageId 输出参数，右兄弟节点ID
 * @param keyIndexInParent 输出参数，当前节点在父节点中的索引
 * @return 成功返回true，失败返回false
 *
 * 功能说明：
 * 在父节点中查找当前节点的位置
 * 确定左右兄弟节点（如果存在）
 * 用于借用或合并操作
 */
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
                                                PageId parentPageId) {
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
                                                 PageId parentPageId) {
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
        if (mergeWithLeftSiblingLeaf(nodePageId, leftSiblingPageId, parentPageId)) {
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
        if (mergeWithRightSiblingLeaf(nodePageId, rightSiblingPageId, parentPageId)) {
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

/**
 * @brief 更新根节点（如果根节点为空）
 *
 * 功能说明：
 * 当根节点是内部节点且没有键时（只有一个子节点）
 * 将唯一的子节点提升为新根节点
 * 这是B+树高度减少的唯一方式
 *
 * 注意：保证树的平衡性和正确性
 */
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
