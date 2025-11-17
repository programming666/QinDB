#include "qindb/hash_bucket_page.h"
#include "qindb/logger.h"
#include <cstring>

namespace qindb {

/**
 * @brief 初始化哈希桶页面
 * @param page 要初始化的页面指针
 */
void HashBucketPage::initialize(Page* page) {
    if (!page) {
        return;
    }

    // Clear the page
    memset(page->getData(), 0, PAGE_SIZE);

    // Initialize header
    uint32_t numEntries = 0;
    PageId nextBucketPageId = INVALID_PAGE_ID;

    memcpy(page->getData() + NUM_ENTRIES_OFFSET, &numEntries, sizeof(numEntries));
    memcpy(page->getData() + NEXT_BUCKET_PAGE_ID_OFFSET, &nextBucketPageId, sizeof(nextBucketPageId));
}

bool HashBucketPage::insert(Page* page, const QByteArray& key, RowId value) {
    if (!page || key.isEmpty()) {
        return false;
    }

    // Check if key already exists (update value)
    RowId existingValue;
    if (search(page, key, existingValue)) {
        // Key exists, update is not supported in hash index
        // (would require finding and modifying the existing entry)
        return false;
    }

    // Calculate required space
    size_t entrySize = KEY_SIZE_FIELD + key.size() + VALUE_SIZE_FIELD + VALUE_DATA_SIZE;

    // Check if page has enough space
    if (isFull(page, key.size())) {
        return false;
    }

    // Get current number of entries
    uint32_t numEntries = getNumEntries(page);

    // Find insert position (after header and existing entries)
    size_t offset = HEADER_SIZE;
    char* data = page->getData();

    // Skip existing entries to find insertion point
    for (uint32_t i = 0; i < numEntries; i++) {
        uint32_t keySize;
        memcpy(&keySize, data + offset, KEY_SIZE_FIELD);
        offset += KEY_SIZE_FIELD + keySize + VALUE_SIZE_FIELD + VALUE_DATA_SIZE;
    }

    // Write new entry
    uint32_t keySize = static_cast<uint32_t>(key.size());
    uint32_t valueSize = VALUE_DATA_SIZE;

    memcpy(data + offset, &keySize, KEY_SIZE_FIELD);
    offset += KEY_SIZE_FIELD;

    memcpy(data + offset, key.constData(), key.size());
    offset += key.size();

    memcpy(data + offset, &valueSize, VALUE_SIZE_FIELD);
    offset += VALUE_SIZE_FIELD;

    memcpy(data + offset, &value, VALUE_DATA_SIZE);

    // Update number of entries
    numEntries++;
    memcpy(data + NUM_ENTRIES_OFFSET, &numEntries, sizeof(numEntries));

    return true;
}

bool HashBucketPage::search(Page* page, const QByteArray& key, RowId& value) {
    if (!page || key.isEmpty()) {
        return false;
    }

    uint32_t numEntries = getNumEntries(page);
    if (numEntries == 0) {
        return false;
    }

    size_t offset = HEADER_SIZE;
    char* data = page->getData();

    // Linear search through entries
    for (uint32_t i = 0; i < numEntries; i++) {
        // Read key size
        uint32_t keySize;
        memcpy(&keySize, data + offset, KEY_SIZE_FIELD);
        offset += KEY_SIZE_FIELD;

        // Read key data
        QByteArray entryKey(data + offset, static_cast<int>(keySize));
        offset += keySize;

        // Skip value size field
        offset += VALUE_SIZE_FIELD;

        // Read value
        RowId entryValue;
        memcpy(&entryValue, data + offset, VALUE_DATA_SIZE);
        offset += VALUE_DATA_SIZE;

        // Check if key matches
        if (entryKey == key) {
            value = entryValue;
            return true;
        }
    }

    return false;
}

bool HashBucketPage::searchAll(Page* page, const QByteArray& key, std::vector<RowId>& values) {
    if (!page || key.isEmpty()) {
        return false;
    }

    uint32_t numEntries = getNumEntries(page);
    if (numEntries == 0) {
        return false;
    }

    size_t offset = HEADER_SIZE;
    char* data = page->getData();
    bool found = false;

    // Linear search through entries
    for (uint32_t i = 0; i < numEntries; i++) {
        // Read key size
        uint32_t keySize;
        memcpy(&keySize, data + offset, KEY_SIZE_FIELD);
        offset += KEY_SIZE_FIELD;

        // Read key data
        QByteArray entryKey(data + offset, static_cast<int>(keySize));
        offset += keySize;

        // Skip value size field
        offset += VALUE_SIZE_FIELD;

        // Read value
        RowId entryValue;
        memcpy(&entryValue, data + offset, VALUE_DATA_SIZE);
        offset += VALUE_DATA_SIZE;

        // Check if key matches
        if (entryKey == key) {
            values.push_back(entryValue);
            found = true;
        }
    }

    return found;
}

bool HashBucketPage::remove(Page* page, const QByteArray& key, RowId value) {
    if (!page || key.isEmpty()) {
        return false;
    }

    uint32_t numEntries = getNumEntries(page);
    if (numEntries == 0) {
        return false;
    }

    size_t offset = HEADER_SIZE;
    char* data = page->getData();

    // Find the entry to remove
    for (uint32_t i = 0; i < numEntries; i++) {
        size_t entryStart = offset;

        // Read key size
        uint32_t keySize;
        memcpy(&keySize, data + offset, KEY_SIZE_FIELD);
        offset += KEY_SIZE_FIELD;

        // Read key data
        QByteArray entryKey(data + offset, static_cast<int>(keySize));
        offset += keySize;

        // Skip value size field
        offset += VALUE_SIZE_FIELD;

        // Read value
        RowId entryValue;
        memcpy(&entryValue, data + offset, VALUE_DATA_SIZE);
        offset += VALUE_DATA_SIZE;

        // Check if this is the entry to remove
        if (entryKey == key && (value == INVALID_ROW_ID || entryValue == value)) {
            size_t entrySize = offset - entryStart;

            // Calculate remaining data size
            size_t remainingSize = (HEADER_SIZE + MAX_ENTRY_SPACE) - offset;

            // Shift remaining entries left
            if (remainingSize > 0) {
                memmove(data + entryStart, data + offset, remainingSize);
            }

            // Clear the end
            size_t clearStart = HEADER_SIZE + MAX_ENTRY_SPACE - entrySize;
            memset(data + clearStart, 0, entrySize);

            // Update number of entries
            numEntries--;
            memcpy(data + NUM_ENTRIES_OFFSET, &numEntries, sizeof(numEntries));

            return true;
        }
    }

    return false;
}

std::vector<std::pair<QByteArray, RowId>> HashBucketPage::getAll(Page* page) {
    std::vector<std::pair<QByteArray, RowId>> result;

    if (!page) {
        return result;
    }

    uint32_t numEntries = getNumEntries(page);
    if (numEntries == 0) {
        return result;
    }

    size_t offset = HEADER_SIZE;
    char* data = page->getData();

    for (uint32_t i = 0; i < numEntries; i++) {
        // Read key size
        uint32_t keySize;
        memcpy(&keySize, data + offset, KEY_SIZE_FIELD);
        offset += KEY_SIZE_FIELD;

        // Read key data
        QByteArray key(data + offset, static_cast<int>(keySize));
        offset += keySize;

        // Skip value size field
        offset += VALUE_SIZE_FIELD;

        // Read value
        RowId value;
        memcpy(&value, data + offset, VALUE_DATA_SIZE);
        offset += VALUE_DATA_SIZE;

        result.push_back({key, value});
    }

    return result;
}

bool HashBucketPage::isFull(Page* page, size_t keySize) {
    if (!page) {
        return true;
    }

    uint32_t numEntries = getNumEntries(page);

    // Calculate current used space
    size_t usedSpace = 0;
    size_t offset = HEADER_SIZE;
    char* data = page->getData();

    for (uint32_t i = 0; i < numEntries; i++) {
        uint32_t entryKeySize;
        memcpy(&entryKeySize, data + offset, KEY_SIZE_FIELD);
        size_t entrySize = KEY_SIZE_FIELD + entryKeySize + VALUE_SIZE_FIELD + VALUE_DATA_SIZE;
        usedSpace += entrySize;
        offset += entrySize;
    }

    // Calculate required space for new entry
    size_t requiredSpace = KEY_SIZE_FIELD + keySize + VALUE_SIZE_FIELD + VALUE_DATA_SIZE;

    return (usedSpace + requiredSpace) > MAX_ENTRY_SPACE;
}

PageId HashBucketPage::getNextBucketPageId(Page* page) {
    if (!page) {
        return INVALID_PAGE_ID;
    }

    PageId nextPageId;
    memcpy(&nextPageId, page->getData() + NEXT_BUCKET_PAGE_ID_OFFSET, sizeof(nextPageId));
    return nextPageId;
}

void HashBucketPage::setNextBucketPageId(Page* page, PageId nextPageId) {
    if (!page) {
        return;
    }

    memcpy(page->getData() + NEXT_BUCKET_PAGE_ID_OFFSET, &nextPageId, sizeof(nextPageId));
}

uint32_t HashBucketPage::getNumEntries(Page* page) {
    if (!page) {
        return 0;
    }

    uint32_t numEntries;
    memcpy(&numEntries, page->getData() + NUM_ENTRIES_OFFSET, sizeof(numEntries));
    return numEntries;
}

} // namespace qindb
