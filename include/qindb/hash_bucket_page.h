#ifndef HASH_BUCKET_PAGE_H
#define HASH_BUCKET_PAGE_H

#include "qindb/common.h"
#include "qindb/page.h"
#include <QByteArray>
#include <QVariant>
#include <vector>

namespace qindb {

/**
 * @brief Hash bucket page for hash index
 *
 * Page layout:
 * - Header (32 bytes):
 *   - numEntries (4 bytes): Number of key-value pairs in this bucket
 *   - nextBucketPageId (4 bytes): Overflow bucket page ID (0 if none)
 *   - reserved (24 bytes): Reserved for future use
 * - Entries (variable length):
 *   - Each entry: keySize (4 bytes) + key (variable) + valueSize (4 bytes) + value (8 bytes for RowId)
 */
class HashBucketPage {
public:
    HashBucketPage() = default;
    ~HashBucketPage() = default;

    // Initialize a new hash bucket page
    static void initialize(Page* page);

    // Insert a key-value pair into the bucket
    // Returns true if successful, false if bucket is full
    static bool insert(Page* page, const QByteArray& key, RowId value);

    // Search for a key in the bucket
    // Returns true if found, and sets 'value' to the corresponding RowId
    static bool search(Page* page, const QByteArray& key, RowId& value);
    static bool searchAll(Page* page, const QByteArray& key, std::vector<RowId>& values);

    // Remove a key from the bucket
    // Returns true if found and removed, false otherwise
    static bool remove(Page* page, const QByteArray& key, RowId value = INVALID_ROW_ID);

    // Get all key-value pairs in this bucket
    static std::vector<std::pair<QByteArray, RowId>> getAll(Page* page);

    // Check if the bucket is full (cannot insert more entries)
    static bool isFull(Page* page, size_t keySize);

    // Get the next overflow bucket page ID
    static PageId getNextBucketPageId(Page* page);

    // Set the next overflow bucket page ID
    static void setNextBucketPageId(Page* page, PageId nextPageId);

    // Get the number of entries in this bucket
    static uint32_t getNumEntries(Page* page);

private:
    // Header offsets
    static constexpr size_t NUM_ENTRIES_OFFSET = 0;
    static constexpr size_t NEXT_BUCKET_PAGE_ID_OFFSET = 4;
    static constexpr size_t HEADER_SIZE = 32;

    // Entry layout: keySize (4) + key (variable) + valueSize (4) + value (8)
    static constexpr size_t KEY_SIZE_FIELD = 4;
    static constexpr size_t VALUE_SIZE_FIELD = 4;
    static constexpr size_t VALUE_DATA_SIZE = 8; // RowId is uint64_t

    // Maximum usable space for entries
    static constexpr size_t MAX_ENTRY_SPACE = PAGE_SIZE - HEADER_SIZE;
};

} // namespace qindb

#endif // HASH_BUCKET_PAGE_H
