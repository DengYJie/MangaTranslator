#include "ui/screen/home/RecentProjectCardDelegate.h"

#include "ui/screen/home/RecentProjectsModel.h"
#include "ui/screen/home/RelativeTime.h"

#include <FluentQt/Design.h>
#include <FluentQt/Foundation.h>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>

namespace manga::ui::screen::home {

namespace {
constexpr int kCardPadding = 4; // breathing room inside the grid cell
constexpr int kCardRadius = 8;
constexpr int kCoverGlyphSize = 32;
constexpr int kTextBlockHeight = 56; // name + timestamp below the cover
constexpr int kTextPadding = 12;
} // namespace

RecentProjectCardDelegate::RecentProjectCardDelegate(fluent::FluentElement* themeSource, QObject* parent)
    : QStyledItemDelegate(parent), m_themeSource(themeSource) {}

QSize RecentProjectCardDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Q_UNUSED(option);
    Q_UNUSED(index);
    // Cell geometry is owned by the GridView (cellSize); the hint only needs to agree.
    return {240, 216};
}

void RecentProjectCardDelegate::paint(QPainter* painter,
                                      const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const fluent::FluentElement::Colors tokens =
        m_themeSource ? m_themeSource->themeColorsRef() : fluent::FluentElement::Colors{};

    const QRect cardRect = option.rect.adjusted(kCardPadding, kCardPadding, -kCardPadding, -kCardPadding);

    QPainterPath cardPath;
    cardPath.addRoundedRect(cardRect, kCardRadius, kCardRadius);

    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    painter->fillPath(cardPath, hovered ? tokens.controlSecondary : tokens.controlDefault);
    painter->setPen(QPen(selected ? tokens.accentDefault : tokens.strokeCard, selected ? 2 : 1));
    painter->drawPath(cardPath);

    // Keep the cover area stable while asynchronous previews arrive.
    const QRect coverRect(cardRect.left() + kTextPadding,
                          cardRect.top() + kTextPadding,
                          cardRect.width() - 2 * kTextPadding,
                          qRound((cardRect.width() - 2 * kTextPadding) * 10.0 / 16.0));
    painter->setPen(Qt::NoPen);
    painter->setBrush(tokens.bgLayerAlt);
    painter->drawRoundedRect(coverRect, 6, 6);
    const auto cover = index.data(RecentProjectsModel::CoverRole).value<QImage>();
    if (!cover.isNull()) {
        painter->save();
        QPainterPath clip;
        clip.addRoundedRect(coverRect, 6, 6);
        painter->setClipPath(clip);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        const QSize imageSize = cover.size().scaled(coverRect.size(), Qt::KeepAspectRatio);
        const QRect imageRect(coverRect.x() + (coverRect.width() - imageSize.width()) / 2,
                              coverRect.y() + (coverRect.height() - imageSize.height()) / 2,
                              imageSize.width(),
                              imageSize.height());
        painter->drawImage(imageRect, cover);
        painter->restore();
    } else {
        QFont glyphFont(Typography::FontFamily::FluentIcons);
        glyphFont.setPixelSize(kCoverGlyphSize);
        painter->setFont(glyphFont);
        painter->setPen(tokens.textTertiary);
        painter->drawText(coverRect, Qt::AlignCenter, Typography::Icons::Document);
    }

    // Name and relative time under the cover.
    const QRect textRect(cardRect.left() + kTextPadding,
                         coverRect.bottom() + 8,
                         cardRect.width() - 2 * kTextPadding,
                         kTextBlockHeight - 8);
    QFont nameFont = option.font;
    nameFont.setWeight(QFont::DemiBold);
    painter->setFont(nameFont);
    painter->setPen(tokens.textPrimary);
    const QString name = index.data(RecentProjectsModel::NameRole).toString();
    const QRect nameRect(textRect.left(), textRect.top(), textRect.width(), textRect.height() / 2);
    painter->drawText(nameRect,
                      Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(name, Qt::ElideRight, nameRect.width()));

    QFont timeFont = option.font;
    timeFont.setPixelSize(12);
    painter->setFont(timeFont);
    painter->setPen(tokens.textSecondary);
    const qint64 lastOpenedMs = index.data(RecentProjectsModel::LastOpenedRole).toLongLong();
    const QString timeText = formatRelativeTime(lastOpenedMs, QDateTime::currentMSecsSinceEpoch());
    const QRect timeRect(
        textRect.left(), textRect.top() + textRect.height() / 2, textRect.width(), textRect.height() / 2);
    painter->drawText(timeRect, Qt::AlignLeft | Qt::AlignVCenter, timeText);

    if (option.state.testFlag(QStyle::State_HasFocus)) {
        // Focus draws one outer ring; this card has no inner ring.
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(tokens.strokeFocusOuter, 2));
        painter->drawRoundedRect(cardRect.adjusted(-1, -1, 1, 1), kCardRadius + 1, kCardRadius + 1);
    }

    painter->restore();
}

} // namespace manga::ui::screen::home
