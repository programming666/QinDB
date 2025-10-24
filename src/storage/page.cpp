#include "qindb/page.h"
#include "qindb/logger.h"
#include <cstring>

namespace qindb {

Page::Page()
    : pinCount_(0)
    , isDirty_(false)
{
    reset();
}

Page::~Page() {
    // 无需特殊清理
}

PageId Page::getPageId() const {
    return getHeader()->pageId;
}

void Page::setPageId(PageId pageId) {
    getHeader()->pageId = pageId;
}

PageType Page::getPageType() const {
    return getHeader()->pageType;
}

void Page::setPageType(PageType type) {
    getHeader()->pageType = type;
}

PageId Page::getNextPageId() const {
    return getHeader()->nextPageId;
}

void Page::setNextPageId(PageId nextPageId) {
    getHeader()->nextPageId = nextPageId;
}

PageId Page::getPrevPageId() const {
    return getHeader()->prevPageId;
}

void Page::setPrevPageId(PageId prevPageId) {
    getHeader()->prevPageId = prevPageId;
}

void Page::reset() {
    std::memset(data_, 0, PAGE_SIZE);

    // 初始化页头
    PageHeader* header = getHeader();
    new (header) PageHeader();

    pinCount_.store(0);
    isDirty_.store(false);
}

uint32_t Page::calculateChecksum() const {
    // 简单的校验和算法：对所有字节求和（跳过校验和字段本身）
    uint32_t checksum = 0;
    const char* ptr = data_;

    // 跳过 PageHeader 中的 checksum 字段（最后4字节）
    size_t checksumOffset = offsetof(PageHeader, checksum);

    // 计算 checksum 字段之前的部分
    for (size_t i = 0; i < checksumOffset; ++i) {
        checksum += static_cast<uint8_t>(ptr[i]);
    }

    // 跳过 checksum 字段，计算之后的部分
    for (size_t i = sizeof(PageHeader); i < PAGE_SIZE; ++i) {
        checksum += static_cast<uint8_t>(ptr[i]);
    }

    return checksum;
}

bool Page::verifyChecksum() const {
    uint32_t stored = getHeader()->checksum;
    uint32_t calculated = calculateChecksum();
    return stored == calculated;
}

void Page::updateChecksum() {
    getHeader()->checksum = calculateChecksum();
}

// ============ DatabaseHeader 实现 ============

uint32_t DatabaseHeader::calculateChecksum() const {
    // CRC32校验和：对所有字节求和（跳过校验和字段本身）
    uint32_t checksum = 0;
    const char* ptr = reinterpret_cast<const char*>(this);

    // 计算 checksum 字段之前的部分
    size_t checksumOffset = offsetof(DatabaseHeader, checksum);
    for (size_t i = 0; i < checksumOffset; ++i) {
        checksum += static_cast<uint8_t>(ptr[i]);
    }

    // 跳过 checksum 字段（4字节），计算之后的部分
    size_t afterChecksum = checksumOffset + sizeof(uint32_t);
    for (size_t i = afterChecksum; i < sizeof(DatabaseHeader); ++i) {
        checksum += static_cast<uint8_t>(ptr[i]);
    }

    return checksum;
}

bool DatabaseHeader::verifyChecksum() const {
    return checksum == calculateChecksum();
}

void DatabaseHeader::updateChecksum() {
    checksum = calculateChecksum();
}

} // namespace qindb
