#include "ui/widget/PageThumbnailDelegate.h"

#include "ui/screen/editor/EditorState.h"
#include "ui/widget/PageListModel.h"
#include "ui/widget/PageThumbnailList.h"

#include <FluentQt/Foundation.h>
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

    const auto& colors = m_list->themeColorsRef();

    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, colors.subtleSecondary);
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
        QFont warnFont(Typography::FontFamily::FluentIcons);
        warnFont.setPixelSize(16);
        painter->setFont(warnFont);
        painter->setPen(colors.textPrimary);
        painter->drawText(thumbRect, Qt::AlignCenter, QStringLiteral("\uE7BA"));
    }

    // Text: page number primary, file name secondary.
    QRect textRect = option.rect;
    textRect.setLeft(thumbRect.right() + 16);
    textRect.setRight(option.rect.right() - 16);
    QRect topRect(textRect.left(), textRect.top(), textRect.width(), textRect.height() / 2);
    QRect bottomRect(textRect.left(), topRect.bottom(), textRect.width(), textRect.height() - topRect.height());

    const int orderIndex = index.data(PageListModel::OrderIndexRole).toInt();
    QFont primaryFont = option.font;
    primaryFont.setWeight(QFont::DemiBold);
    painter->setFont(primaryFont);
    painter->setPen(colors.textPrimary);
    painter->drawText(topRect, Qt::AlignVCenter | Qt::AlignLeft, tr("第 %1 页").arg(orderIndex + 1));

    QString displayName = index.data(PageListModel::DisplayNameRole).toString();
    painter->setFont(option.font);
    painter->setPen(colors.textSecondary);
    painter->drawText(bottomRect, Qt::AlignVCenter | Qt::AlignLeft, displayName);

    if (status == ThumbnailStatus::Failed) {
        QFont retryFont(Typography::FontFamily::FluentIcons);
        retryFont.setPixelSize(14);
        painter->setFont(retryFont);
        painter->setPen(colors.textSecondary);
        painter->drawText(retryHotspotRect(option.rect), Qt::AlignCenter, QStringLiteral("\uE72C"));
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
            if (retryHotspotRect(option.rect).contains(mouseEvent->pos())) {
                auto pageId = index.data(PageListModel::PageIdRole).value<domain::PageId>();
                Q_EMIT m_list->thumbnailRequested(pageId, true);
                return true;
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

} // namespace manga::ui::widget
