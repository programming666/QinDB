#include "qindb/buffer_pool_manager.h"
#include "qindb/logger.h"
#include <algorithm>

namespace qindb {

BufferPoolManager::BufferPoolManager(size_t poolSize, DiskManager* diskManager)
    : poolSize_(poolSize)
    , frames_(poolSize)
    , pages_(poolSize)
    , clockHand_(0)
    , diskManager_(diskManager)
    , hitCount_(0)
    , missCount_(0)
{
    LOG_INFO(QString("Initializing BufferPoolManager with pool size: %1").arg(poolSize));

    // 初始化页对象池
    for (size_t i = 0; i < poolSize; ++i) {
        pages_[i] = new Page();
        frames_[i].page = pages_[i];
        freeList_.push_back(i);
    }

    LOG_INFO("BufferPoolManager initialized successfully");
}

BufferPoolManager::~BufferPoolManager() {
    LOG_INFO("Shutting down BufferPoolManager");

    // 刷新所有脏页
    flushAllPages();

    // 释放页对象
    for (Page* page : pages_) {
        delete page;
    }

    LOG_INFO("BufferPoolManager shut down");
}

Page* BufferPoolManager::fetchPage(PageId pageId) {
    QMutexLocker locker(&mutex_);

    if (pageId == INVALID_PAGE_ID) {
        LOG_ERROR("Attempted to fetch invalid page");
        return nullptr;
    }

    // 检查页是否已经在缓冲池中
    auto it = pageTable_.find(pageId);
    if (it != pageTable_.end()) {
        // 缓存命中
        size_t frameIdx = it.value();
        Frame& frame = frames_[frameIdx];

        frame.pinCount++;
        frame.lastAccessTime = ++hitCount_;  // 使用命中计数作为时间戳

        LOG_DEBUG(QString("Page %1 found in buffer pool (frame %2, pin count: %3)")
                      .arg(pageId).arg(frameIdx).arg(frame.pinCount));

        return frame.page;
    }

    // 缓存未命中
    missCount_++;

    // 查找空闲帧或替换帧
    int frameIdx = -1;

    if (!freeList_.empty()) {
        // 有空闲帧
        frameIdx = freeList_.front();
        freeList_.pop_front();
        LOG_DEBUG(QString("Using free frame %1 for page %2").arg(frameIdx).arg(pageId));
    } else {
        // 需要替换
        frameIdx = findVictim();
        if (frameIdx == -1) {
            LOG_ERROR("All pages are pinned, cannot evict");
            return nullptr;
        }

        Frame& victim = frames_[frameIdx];

        // 如果被替换的页是脏页，先刷新
        if (victim.isDirty) {
            if (!diskManager_->writePage(victim.pageId, victim.page)) {
                LOG_ERROR(QString("Failed to flush victim page %1").arg(victim.pageId));
                return nullptr;
            }
        }

        // 从页表中移除旧页
        pageTable_.remove(victim.pageId);
        LOG_DEBUG(QString("Evicted page %1 from frame %2").arg(victim.pageId).arg(frameIdx));
    }

    // 从磁盘读取新页
    Frame& frame = frames_[frameIdx];
    if (!diskManager_->readPage(pageId, frame.page)) {
        LOG_ERROR(QString("Failed to read page %1 from disk").arg(pageId));
        freeList_.push_back(frameIdx);  // 归还帧
        return nullptr;
    }

    // 更新帧信息
    frame.pageId = pageId;
    frame.isDirty = false;
    frame.pinCount = 1;
    frame.lastAccessTime = hitCount_ + missCount_;

    // 更新页表
    pageTable_[pageId] = frameIdx;

    LOG_DEBUG(QString("Loaded page %1 into frame %2").arg(pageId).arg(frameIdx));

    return frame.page;
}

Page* BufferPoolManager::newPage(PageId* pageId) {
    QMutexLocker locker(&mutex_);

    // 分配新页ID
    PageId newPageId = diskManager_->allocatePage();
    if (newPageId == INVALID_PAGE_ID) {
        LOG_ERROR("Failed to allocate new page");
        return nullptr;
    }

    // 查找空闲帧或替换帧
    int frameIdx = -1;

    if (!freeList_.empty()) {
        frameIdx = freeList_.front();
        freeList_.pop_front();
    } else {
        frameIdx = findVictim();
        if (frameIdx == -1) {
            LOG_ERROR("All pages are pinned, cannot create new page");
            diskManager_->deallocatePage(newPageId);
            return nullptr;
        }

        Frame& victim = frames_[frameIdx];

        // 如果被替换的页是脏页，先刷新
        if (victim.isDirty) {
            if (!diskManager_->writePage(victim.pageId, victim.page)) {
                LOG_ERROR(QString("Failed to flush victim page %1").arg(victim.pageId));
                diskManager_->deallocatePage(newPageId);
                return nullptr;
            }
        }

        pageTable_.remove(victim.pageId);
    }

    // 初始化新页
    Frame& frame = frames_[frameIdx];
    frame.page->reset();
    frame.page->setPageId(newPageId);
    frame.pageId = newPageId;
    frame.isDirty = true;  // 新页默认是脏的
    frame.pinCount = 1;
    frame.lastAccessTime = hitCount_ + missCount_;

    pageTable_[newPageId] = frameIdx;

    *pageId = newPageId;

    LOG_INFO(QString("Created new page %1 in frame %2").arg(newPageId).arg(frameIdx));

    return frame.page;
}

bool BufferPoolManager::unpinPage(PageId pageId, bool isDirty) {
    QMutexLocker locker(&mutex_);

    auto it = pageTable_.find(pageId);
    if (it == pageTable_.end()) {
        LOG_WARN(QString("Attempted to unpin page %1 which is not in buffer pool").arg(pageId));
        return false;
    }

    size_t frameIdx = it.value();
    Frame& frame = frames_[frameIdx];

    if (frame.pinCount <= 0) {
        LOG_WARN(QString("Attempted to unpin page %1 with pin count %2")
                     .arg(pageId).arg(frame.pinCount));
        return false;
    }

    frame.pinCount--;
    if (isDirty) {
        frame.isDirty = true;
        frame.page->setDirty(true);
    }

    LOG_DEBUG(QString("Unpinned page %1 (frame %2, pin count: %3, dirty: %4)")
                  .arg(pageId).arg(frameIdx).arg(frame.pinCount).arg(frame.isDirty));

    return true;
}

bool BufferPoolManager::flushPage(PageId pageId) {
    QMutexLocker locker(&mutex_);

    auto it = pageTable_.find(pageId);
    if (it == pageTable_.end()) {
        LOG_WARN(QString("Attempted to flush page %1 which is not in buffer pool").arg(pageId));
        return false;
    }

    size_t frameIdx = it.value();
    Frame& frame = frames_[frameIdx];

    if (frame.isDirty) {
        // 更新校验和
        frame.page->updateChecksum();

        if (!diskManager_->writePage(pageId, frame.page)) {
            LOG_ERROR(QString("Failed to flush page %1").arg(pageId));
            return false;
        }

        frame.isDirty = false;
        frame.page->setDirty(false);

        LOG_DEBUG(QString("Flushed page %1").arg(pageId));
    }

    return true;
}

void BufferPoolManager::flushAllPages() {
    QMutexLocker locker(&mutex_);

    LOG_INFO("Flushing all dirty pages");

    int flushedCount = 0;
    for (auto it = pageTable_.begin(); it != pageTable_.end(); ++it) {
        PageId pageId = it.key();
        size_t frameIdx = it.value();
        Frame& frame = frames_[frameIdx];

        if (frame.isDirty) {
            frame.page->updateChecksum();

            if (diskManager_->writePage(pageId, frame.page)) {
                frame.isDirty = false;
                frame.page->setDirty(false);
                flushedCount++;
            } else {
                LOG_ERROR(QString("Failed to flush page %1").arg(pageId));
            }
        }
    }

    diskManager_->flush();

    LOG_INFO(QString("Flushed %1 dirty pages").arg(flushedCount));
}

bool BufferPoolManager::deletePage(PageId pageId) {
    QMutexLocker locker(&mutex_);

    auto it = pageTable_.find(pageId);
    if (it != pageTable_.end()) {
        size_t frameIdx = it.value();
        Frame& frame = frames_[frameIdx];

        if (frame.pinCount > 0) {
            LOG_ERROR(QString("Cannot delete page %1: still pinned (pin count: %2)")
                          .arg(pageId).arg(frame.pinCount));
            return false;
        }

        // 从页表中移除
        pageTable_.remove(pageId);

        // 重置帧
        frame.page->reset();
        frame.pageId = INVALID_PAGE_ID;
        frame.isDirty = false;
        frame.pinCount = 0;

        // 归还到空闲列表
        freeList_.push_back(frameIdx);
    }

    // 释放磁盘空间
    diskManager_->deallocatePage(pageId);

    LOG_INFO(QString("Deleted page %1").arg(pageId));

    return true;
}

BufferPoolManager::Stats BufferPoolManager::getStats() const {
    QMutexLocker locker(&mutex_);

    Stats stats;
    stats.poolSize = poolSize_;
    stats.numPages = pageTable_.size();
    stats.numDirtyPages = 0;
    stats.numPinnedPages = 0;
    stats.hitCount = hitCount_;
    stats.missCount = missCount_;

    for (const auto& frame : frames_) {
        if (frame.pageId != INVALID_PAGE_ID) {
            if (frame.isDirty) {
                stats.numDirtyPages++;
            }
            if (frame.pinCount > 0) {
                stats.numPinnedPages++;
            }
        }
    }

    return stats;
}

int BufferPoolManager::findVictim() {
    // Clock (Second Chance) 算法
    // 注意：此函数假设已经持有 mutex_

    size_t startPos = clockHand_;
    Q_UNUSED(startPos);
    size_t numScanned = 0;

    while (numScanned < poolSize_ * 2) {  // 最多扫描两圈
        Frame& frame = frames_[clockHand_];

        // 跳过固定的页
        if (frame.pinCount > 0) {
            clockHand_ = (clockHand_ + 1) % poolSize_;
            numScanned++;
            continue;
        }

        // 找到页表中的页
        if (frame.pageId != INVALID_PAGE_ID) {
            // 第一次扫描：给第二次机会（设置访问位）
            // 第二次扫描：选择为牺牲者
            if (numScanned < poolSize_) {
                // 继续扫描
                clockHand_ = (clockHand_ + 1) % poolSize_;
                numScanned++;
                continue;
            } else {
                // 选择为牺牲者
                int victim = clockHand_;
                clockHand_ = (clockHand_ + 1) % poolSize_;
                return victim;
            }
        }

        clockHand_ = (clockHand_ + 1) % poolSize_;
        numScanned++;
    }

    // 如果所有页都被固定，返回-1
    return -1;
}

bool BufferPoolManager::evictPage(PageId pageId) {
    // 此函数假设已经持有 mutex_

    auto it = pageTable_.find(pageId);
    if (it == pageTable_.end()) {
        return true;  // 页不在缓冲池中
    }

    size_t frameIdx = it.value();
    Frame& frame = frames_[frameIdx];

    if (frame.pinCount > 0) {
        return false;  // 页被固定，无法驱逐
    }

    // 如果是脏页，刷新到磁盘
    if (frame.isDirty) {
        frame.page->updateChecksum();
        if (!diskManager_->writePage(pageId, frame.page)) {
            return false;
        }
    }

    // 从页表中移除
    pageTable_.remove(pageId);

    // 重置帧
    frame.page->reset();
    frame.pageId = INVALID_PAGE_ID;
    frame.isDirty = false;
    frame.pinCount = 0;

    // 归还到空闲列表
    freeList_.push_back(frameIdx);

    return true;
}

} // namespace qindb
