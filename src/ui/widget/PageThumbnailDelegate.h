#pragma once

#include <FluentQt/FluentQt.h>
#include <QStyledItemDelegate>

namespace manga::ui::widget {

class PageThumbnailList;

class PageThumbnailDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit PageThumbnailDelegate(PageThumbnailList* list, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

    void setShimmerPalette(const fluent::status_info::ShimmerPainter::Palette& palette);
    void setShimmerProgress(qreal progress);

private:
    PageThumbnailList* m_list{nullptr};
    fluent::status_info::ShimmerPainter::Palette m_shimmerPalette;
    qreal m_shimmerProgress{0.0};
};

} // namespace manga::ui::widget
