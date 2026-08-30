#pragma once

#include "domain/model/Uuid.h"
#include "ui/base/DomainMetaTypes.h"

#include <QAbstractListModel>
#include <QHash>
#include <QVector>

namespace manga::ui::widget {

enum class ThumbnailStatus {
    NotLoaded,
    Loading,
    Ready,
    Failed
};

/// View-facing projection of one project page. Holds no pixmaps; pixels live in the
/// PageThumbnailList QCache keyed by PageId.
struct PageListEntry {
    domain::PageId id;
    int orderIndex{0};
    QString displayName;
    double aspectRatio{1.0};
    bool operator==(const PageListEntry&) const = default;
};

class PageListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        PageIdRole = Qt::UserRole,
        DisplayNameRole,
        OrderIndexRole,
        AspectRatioRole,
        ThumbnailStatusRole,
        ThumbnailErrorRole,
    };

    explicit PageListModel(QObject* parent = nullptr);

    /// Replaces the page projection. No reset when the content is unchanged.
    void setPages(const QVector<PageListEntry>& pages);
    [[nodiscard]] std::optional<domain::PageId> pageIdAt(int row) const;
    [[nodiscard]] int rowForPageId(const domain::PageId& pageId) const;
    void setThumbnailStatus(const domain::PageId& pageId, ThumbnailStatus status, QString errorSummary = {});

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

private:
    struct Row {
        PageListEntry entry;
        ThumbnailStatus thumbnailStatus{ThumbnailStatus::NotLoaded};
        QString thumbnailError;
        bool operator==(const Row&) const = default;
    };
    QVector<Row> m_rows;
    QHash<QString, int> m_rowByPageId; // key: PageId string
};

} // namespace manga::ui::widget
