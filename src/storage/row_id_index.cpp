#include "qindb/row_id_index.h"
#include "qindb/logger.h"

namespace qindb {

RowIdIndex::RowIdIndex() {
}

RowIdIndex::~RowIdIndex() {
}

void RowIdIndex::insert(RowId rowId, const RowLocation& location) {
    QMutexLocker locker(&mutex_);

    if (rowId == INVALID_ROW_ID || !location.isValid()) {
        LOG_ERROR(QString("Invalid rowId or location: rowId=%1, pageId=%2")
                    .arg(rowId)
                    .arg(location.pageId));
        return;
    }

    index_[rowId] = location;

    LOG_DEBUG(QString("RowIdIndex: inserted rowId=%1 -> (pageId=%2, slot=%3)")
                .arg(rowId)
                .arg(location.pageId)
                .arg(location.slotIndex));
}

void RowIdIndex::remove(RowId rowId) {
    QMutexLocker locker(&mutex_);

    if (index_.remove(rowId) > 0) {
        LOG_DEBUG(QString("RowIdIndex: removed rowId=%1").arg(rowId));
    }
}

bool RowIdIndex::lookup(RowId rowId, RowLocation& location) const {
    QMutexLocker locker(&mutex_);

    auto it = index_.find(rowId);
    if (it != index_.end()) {
        location = it.value();
        return true;
    }

    return false;
}

bool RowIdIndex::update(RowId rowId, const RowLocation& newLocation) {
    QMutexLocker locker(&mutex_);

    auto it = index_.find(rowId);
    if (it == index_.end()) {
        LOG_WARN(QString("RowIdIndex: rowId=%1 not found for update").arg(rowId));
        return false;
    }

    it.value() = newLocation;

    LOG_DEBUG(QString("RowIdIndex: updated rowId=%1 -> (pageId=%2, slot=%3)")
                .arg(rowId)
                .arg(newLocation.pageId)
                .arg(newLocation.slotIndex));

    return true;
}

void RowIdIndex::clear() {
    QMutexLocker locker(&mutex_);
    index_.clear();
    LOG_DEBUG("RowIdIndex: cleared all mappings");
}

int RowIdIndex::size() const {
    QMutexLocker locker(&mutex_);
    return index_.size();
}

QVector<RowId> RowIdIndex::getAllRowIds() const {
    QMutexLocker locker(&mutex_);
    return index_.keys().toVector();
}

} // namespace qindb
