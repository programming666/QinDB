#include "qindb/hash_index.h"
#include "qindb/hash_bucket_page.h"
#include "qindb/logger.h"
#include <QCryptographicHash>
#include <QMutexLocker>
#include <cstring>

namespace qindb {

HashIndex::HashIndex(const QString& indexName,
                     DataType keyType,
                     BufferPoolManager* bufferPool,
                     uint32_t numBuckets)
    : indexName_(indexName)
    , keyType_(keyType)
    , bufferPool_(bufferPool)
    , numBuckets_(numBuckets)
    , directoryPageId_(INVALID_PAGE_ID)
{
    // Ensure numBuckets is a power of 2
    if ((numBuckets & (numBuckets - 1)) != 0) {
        // Round up to next power of 2
        numBuckets_ = 1;
        while (numBuckets_ < numBuckets) {
            numBuckets_ <<= 1;
        }
        LOG_WARN(QString("HashIndex: Adjusted bucket count from %1 to %2 (power of 2)")
                     .arg(numBuckets).arg(numBuckets_));
    }

    LOG_INFO(QString("HashIndex created: %1, keyType=%2, numBuckets=%3")
                 .arg(indexName_)
                 .arg(static_cast<int>(keyType_))
                 .arg(numBuckets_));
}

HashIndex::~HashIndex() {
    LOG_INFO(QString("HashIndex destroyed: %1").arg(indexName_));
}

bool HashIndex::insert(const QVariant& key, RowId value) {
    if (key.isNull() || value == INVALID_ROW_ID) {
        return false;
    }

    QMutexLocker locker(&mutex_);

    // Serialize key
    QByteArray serializedKey = serializeKey(key);
    if (serializedKey.isEmpty()) {
        LOG_ERROR("HashIndex::insert: Failed to serialize key");
        return false;
    }

    // Calculate hash and bucket index
    uint32_t bucketIndex = hash(serializedKey);

    // Get bucket page ID
    PageId bucketPageId = getBucketPageId(bucketIndex);
    if (bucketPageId == INVALID_PAGE_ID) {
        LOG_ERROR(QString("HashIndex::insert: Failed to get bucket page for index %1")
                      .arg(bucketIndex));
        return false;
    }

    // Try to insert into bucket page
    Page* page = bufferPool_->fetchPage(bucketPageId);
    if (!page) {
        LOG_ERROR(QString("HashIndex::insert: Failed to fetch bucket page %1")
                      .arg(bucketPageId));
        return false;
    }

    bool inserted = HashBucketPage::insert(page, serializedKey, value);

    // If bucket is full, try overflow pages
    if (!inserted) {
        PageId currentPageId = bucketPageId;
        while (!inserted) {
            PageId nextPageId = HashBucketPage::getNextBucketPageId(page);

            if (nextPageId == INVALID_PAGE_ID) {
                // Create new overflow page
                nextPageId = createOverflowPage();
                if (nextPageId == INVALID_PAGE_ID) {
                    bufferPool_->unpinPage(currentPageId, false);
                    LOG_ERROR("HashIndex::insert: Failed to create overflow page");
                    return false;
                }

                // Link overflow page
                HashBucketPage::setNextBucketPageId(page, nextPageId);
                bufferPool_->unpinPage(currentPageId, true);
            } else {
                bufferPool_->unpinPage(currentPageId, false);
            }

            // Fetch overflow page
            currentPageId = nextPageId;
            page = bufferPool_->fetchPage(currentPageId);
            if (!page) {
                LOG_ERROR(QString("HashIndex::insert: Failed to fetch overflow page %1")
                              .arg(currentPageId));
                return false;
            }

            inserted = HashBucketPage::insert(page, serializedKey, value);
        }

        bufferPool_->unpinPage(currentPageId, true);
    } else {
        bufferPool_->unpinPage(bucketPageId, true);
    }

    return true;
}

bool HashIndex::search(const QVariant& key, RowId& value) {
    if (key.isNull()) {
        return false;
    }

    QMutexLocker locker(&mutex_);

    // Serialize key
    QByteArray serializedKey = serializeKey(key);
    if (serializedKey.isEmpty()) {
        return false;
    }

    // Calculate hash and bucket index
    uint32_t bucketIndex = hash(serializedKey);

    // Get bucket page ID
    PageId bucketPageId = getBucketPageId(bucketIndex);
    if (bucketPageId == INVALID_PAGE_ID) {
        return false;
    }

    // Search in bucket page
    Page* page = bufferPool_->fetchPage(bucketPageId);
    if (!page) {
        return false;
    }

    bool found = HashBucketPage::search(page, serializedKey, value);

    // If not found, search in overflow pages
    if (!found) {
        PageId currentPageId = bucketPageId;
        while (!found) {
            PageId nextPageId = HashBucketPage::getNextBucketPageId(page);
            bufferPool_->unpinPage(currentPageId, false);

            if (nextPageId == INVALID_PAGE_ID) {
                break;
            }

            currentPageId = nextPageId;
            page = bufferPool_->fetchPage(currentPageId);
            if (!page) {
                break;
            }

            found = HashBucketPage::search(page, serializedKey, value);
        }

        if (page) {
            bufferPool_->unpinPage(currentPageId, false);
        }
    } else {
        bufferPool_->unpinPage(bucketPageId, false);
    }

    return found;
}

bool HashIndex::searchAll(const QVariant& key, std::vector<RowId>& values) {
    if (key.isNull()) {
        return false;
    }

    QMutexLocker locker(&mutex_);

    // Serialize key
    QByteArray serializedKey = serializeKey(key);
    if (serializedKey.isEmpty()) {
        return false;
    }

    // Calculate hash and bucket index
    uint32_t bucketIndex = hash(serializedKey);

    // Get bucket page ID
    PageId bucketPageId = getBucketPageId(bucketIndex);
    if (bucketPageId == INVALID_PAGE_ID) {
        return false;
    }

    // Search in bucket page
    Page* page = bufferPool_->fetchPage(bucketPageId);
    if (!page) {
        return false;
    }

    bool found = HashBucketPage::searchAll(page, serializedKey, values);

    // Search in overflow pages
    PageId currentPageId = bucketPageId;
    while (true) {
        PageId nextPageId = HashBucketPage::getNextBucketPageId(page);
        bufferPool_->unpinPage(currentPageId, false);

        if (nextPageId == INVALID_PAGE_ID) {
            break;
        }

        currentPageId = nextPageId;
        page = bufferPool_->fetchPage(currentPageId);
        if (!page) {
            break;
        }

        if (HashBucketPage::searchAll(page, serializedKey, values)) {
            found = true;
        }
    }

    return found;
}

bool HashIndex::remove(const QVariant& key, RowId value) {
    if (key.isNull()) {
        return false;
    }

    QMutexLocker locker(&mutex_);

    // Serialize key
    QByteArray serializedKey = serializeKey(key);
    if (serializedKey.isEmpty()) {
        return false;
    }

    // Calculate hash and bucket index
    uint32_t bucketIndex = hash(serializedKey);

    // Get bucket page ID
    PageId bucketPageId = getBucketPageId(bucketIndex);
    if (bucketPageId == INVALID_PAGE_ID) {
        return false;
    }

    // Try to remove from bucket page
    Page* page = bufferPool_->fetchPage(bucketPageId);
    if (!page) {
        return false;
    }

    bool removed = HashBucketPage::remove(page, serializedKey, value);

    // If not removed, try overflow pages
    if (!removed) {
        PageId currentPageId = bucketPageId;
        while (!removed) {
            PageId nextPageId = HashBucketPage::getNextBucketPageId(page);
            bufferPool_->unpinPage(currentPageId, false);

            if (nextPageId == INVALID_PAGE_ID) {
                break;
            }

            currentPageId = nextPageId;
            page = bufferPool_->fetchPage(currentPageId);
            if (!page) {
                break;
            }

            removed = HashBucketPage::remove(page, serializedKey, value);
        }

        if (page) {
            bufferPool_->unpinPage(currentPageId, removed);
        }
    } else {
        bufferPool_->unpinPage(bucketPageId, true);
    }

    return removed;
}

HashIndex::Statistics HashIndex::getStatistics() const {
    QMutexLocker locker(&mutex_);

    Statistics stats;
    stats.numBuckets = numBuckets_;
    stats.numEntries = 0;
    stats.numOverflowPages = 0;
    stats.avgBucketSize = 0.0;
    stats.loadFactor = 0.0;

    if (directoryPageId_ == INVALID_PAGE_ID) {
        return stats;
    }

    // Iterate through all buckets
    for (uint32_t i = 0; i < numBuckets_; i++) {
        PageId bucketPageId = const_cast<HashIndex*>(this)->getBucketPageId(i);
        if (bucketPageId == INVALID_PAGE_ID) {
            continue;
        }

        Page* page = bufferPool_->fetchPage(bucketPageId);
        if (!page) {
            continue;
        }

        stats.numEntries += HashBucketPage::getNumEntries(page);

        // Count overflow pages
        PageId currentPageId = bucketPageId;
        while (true) {
            PageId nextPageId = HashBucketPage::getNextBucketPageId(page);
            bufferPool_->unpinPage(currentPageId, false);

            if (nextPageId == INVALID_PAGE_ID) {
                break;
            }

            stats.numOverflowPages++;
            currentPageId = nextPageId;
            page = bufferPool_->fetchPage(currentPageId);
            if (!page) {
                break;
            }

            stats.numEntries += HashBucketPage::getNumEntries(page);
        }
    }

    stats.avgBucketSize = static_cast<double>(stats.numEntries) / numBuckets_;
    stats.loadFactor = stats.avgBucketSize;

    return stats;
}

uint32_t HashIndex::hash(const QByteArray& key) const {
    // Use SHA-256 hash and take first 4 bytes
    QByteArray hashValue = QCryptographicHash::hash(key, QCryptographicHash::Sha256);
    uint32_t hashInt;
    memcpy(&hashInt, hashValue.constData(), sizeof(hashInt));

    // Use modulo to get bucket index (works because numBuckets_ is power of 2)
    return hashInt & (numBuckets_ - 1);
}

PageId HashIndex::getBucketPageId(uint32_t bucketIndex) {
    if (bucketIndex >= numBuckets_) {
        return INVALID_PAGE_ID;
    }

    // If directory doesn't exist, create it
    if (directoryPageId_ == INVALID_PAGE_ID) {
        initializeDirectory();
        if (directoryPageId_ == INVALID_PAGE_ID) {
            return INVALID_PAGE_ID;
        }
    }

    // Fetch directory page
    Page* dirPage = bufferPool_->fetchPage(directoryPageId_);
    if (!dirPage) {
        return INVALID_PAGE_ID;
    }

    // Read bucket page ID from directory
    char* data = dirPage->getData();
    PageId bucketPageId;
    memcpy(&bucketPageId, data + bucketIndex * sizeof(PageId), sizeof(PageId));

    // If bucket doesn't exist, create it
    if (bucketPageId == INVALID_PAGE_ID) {
        Page* bucketPage = bufferPool_->newPage(&bucketPageId);
        if (!bucketPage) {
            bufferPool_->unpinPage(directoryPageId_, false);
            return INVALID_PAGE_ID;
        }

        // Initialize bucket page
        HashBucketPage::initialize(bucketPage);
        bufferPool_->unpinPage(bucketPageId, true);

        // Update directory
        memcpy(data + bucketIndex * sizeof(PageId), &bucketPageId, sizeof(PageId));
        bufferPool_->unpinPage(directoryPageId_, true);
    } else {
        bufferPool_->unpinPage(directoryPageId_, false);
    }

    return bucketPageId;
}

PageId HashIndex::createOverflowPage() {
    PageId pageId;
    Page* page = bufferPool_->newPage(&pageId);
    if (!page) {
        return INVALID_PAGE_ID;
    }

    HashBucketPage::initialize(page);
    bufferPool_->unpinPage(pageId, true);

    return pageId;
}

void HashIndex::initializeDirectory() {
    Page* dirPage = bufferPool_->newPage(&directoryPageId_);
    if (!dirPage) {
        directoryPageId_ = INVALID_PAGE_ID;
        return;
    }

    // Initialize directory with INVALID_PAGE_ID for all buckets
    char* data = dirPage->getData();
    for (uint32_t i = 0; i < numBuckets_; i++) {
        PageId invalidPageId = INVALID_PAGE_ID;
        memcpy(data + i * sizeof(PageId), &invalidPageId, sizeof(PageId));
    }

    bufferPool_->unpinPage(directoryPageId_, true);

    LOG_INFO(QString("HashIndex directory initialized: pageId=%1, numBuckets=%2")
                 .arg(directoryPageId_).arg(numBuckets_));
}

QByteArray HashIndex::serializeKey(const QVariant& key) const {
    QByteArray output;
    if (!TypeSerializer::serialize(key, keyType_, output)) {
        LOG_ERROR("HashIndex::serializeKey: Failed to serialize key");
        return QByteArray();
    }
    return output;
}

} // namespace qindb
