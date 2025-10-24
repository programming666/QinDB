#include "qindb/bplus_tree.h"
#include "qindb/logger.h"
#include <algorithm>
#include <cstring>

namespace qindb {

BPlusTree::BPlusTree(BufferPoolManager* bufferPoolManager, PageId rootPageId, int order)
    : bufferPoolManager_(bufferPoolManager)
    , rootPageId_(rootPageId)
    , order_(order)
{
    LOG_DEBUG(QString("BPlusTree initialized with order %1, root=%2").arg(order).arg(rootPageId));

    // If no root exists, create one
    if (rootPageId_ == INVALID_PAGE_ID) {
        Page* rootPage = bufferPoolManager_->newPage(&rootPageId_);
        if (rootPage) {
            // Initialize as empty leaf node
            BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(rootPage->getData());
            *header = BPlusTreePageHeader();
            header->nodeType = BPlusTreeNodeType::LEAF_NODE;
            header->pageId = rootPageId_;
            header->maxKeys = order_;
            header->numKeys = 0;

            bufferPoolManager_->unpinPage(rootPageId_, true);
            LOG_INFO(QString("Created new B+ tree root page: %1").arg(rootPageId_));
        }
    }
}

BPlusTree::~BPlusTree() {
}

bool BPlusTree::insert(int64_t key, RowId value) {
    QMutexLocker locker(&mutex_);

    if (rootPageId_ == INVALID_PAGE_ID) {
        LOG_ERROR("B+ tree root is invalid");
        return false;
    }

    // Find the leaf page where the key should be inserted
    PageId leafPageId = findLeafPage(key);
    if (leafPageId == INVALID_PAGE_ID) {
        LOG_ERROR("Failed to find leaf page for insertion");
        return false;
    }

    // Try to insert into leaf
    Page* leafPage = bufferPoolManager_->fetchPage(leafPageId);
    if (!leafPage) {
        LOG_ERROR(QString("Failed to fetch leaf page %1").arg(leafPageId));
        return false;
    }

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(leafPage->getData());

    // Check if leaf has space
    if (header->numKeys < header->maxKeys) {
        // Direct insertion
        bool success = insertIntoLeaf(leafPageId, key, value);
        bufferPoolManager_->unpinPage(leafPageId, success);
        return success;
    }

    // Leaf is full, need to split
    PageId newLeafPageId;
    int64_t middleKey;

    if (!splitLeafNode(leafPageId, newLeafPageId, middleKey)) {
        bufferPoolManager_->unpinPage(leafPageId, false);
        LOG_ERROR("Failed to split leaf node");
        return false;
    }

    // Insert the key into appropriate leaf after split
    if (key < middleKey) {
        insertIntoLeaf(leafPageId, key, value);
    } else {
        insertIntoLeaf(newLeafPageId, key, value);
    }

    bufferPoolManager_->unpinPage(leafPageId, true);
    bufferPoolManager_->unpinPage(newLeafPageId, true);

    // Insert middle key into parent
    PageId parentPageId = header->parentPageId;
    if (parentPageId == INVALID_PAGE_ID) {
        // Need new root
        rootPageId_ = createNewRoot(leafPageId, middleKey, newLeafPageId);
        if (rootPageId_ == INVALID_PAGE_ID) {
            LOG_ERROR("Failed to create new root");
            return false;
        }
    } else {
        if (!insertIntoParent(parentPageId, middleKey, leafPageId, newLeafPageId)) {
            LOG_ERROR("Failed to insert into parent");
            return false;
        }
    }

    LOG_DEBUG(QString("Inserted key=%1, value=%2").arg(key).arg(value));
    return true;
}

bool BPlusTree::remove(int64_t key) {
    QMutexLocker locker(&mutex_);

    // Find leaf containing the key
    PageId leafPageId = findLeafPage(key);
    if (leafPageId == INVALID_PAGE_ID) {
        return false;
    }

    Page* leafPage = bufferPoolManager_->fetchPage(leafPageId);
    if (!leafPage) {
        return false;
    }

    QVector<BPlusTreeEntry> entries;
    readLeafEntries(leafPage, entries);

    // Find and remove the key
    bool found = false;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].key == key) {
            entries.removeAt(i);
            found = true;
            break;
        }
    }

    if (found) {
        writeLeafEntries(leafPage, entries);
        bufferPoolManager_->unpinPage(leafPageId, true);
        LOG_DEBUG(QString("Removed key=%1").arg(key));
    } else {
        bufferPoolManager_->unpinPage(leafPageId, false);
    }

    // TODO: Handle underflow and rebalancing

    return found;
}

bool BPlusTree::search(int64_t key, RowId& value) {
    QMutexLocker locker(&mutex_);

    PageId leafPageId = findLeafPage(key);
    if (leafPageId == INVALID_PAGE_ID) {
        return false;
    }

    Page* leafPage = bufferPoolManager_->fetchPage(leafPageId);
    if (!leafPage) {
        return false;
    }

    QVector<BPlusTreeEntry> entries;
    readLeafEntries(leafPage, entries);

    bool found = false;
    for (const auto& entry : entries) {
        if (entry.key == key) {
            value = entry.value;
            found = true;
            break;
        }
    }

    bufferPoolManager_->unpinPage(leafPageId, false);
    return found;
}

bool BPlusTree::rangeSearch(int64_t minKey, int64_t maxKey, QVector<BPlusTreeEntry>& results) {
    QMutexLocker locker(&mutex_);

    results.clear();

    // Find the leaf page containing minKey
    PageId currentPageId = findLeafPage(minKey);
    if (currentPageId == INVALID_PAGE_ID) {
        return false;
    }

    // Traverse leaf pages via linked list
    while (currentPageId != INVALID_PAGE_ID) {
        Page* page = bufferPoolManager_->fetchPage(currentPageId);
        if (!page) {
            break;
        }

        QVector<BPlusTreeEntry> entries;
        readLeafEntries(page, entries);

        for (const auto& entry : entries) {
            if (entry.key >= minKey && entry.key <= maxKey) {
                results.append(entry);
            }
            if (entry.key > maxKey) {
                bufferPoolManager_->unpinPage(currentPageId, false);
                return true;
            }
        }

        BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
        PageId nextPageId = header->nextPageId;
        bufferPoolManager_->unpinPage(currentPageId, false);
        currentPageId = nextPageId;
    }

    return true;
}

BPlusTree::Stats BPlusTree::getStats() const {
    QMutexLocker locker(&mutex_);

    Stats stats;
    stats.numKeys = 0;
    stats.numLeafPages = 0;
    stats.numInternalPages = 0;
    stats.treeHeight = 0;

    // TODO: Traverse tree to collect statistics

    return stats;
}

void BPlusTree::printTree() const {
    QMutexLocker locker(&mutex_);

    LOG_INFO("=== B+ Tree Structure ===");
    if (rootPageId_ != INVALID_PAGE_ID) {
        printTreeRecursive(rootPageId_, 0);
    } else {
        LOG_INFO("Empty tree");
    }
    LOG_INFO("========================");
}

PageId BPlusTree::findLeafPage(int64_t key) {
    if (rootPageId_ == INVALID_PAGE_ID) {
        return INVALID_PAGE_ID;
    }

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

        // Internal node - find child to descend to
        QVector<BPlusTreeInternalEntry> entries;
        PageId firstChild;
        readInternalEntries(page, entries, firstChild);

        PageId nextPageId = firstChild;
        for (const auto& entry : entries) {
            if (key >= entry.key) {
                nextPageId = entry.childPageId;
            } else {
                break;
            }
        }

        bufferPoolManager_->unpinPage(currentPageId, false);
        currentPageId = nextPageId;
    }

    return INVALID_PAGE_ID;
}

bool BPlusTree::insertIntoLeaf(PageId leafPageId, int64_t key, RowId value) {
    Page* page = bufferPoolManager_->fetchPage(leafPageId);
    if (!page) {
        return false;
    }

    QVector<BPlusTreeEntry> entries;
    readLeafEntries(page, entries);

    // Insert in sorted order
    BPlusTreeEntry newEntry(key, value);
    int insertPos = 0;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].key < key) {
            insertPos = i + 1;
        } else if (entries[i].key == key) {
            // Update existing key
            entries[i].value = value;
            writeLeafEntries(page, entries);
            bufferPoolManager_->unpinPage(leafPageId, true);
            return true;
        } else {
            break;
        }
    }

    entries.insert(insertPos, newEntry);
    writeLeafEntries(page, entries);
    bufferPoolManager_->unpinPage(leafPageId, true);

    return true;
}

bool BPlusTree::splitLeafNode(PageId leafPageId, PageId& newLeafPageId, int64_t& middleKey) {
    Page* oldPage = bufferPoolManager_->fetchPage(leafPageId);
    if (!oldPage) {
        return false;
    }

    Page* newPage = bufferPoolManager_->newPage(&newLeafPageId);
    if (!newPage) {
        bufferPoolManager_->unpinPage(leafPageId, false);
        return false;
    }

    // Read old entries
    QVector<BPlusTreeEntry> oldEntries;
    readLeafEntries(oldPage, oldEntries);

    // Split entries
    int midPoint = oldEntries.size() / 2;
    QVector<BPlusTreeEntry> newEntries = oldEntries.mid(midPoint);
    oldEntries = oldEntries.mid(0, midPoint);

    middleKey = newEntries.first().key;

    // Initialize new page header
    BPlusTreePageHeader* oldHeader = reinterpret_cast<BPlusTreePageHeader*>(oldPage->getData());
    BPlusTreePageHeader* newHeader = reinterpret_cast<BPlusTreePageHeader*>(newPage->getData());

    *newHeader = BPlusTreePageHeader();
    newHeader->nodeType = BPlusTreeNodeType::LEAF_NODE;
    newHeader->pageId = newLeafPageId;
    newHeader->maxKeys = order_;
    newHeader->parentPageId = oldHeader->parentPageId;

    // Update linked list
    newHeader->nextPageId = oldHeader->nextPageId;
    newHeader->prevPageId = leafPageId;
    oldHeader->nextPageId = newLeafPageId;

    // Write entries
    writeLeafEntries(oldPage, oldEntries);
    writeLeafEntries(newPage, newEntries);

    return true;
}

bool BPlusTree::splitInternalNode(PageId internalPageId, PageId& newInternalPageId, int64_t& middleKey) {
    Page* oldPage = bufferPoolManager_->fetchPage(internalPageId);
    if (!oldPage) {
        return false;
    }

    Page* newPage = bufferPoolManager_->newPage(&newInternalPageId);
    if (!newPage) {
        bufferPoolManager_->unpinPage(internalPageId, false);
        return false;
    }

    QVector<BPlusTreeInternalEntry> oldEntries;
    PageId firstChild;
    readInternalEntries(oldPage, oldEntries, firstChild);

    int midPoint = oldEntries.size() / 2;
    middleKey = oldEntries[midPoint].key;

    QVector<BPlusTreeInternalEntry> newEntries = oldEntries.mid(midPoint + 1);
    PageId newFirstChild = oldEntries[midPoint].childPageId;
    oldEntries = oldEntries.mid(0, midPoint);

    BPlusTreePageHeader* newHeader = reinterpret_cast<BPlusTreePageHeader*>(newPage->getData());
    *newHeader = BPlusTreePageHeader();
    newHeader->nodeType = BPlusTreeNodeType::INTERNAL_NODE;
    newHeader->pageId = newInternalPageId;
    newHeader->maxKeys = order_;

    writeInternalEntries(oldPage, oldEntries, firstChild);
    writeInternalEntries(newPage, newEntries, newFirstChild);

    return true;
}

bool BPlusTree::insertIntoParent(PageId parentPageId, int64_t key, PageId leftPageId, PageId rightPageId) {
    Page* parentPage = bufferPoolManager_->fetchPage(parentPageId);
    if (!parentPage) {
        return false;
    }

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(parentPage->getData());

    if (header->numKeys < header->maxKeys) {
        // Parent has space
        QVector<BPlusTreeInternalEntry> entries;
        PageId firstChild;
        readInternalEntries(parentPage, entries, firstChild);

        BPlusTreeInternalEntry newEntry(key, rightPageId);
        entries.append(newEntry);
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.key < b.key;
        });

        writeInternalEntries(parentPage, entries, firstChild);
        bufferPoolManager_->unpinPage(parentPageId, true);
        return true;
    }

    // Parent is full, need to split
    PageId newParentPageId;
    int64_t middleKey;

    if (!splitInternalNode(parentPageId, newParentPageId, middleKey)) {
        bufferPoolManager_->unpinPage(parentPageId, false);
        return false;
    }

    bufferPoolManager_->unpinPage(parentPageId, true);

    // Insert into appropriate parent after split
    if (key < middleKey) {
        insertIntoParent(parentPageId, key, leftPageId, rightPageId);
    } else {
        insertIntoParent(newParentPageId, key, leftPageId, rightPageId);
    }

    // Propagate split upward
    PageId grandParentPageId = header->parentPageId;
    if (grandParentPageId == INVALID_PAGE_ID) {
        rootPageId_ = createNewRoot(parentPageId, middleKey, newParentPageId);
    } else {
        insertIntoParent(grandParentPageId, middleKey, parentPageId, newParentPageId);
    }

    return true;
}

PageId BPlusTree::createNewRoot(PageId leftPageId, int64_t key, PageId rightPageId) {
    PageId newRootPageId;
    Page* newRootPage = bufferPoolManager_->newPage(&newRootPageId);
    if (!newRootPage) {
        return INVALID_PAGE_ID;
    }

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(newRootPage->getData());
    *header = BPlusTreePageHeader();
    header->nodeType = BPlusTreeNodeType::INTERNAL_NODE;
    header->pageId = newRootPageId;
    header->maxKeys = order_;
    header->numKeys = 1;

    QVector<BPlusTreeInternalEntry> entries;
    entries.append(BPlusTreeInternalEntry(key, rightPageId));
    writeInternalEntries(newRootPage, entries, leftPageId);

    // Update children's parent pointers
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

    bufferPoolManager_->unpinPage(newRootPageId, true);

    LOG_INFO(QString("Created new root page %1").arg(newRootPageId));
    return newRootPageId;
}

int BPlusTree::findKeyPositionInLeaf(Page* page, int64_t key) {
    QVector<BPlusTreeEntry> entries;
    readLeafEntries(page, entries);

    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].key >= key) {
            return i;
        }
    }
    return entries.size();
}

int BPlusTree::findKeyPositionInInternal(Page* page, int64_t key) {
    QVector<BPlusTreeInternalEntry> entries;
    PageId firstChild;
    readInternalEntries(page, entries, firstChild);

    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].key > key) {
            return i;
        }
    }
    return entries.size();
}

void BPlusTree::readLeafEntries(Page* page, QVector<BPlusTreeEntry>& entries) {
    entries.clear();

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    char* dataStart = page->getData() + sizeof(BPlusTreePageHeader);

    for (int i = 0; i < header->numKeys; ++i) {
        BPlusTreeEntry entry;
        std::memcpy(&entry, dataStart + i * sizeof(BPlusTreeEntry), sizeof(BPlusTreeEntry));
        entries.append(entry);
    }
}

void BPlusTree::writeLeafEntries(Page* page, const QVector<BPlusTreeEntry>& entries) {
    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    header->numKeys = entries.size();

    char* dataStart = page->getData() + sizeof(BPlusTreePageHeader);

    for (int i = 0; i < entries.size(); ++i) {
        std::memcpy(dataStart + i * sizeof(BPlusTreeEntry), &entries[i], sizeof(BPlusTreeEntry));
    }
}

void BPlusTree::readInternalEntries(Page* page, QVector<BPlusTreeInternalEntry>& entries, PageId& firstChild) {
    entries.clear();

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    char* dataStart = page->getData() + sizeof(BPlusTreePageHeader);

    // First 4 bytes contain the first child pointer
    std::memcpy(&firstChild, dataStart, sizeof(PageId));
    dataStart += sizeof(PageId);

    for (int i = 0; i < header->numKeys; ++i) {
        BPlusTreeInternalEntry entry;
        std::memcpy(&entry, dataStart + i * sizeof(BPlusTreeInternalEntry), sizeof(BPlusTreeInternalEntry));
        entries.append(entry);
    }
}

void BPlusTree::writeInternalEntries(Page* page, const QVector<BPlusTreeInternalEntry>& entries, PageId firstChild) {
    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    header->numKeys = entries.size();

    char* dataStart = page->getData() + sizeof(BPlusTreePageHeader);

    // Write first child pointer
    std::memcpy(dataStart, &firstChild, sizeof(PageId));
    dataStart += sizeof(PageId);

    for (int i = 0; i < entries.size(); ++i) {
        std::memcpy(dataStart + i * sizeof(BPlusTreeInternalEntry), &entries[i], sizeof(BPlusTreeInternalEntry));
    }
}

void BPlusTree::printTreeRecursive(PageId pageId, int level) const {
    if (pageId == INVALID_PAGE_ID) {
        return;
    }

    Page* page = bufferPoolManager_->fetchPage(pageId);
    if (!page) {
        return;
    }

    BPlusTreePageHeader* header = reinterpret_cast<BPlusTreePageHeader*>(page->getData());
    QString indent(level * 2, ' ');

    if (header->nodeType == BPlusTreeNodeType::LEAF_NODE) {
        QVector<BPlusTreeEntry> entries;
        // Inline read for const method
        char* dataStart = page->getData() + sizeof(BPlusTreePageHeader);
        for (int i = 0; i < header->numKeys; ++i) {
            BPlusTreeEntry entry;
            std::memcpy(&entry, dataStart + i * sizeof(BPlusTreeEntry), sizeof(BPlusTreeEntry));
            entries.append(entry);
        }

        QString keys;
        for (const auto& entry : entries) {
            keys += QString(" %1:%2").arg(entry.key).arg(entry.value);
        }
        LOG_INFO(QString("%1LEAF[%2]:%3").arg(indent).arg(pageId).arg(keys));
    } else {
        QVector<BPlusTreeInternalEntry> entries;
        PageId firstChild;
        // Inline read for const method
        char* dataStart = page->getData() + sizeof(BPlusTreePageHeader);
        std::memcpy(&firstChild, dataStart, sizeof(PageId));
        dataStart += sizeof(PageId);
        for (int i = 0; i < header->numKeys; ++i) {
            BPlusTreeInternalEntry entry;
            std::memcpy(&entry, dataStart + i * sizeof(BPlusTreeInternalEntry), sizeof(BPlusTreeInternalEntry));
            entries.append(entry);
        }

        QString keys;
        for (const auto& entry : entries) {
            keys += QString(" %1").arg(entry.key);
        }
        LOG_INFO(QString("%1INTERNAL[%2]:%3").arg(indent).arg(pageId).arg(keys));

        // Recursively print children
        printTreeRecursive(firstChild, level + 1);
        for (const auto& entry : entries) {
            printTreeRecursive(entry.childPageId, level + 1);
        }
    }

    bufferPoolManager_->unpinPage(pageId, false);
}

} // namespace qindb
