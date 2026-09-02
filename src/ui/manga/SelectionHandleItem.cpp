#include "ui/manga/SelectionHandleItem.h"

#include "ui/manga/CanvasZOrder.h"

#include <QBrush>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace manga::ui::manga {

SelectionHandleItem::SelectionHandleItem(canvas::HandlePosition position, QGraphicsItem* parent)
    : QGraphicsRectItem(parent), m_position(position) {
    setFlag(ItemIgnoresTransformations, true);
    setZValue(canvas::kZSelectionHandles);

    const double halfVisual = canvas::kHandleVisualSizeDevicePx * 0.5;
    setRect(-halfVisual, -halfVisual, canvas::kHandleVisualSizeDevicePx, canvas::kHandleVisualSizeDevicePx);

    setPen(Qt::NoPen);
    setBrush(Qt::NoBrush);
}

QRectF SelectionHandleItem::boundingRect() const {
    const double halfHit = canvas::kHandleHitSizeDevicePx * 0.5;
    return {-halfHit, -halfHit, canvas::kHandleHitSizeDevicePx, canvas::kHandleHitSizeDevicePx};
}

QPainterPath SelectionHandleItem::shape() const {
    const double halfHit = canvas::kHandleHitSizeDevicePx * 0.5;
    QPainterPath path;
    path.addRect(-halfHit, -halfHit, canvas::kHandleHitSizeDevicePx, canvas::kHandleHitSizeDevicePx);
    return path;
}

void SelectionHandleItem::setEdgesOnly(bool edgesOnly) {
    if (m_edgesOnly == edgesOnly) {
        return;
    }
    m_edgesOnly = edgesOnly;
    update();
}

void SelectionHandleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    // In edgesOnly mode, edge handles (N, S, E, W) don't draw square boxes (they act as invisible edge resize zones)
    if (m_edgesOnly && isEdgeHandle()) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const double halfVisual = canvas::kHandleVisualSizeDevicePx * 0.5;

    // For rotate handle, draw the 8-12px connecting stem line down to the selection box top edge
    if (isRotateHandle()) {
        QPen stemPen(m_accentColor.isValid() ? m_accentColor : QColor(0, 120, 215), 1.0);
        stemPen.setCosmetic(true);
        painter->setPen(stemPen);
        painter->drawLine(QPointF(0.0, halfVisual), QPointF(0.0, halfVisual + 14.0));
    }

    const QRectF handleRect(
        -halfVisual, -halfVisual, canvas::kHandleVisualSizeDevicePx, canvas::kHandleVisualSizeDevicePx);

    // Subtle drop shadow for contrast on both white/black manga backgrounds
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 50));
    if (isRotateHandle()) {
        painter->drawEllipse(handleRect.translated(0.5, 0.5));
    } else {
        painter->drawRoundedRect(handleRect.translated(0.5, 0.5), 1.0, 1.0);
    }

    // White surface fill
    painter->setBrush(Qt::white);

    // Crisp 1.2px accent border (Koharu style)
    QPen borderPen(m_accentColor.isValid() ? m_accentColor : QColor(0, 120, 215), 1.2);
    borderPen.setCosmetic(true);
    painter->setPen(borderPen);
    if (isRotateHandle()) {
        painter->drawEllipse(handleRect);
    } else {
        painter->drawRoundedRect(handleRect, 1.0, 1.0);
    }

    painter->restore();
}

void SelectionHandleItem::setFillColor(const QColor& color) {
    m_accentColor = color;
    update();
}

} // namespace manga::ui::manga
