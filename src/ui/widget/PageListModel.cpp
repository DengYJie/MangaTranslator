#include "ui/widget/PageListModel.h"

namespace manga::ui::widget {

PageListModel::PageListModel(QObject* parent) : QAbstractListModel(parent) {}

void PageListModel::setPages(const QVector<PageListEntry>& pages) {
    bool changed = false;
    if (m_rows.size() != pages.size()) {
        changed = true;
    } else {
        for (int i = 0; i < pages.size(); ++i) {
            if (m_rows[i].entry != pages[i]) {
                changed = true;
                break;
            }
        }
    }

    if (!changed) {
        return;
    }

    beginResetModel();
    QHash<QString, Row> oldRows;
    for (const auto& row : std::as_const(m_rows)) {
        oldRows.insert(QString::fromStdString(row.entry.id.toString()), row);
    }

    m_rows.clear();
    m_rows.reserve(pages.size());
    m_rowByPageId.clear();
    m_rowByPageId.reserve(pages.size());

    for (int i = 0; i < pages.size(); ++i) {
        const auto& page = pages[i];
        Row newRow{page, ThumbnailStatus::NotLoaded, {}};
        QString idStr = QString::fromStdString(page.id.toString());

        auto it = oldRows.constFind(idStr);
        if (it != oldRows.constEnd()) {
            newRow.thumbnailStatus = it->thumbnailStatus;
            newRow.thumbnailError = it->thumbnailError;
        }

        m_rows.append(newRow);
        m_rowByPageId.insert(idStr, i);
    }
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
    emit dataChanged(idx, idx, {ThumbnailStatusRole, ThumbnailErrorRole, Qt::ToolTipRole});
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
