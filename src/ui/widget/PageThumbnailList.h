#pragma once

#include "domain/model/Uuid.h"

#include <FluentQt/Foundation.h>
#include <FluentQt/StatusInfo.h>
#include <QCache>
#include <QPixmap>
#include <QWidget>

class QTimer;
class QListView;

namespace fluent::textfields {
class Label;
}

namespace manga::ui::widget {

class PageListModel;
class PageThumbnailDelegate;

/// Editor page pane: virtualized page list with lazy thumbnails.
/// Owns the 64 MiB QCache pixel cache; the model stores only PageId, aspect and status.
class PageThumbnailList : public QWidget, public fluent::FluentElement, public fluent::QMLPlus {
    Q_OBJECT
public:
    explicit PageThumbnailList(QWidget* parent = nullptr);

    void onThemeUpdated() override;

    [[nodiscard]] PageListModel* model() const {
        return m_model;
    }
    void setCurrentPageId(const domain::PageId& pageId);

    /// Requests thumbnails for visible rows plus two rows of lookahead. Loading rows
    /// are not requested again.
    void requestVisibleThumbnails();

    /// GUI thread: converts to QPixmap, inserts into the bounded cache, marks the row Ready.
    void insertThumbnail(const domain::PageId& pageId, const QImage& image);
    void markThumbnailFailed(const domain::PageId& pageId, const QString& safeMessage);
    /// Generation bump: in-flight binds are discarded, Loading rows return to NotLoaded.
    void resetLoadingThumbnails();
    void setPageProcessingStatus(const domain::PageId& pageId, int status);

    [[nodiscard]] const QPixmap* thumbnail(const domain::PageId& pageId) const; // delegate lookup
    [[nodiscard]] int thumbnailCacheTotalCostForTest() const;
    void requestThumbnailForTest(const domain::PageId& pageId, bool force);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

Q_SIGNALS:
    void pageActivated(const ::manga::domain::PageId& pageId);
    void pagesSelected(const QVector<::manga::domain::PageId>& pageIds);
    void thumbnailRequested(const ::manga::domain::PageId& pageId, bool force);

private:
    void updateCountLabel();

    fluent::textfields::Label* m_headerTitle{nullptr};
    fluent::textfields::Label* m_headerCount{nullptr};
    PageListModel* m_model{nullptr};            // owned by this
    PageThumbnailDelegate* m_delegate{nullptr}; // owned by view
    QListView* m_view{nullptr};
    QCache<QString, QPixmap> m_thumbnailCache; // cost in KiB, max 64 * 1024
    QTimer* m_shimmerTimer{nullptr};
    qreal m_shimmerProgress{0.0};
    fluent::status_info::ShimmerPainter::Palette m_shimmerPalette;
};

} // namespace manga::ui::widget
