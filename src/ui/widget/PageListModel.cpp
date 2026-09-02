#include "ui/widget/PageListModel.h"

namespace manga::ui::widget {

PageListModel::PageListModel(QObject* parent) : QAbstractListModel(parent) {}

void PageListModel::setPages(const QVector<PageListEntry>& pages) {
    const int oldCount = static_cast<int>(m_rows.size());
    const int newCount = static_cast<int>(pages.size());

    int prefix = 0;
    while (prefix < oldCount && prefix < newCount && m_rows[prefix].entry == pages[prefix]) {
        ++prefix;
    }

    // The suffix is trimmed only after the prefix so the two ranges never overlap.
    int suffix = 0;
    while (suffix < oldCount - prefix && suffix < newCount - prefix &&
           m_rows[oldCount - suffix - 1].entry == pages[newCount - suffix - 1]) {
        ++suffix;
    }

    const int oldMid = oldCount - prefix - suffix;
    const int newMid = newCount - prefix - suffix;
    if (oldMid == 0 && newMid == 0) {
        return;
    }

    if (oldMid > 0 && newMid > 0 && oldMid == newMid) {
        bool sameIds = true;
        for (int i = 0; i < newMid; ++i) {
            if (m_rows[prefix + i].entry.id != pages[prefix + i].id) {
                sameIds = false;
                break;
            }
        }

        if (sameIds) {
            // Only the projection changes, so every row keeps its thumbnail and processing
            // state and the views receive a data change instead of a structural one.
            for (int i = 0; i < newMid; ++i) {
                m_rows[prefix + i].entry = pages[prefix + i];
            }
            Q_EMIT dataChanged(index(prefix), index(prefix + newMid - 1));
            return;
        }
    }

    if (oldMid == 0 && newMid > 0) {
        insertRowsFromPages(pages, prefix, newMid);
        rebuildRowIndex();
        return;
    }

    if (newMid == 0 && oldMid > 0) {
        beginRemoveRows(QModelIndex(), prefix, prefix + oldMid - 1);
        m_rows.remove(prefix, oldMid);
        endRemoveRows();
        rebuildRowIndex();
        return;
    }

    // Inserting or removing a page in the middle renumbers the orderIndex of every following
    // page, so the entry comparison above cannot trim them. A second trim that compares page
    // ids only still recognizes such edits as a pure insertion or a pure removal.
    int idPrefix = 0;
    while (idPrefix < oldCount && idPrefix < newCount && m_rows[idPrefix].entry.id == pages[idPrefix].id) {
        ++idPrefix;
    }

    int idSuffix = 0;
    while (idSuffix < oldCount - idPrefix && idSuffix < newCount - idPrefix &&
           m_rows[oldCount - idSuffix - 1].entry.id == pages[newCount - idSuffix - 1].id) {
        ++idSuffix;
    }

    const int idOldMid = oldCount - idPrefix - idSuffix;
    const int idNewMid = newCount - idPrefix - idSuffix;

    if (idOldMid == 0 && idNewMid > 0) {
        insertRowsFromPages(pages, idPrefix, idNewMid);
        rebuildRowIndex();
        syncEntriesFrom(pages, prefix);
        return;
    }

    if (idNewMid == 0 && idOldMid > 0) {
        beginRemoveRows(QModelIndex(), idPrefix, idPrefix + idOldMid - 1);
        m_rows.remove(idPrefix, idOldMid);
        endRemoveRows();
        rebuildRowIndex();
        syncEntriesFrom(pages, prefix);
        return;
    }

    resetWithPreservedState(pages);
}

void PageListModel::insertRowsFromPages(const QVector<PageListEntry>& pages, int first, int count) {
    // Per-row state is carried over by page id so a re-render never downgrades a thumbnail
    // that was already requested for a page that came back.
    const QHash<QString, Row> oldRows = snapshotRowsById();

    QVector<Row> inserted;
    inserted.reserve(count);
    for (int i = 0; i < count; ++i) {
        inserted.append(makeRow(pages[first + i], oldRows));
    }

    m_rows.reserve(m_rows.size() + count);
    beginInsertRows(QModelIndex(), first, first + count - 1);
    m_rows.insert(first, count, Row{});
    for (int i = 0; i < count; ++i) {
        m_rows[first + i] = inserted[i];
    }
    endInsertRows();
}

void PageListModel::syncEntriesFrom(const QVector<PageListEntry>& pages, int first) {
    // Rows below first are already up to date. The rest takes the incoming projection after a
    // structural edit shifted them, which leaves their thumbnail and processing state alone.
    for (int i = first; i < pages.size(); ++i) {
        m_rows[i].entry = pages[i];
    }

    if (first < pages.size()) {
        Q_EMIT dataChanged(index(first), index(static_cast<int>(pages.size()) - 1));
    }
}

QHash<QString, PageListModel::Row> PageListModel::snapshotRowsById() const {
    QHash<QString, Row> snapshot;
    snapshot.reserve(m_rows.size());
    for (const auto& row : m_rows) {
        snapshot.insert(QString::fromStdString(row.entry.id.toString()), row);
    }
    return snapshot;
}

PageListModel::Row PageListModel::makeRow(const PageListEntry& entry, const QHash<QString, Row>& oldRows) const {
    Row row{entry, ThumbnailStatus::NotLoaded, {}, 0};
    const auto it = oldRows.constFind(QString::fromStdString(entry.id.toString()));
    if (it != oldRows.constEnd()) {
        row.thumbnailStatus = it->thumbnailStatus;
        row.thumbnailError = it->thumbnailError;
        row.processingStatus = it->processingStatus;
    }
    return row;
}

void PageListModel::rebuildRowIndex() {
    m_rowByPageId.clear();
    m_rowByPageId.reserve(m_rows.size());
    for (int i = 0; i < m_rows.size(); ++i) {
        m_rowByPageId.insert(QString::fromStdString(m_rows[i].entry.id.toString()), i);
    }
}

void PageListModel::resetWithPreservedState(const QVector<PageListEntry>& pages) {
    const QHash<QString, Row> oldRows = snapshotRowsById();

    beginResetModel();
    m_rows.clear();
    m_rows.reserve(pages.size());
    for (const auto& page : pages) {
        m_rows.append(makeRow(page, oldRows));
    }
    rebuildRowIndex();
    endResetModel();
}

std::optional<domain::PageId> PageListModel::pageIdAt(int row) const {
    if (row < 0 || row >= m_rows.size()) {
        return std::nullopt;
    }
    return m_rows[row].entry.id;
}

int PageListModel::rowForPageId(const domain::PageId& pageId) const {
    QString idStr = QString::fromStdString(pageId.toString());
    return m_rowByPageId.value(idStr, -1);
}

void PageListModel::setThumbnailStatus(const domain::PageId& pageId, ThumbnailStatus status, QString errorSummary) {
    int row = rowForPageId(pageId);
    if (row < 0) {
        return;
    }

    if (m_rows[row].thumbnailStatus == status && m_rows[row].thumbnailError == errorSummary) {
        return;
    }

    m_rows[row].thumbnailStatus = status;
    m_rows[row].thumbnailError = std::move(errorSummary);

    QModelIndex idx = index(row);
    Q_EMIT dataChanged(idx, idx, {ThumbnailStatusRole, ThumbnailErrorRole, Qt::ToolTipRole});
}

int PageListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

QVariant PageListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }

    const auto& row = m_rows[index.row()];

    switch (role) {
        case PageIdRole:
            return QVariant::fromValue(row.entry.id);
        case DisplayNameRole:
            return row.entry.displayName;
        case OrderIndexRole:
            return row.entry.orderIndex;
        case AspectRatioRole:
            return row.entry.aspectRatio;
        case ThumbnailStatusRole:
            return static_cast<int>(row.thumbnailStatus);
        case ThumbnailErrorRole:
            return row.thumbnailError;
        case ProcessingStatusRole:
            return row.processingStatus;
        case Qt::AccessibleTextRole:
            return tr("第 %1 页，%2").arg(row.entry.orderIndex + 1).arg(row.entry.displayName);
        case Qt::ToolTipRole:
            if (!row.thumbnailError.isEmpty()) {
                return row.entry.displayName + QStringLiteral("\n") + row.thumbnailError;
            }
            return row.entry.displayName;
        default:
            break;
    }
    return {};
}

void PageListModel::setProcessingStatus(const domain::PageId& pageId, int status) {
    int row = rowForPageId(pageId);
    if (row < 0) {
        return;
    }
    if (m_rows[row].processingStatus == status) {
        return;
    }
    m_rows[row].processingStatus = status;
    QModelIndex idx = index(row);
    Q_EMIT dataChanged(idx, idx, {ProcessingStatusRole});
}

} // namespace manga::ui::widget
