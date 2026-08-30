#include "ui/widget/PageThumbnailDelegate.h"

#include "ui/widget/PageListModel.h"
#include "ui/widget/PageThumbnailList.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>

namespace manga::ui::widget {

namespace {
constexpr QRect retryHotspotRect(const QRect& optionRect) {
    return QRect(optionRect.right() - 32, optionRect.bottom() - 32, 24, 24);
}
} // namespace

PageThumbnailDelegate::PageThumbnailDelegate(PageThumbnailList* list, QObject* parent)
    : QStyledItemDelegate(parent), m_list(list) {}

void PageThumbnailDelegate::setShimmerPalette(const fluent::status_info::ShimmerPainter::Palette& palette) {
    m_shimmerPalette = palette;
}

void PageThumbnailDelegate::setShimmerProgress(qreal progress) {
    m_shimmerProgress = progress;
}

QSize PageThumbnailDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return QSize(option.rect.width(), 88);
}

void PageThumbnailDelegate::paint(QPainter* painter,
                                  const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    } else if (option.state & QStyle::State_MouseOver) {
        // Just a subtle hover effect if desired, or skip
    }

    QRect thumbRect(option.rect.left() + 16, option.rect.top() + (88 - 72) / 2, 56, 72);

    auto status = static_cast<ThumbnailStatus>(index.data(PageListModel::ThumbnailStatusRole).toInt());
    auto pageId = index.data(PageListModel::PageIdRole).value<domain::PageId>();

    if (status == ThumbnailStatus::Ready) {
        const QPixmap* pixmap = m_list->thumbnail(pageId);
        if (pixmap && !pixmap->isNull()) {
            QSize scaledSize = pixmap->size().scaled(thumbRect.size(), Qt::KeepAspectRatio);
            QRect drawRect(thumbRect.center().x() - scaledSize.width() / 2,
                           thumbRect.center().y() - scaledSize.height() / 2,
                           scaledSize.width(),
                           scaledSize.height());
            painter->drawPixmap(drawRect, *pixmap);
        } else {
            // fallback if cache evicted
            fluent::status_info::ShimmerPainter::paint(
                painter,
                fluent::status_info::ShimmerPainter::imageCardElements(thumbRect, 4.0),
                m_shimmerPalette,
                m_shimmerProgress,
                true);
        }
    } else if (status == ThumbnailStatus::Loading || status == ThumbnailStatus::NotLoaded) {
        fluent::status_info::ShimmerPainter::paint(
            painter,
            fluent::status_info::ShimmerPainter::imageCardElements(thumbRect, 4.0),
            m_shimmerPalette,
            m_shimmerProgress,
            true);
    } else if (status == ThumbnailStatus::Failed) {
        painter->setPen(option.palette.text().color());
        painter->drawText(thumbRect, Qt::AlignCenter, QStringLiteral("\uE7BA"));
    }

    // Text
    QRect textRect = option.rect;
    textRect.setLeft(thumbRect.right() + 16);
    textRect.setRight(option.rect.right() - 16);

    QString displayName = index.data(PageListModel::DisplayNameRole).toString();
    painter->setPen(option.palette.text().color());
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, displayName);

    if (status == ThumbnailStatus::Failed) {
        QRect retryRect = retryHotspotRect(option.rect);
        painter->drawText(retryRect, Qt::AlignCenter, QStringLiteral("\uE72C")); // Refresh icon
    }

    painter->restore();
}

bool PageThumbnailDelegate::editorEvent(QEvent* event,
                                        QAbstractItemModel* model,
                                        const QStyleOptionViewItem& option,
                                        const QModelIndex& index) {
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        auto status = static_cast<ThumbnailStatus>(index.data(PageListModel::ThumbnailStatusRole).toInt());

        if (status == ThumbnailStatus::Failed) {
            QRect retryRect = retryHotspotRect(option.rect);
            if (retryRect.contains(mouseEvent->pos())) {
                auto pageId = index.data(PageListModel::PageIdRole).value<domain::PageId>();
                emit m_list->thumbnailRequested(pageId, true);
                return true;
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

} // namespace manga::ui::widget
