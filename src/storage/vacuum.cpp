#include "qindb/vacuum.h"
#include "qindb/logger.h"
#include "qindb/table_page.h"
#include <QThread>

namespace qindb {

VacuumWorker::VacuumWorker(TransactionManager* txnMgr,
                           BufferPoolManager* bufferPool)
    : txnMgr_(txnMgr)
    , bufferPool_(bufferPool)
    , workerThread_(nullptr)
    , running_(false)
    , intervalSeconds_(60)
{
    LOG_INFO("VacuumWorker initialized");
}

VacuumWorker::~VacuumWorker() {
    stopBackgroundWorker();
    LOG_INFO("VacuumWorker destroyed");
}

bool VacuumWorker::canDelete(const RecordHeader& header) {
    // 条件 1: 记录必须被标记为删除
    if (header.deleteTxnId == INVALID_TXN_ID) {
        return false;  // 未删除
    }

    // 条件 2: 删除事务必须已提交
    TransactionState state = txnMgr_->getTransactionState(header.deleteTxnId);
    if (state != TransactionState::COMMITTED) {
        return false;  // 删除事务未提交或已中止
    }

    // 条件 3: 没有活跃事务可能看到该记录
    // 简化实现：检查删除事务ID是否小于最小活跃事务ID
    // 真实数据库会维护一个快照列表
    //
    // 当前简化：如果删除事务已提交，且创建事务也已提交，就可以清理
    // TODO: 实现更精确的可见性判断（基于快照）
    TransactionState createState = txnMgr_->getTransactionState(header.createTxnId);
    if (createState != TransactionState::COMMITTED) {
        return false;  // 创建事务未提交
    }

    return true;  // 可以安全删除
}

int VacuumWorker::cleanupTable(const TableDef* tableDef) {
    if (!tableDef) {
        return 0;
    }

    LOG_INFO(QString("VACUUM: Cleaning up table '%1'").arg(tableDef->name));

    int deletedCount = 0;
    PageId currentPageId = tableDef->firstPageId;

    while (currentPageId != INVALID_PAGE_ID) {
        Page* page = bufferPool_->fetchPage(currentPageId);
        if (!page) {
            LOG_ERROR(QString("VACUUM: Failed to fetch page %1").arg(currentPageId));
            break;
        }

        PageHeader* pageHeader = page->getHeader();
        Slot* slotArray = reinterpret_cast<Slot*>(page->getData() + sizeof(PageHeader));

        // 收集需要删除的槽位（倒序删除以避免索引问题）
        QVector<int> slotsToDelete;

        for (uint16_t i = 0; i < pageHeader->slotCount; ++i) {
            const Slot& slot = slotArray[i];

            if (slot.length == 0) {
                continue;  // 空槽位
            }

            // 读取记录头
            const char* recordData = page->getData() + slot.offset;
            QByteArray byteArray = QByteArray::fromRawData(recordData, slot.length);
            QDataStream stream(byteArray);
            stream.setByteOrder(QDataStream::LittleEndian);

            RecordHeader recordHeader;
            if (stream.readRawData(reinterpret_cast<char*>(&recordHeader), sizeof(RecordHeader)) != sizeof(RecordHeader)) {
                continue;
            }

            // 检查是否可以删除
            if (canDelete(recordHeader)) {
                slotsToDelete.append(i);
            }
        }

        // 物理删除记录（倒序以保持索引正确性）
        for (int i = slotsToDelete.size() - 1; i >= 0; --i) {
            int slotIndex = slotsToDelete[i];
            Slot& slot = slotArray[slotIndex];

            // 标记槽位为空（物理删除）
            slot.offset = 0;
            slot.length = 0;

            deletedCount++;
        }

        if (!slotsToDelete.isEmpty()) {
            // 标记页面为脏
            bufferPool_->unpinPage(currentPageId, true);

            LOG_DEBUG(QString("VACUUM: Cleaned %1 records from page %2")
                         .arg(slotsToDelete.size())
                         .arg(currentPageId));
        } else {
            bufferPool_->unpinPage(currentPageId, false);
        }

        // 移动到下一页
        PageId nextPageId = pageHeader->nextPageId;
        currentPageId = nextPageId;
    }

    LOG_INFO(QString("VACUUM: Cleaned %1 records from table '%2'")
                .arg(deletedCount)
                .arg(tableDef->name));

    return deletedCount;
}

void VacuumWorker::startBackgroundWorker(int intervalSeconds) {
    if (running_) {
        LOG_WARN("VACUUM: Background worker already running");
        return;
    }

    intervalSeconds_ = intervalSeconds;
    running_ = true;

    workerThread_ = QThread::create([this]() {
        this->backgroundWork();
    });

    workerThread_->start();

    LOG_INFO(QString("VACUUM: Background worker started (interval=%1s)").arg(intervalSeconds));
}

void VacuumWorker::stopBackgroundWorker() {
    if (!running_) {
        return;
    }

    {
        QMutexLocker locker(&mutex_);
        running_ = false;
        condition_.wakeAll();  // 唤醒等待线程
    }

    if (workerThread_ && workerThread_->isRunning()) {
        workerThread_->wait();  // 等待线程结束
        delete workerThread_;
        workerThread_ = nullptr;
    }

    LOG_INFO("VACUUM: Background worker stopped");
}

void VacuumWorker::backgroundWork() {
    LOG_INFO("VACUUM: Background worker thread started");

    while (running_) {
        {
            QMutexLocker locker(&mutex_);

            // 等待指定时间，或被停止信号唤醒
            condition_.wait(&mutex_, intervalSeconds_ * 1000);

            if (!running_) {
                break;
            }
        }

        // TODO: 获取所有表并清理
        // 当前简化实现：仅记录日志
        LOG_DEBUG("VACUUM: Background cleanup cycle (skipped - no catalog access)");

        // 真实实现需要：
        // 1. 从 Catalog 获取所有表
        // 2. 对每个表调用 cleanupTable()
        // 3. 刷新脏页
    }

    LOG_INFO("VACUUM: Background worker thread stopped");
}

} // namespace qindb
