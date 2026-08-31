#include "ui/manga/SelectionHandleItem.h"

#include "ui/manga/CanvasZOrder.h"

#include <QBrush>
#include <QPainterPath>
#include <QPen>

namespace manga::ui::manga {

SelectionHandleItem::SelectionHandleItem(canvas::HandlePosition position, QGraphicsItem* parent)
    : QGraphicsRectItem(parent), m_position(position) {
    setFlag(ItemIgnoresTransformations, true);
    setZValue(canvas::kZSelectionHandles);

    const double halfVisual = canvas::kHandleVisualSizeDevicePx * 0.5;
    setRect(-halfVisual, -halfVisual, canvas::kHandleVisualSizeDevicePx, canvas::kHandleVisualSizeDevicePx);

    QPen pen(Qt::white, 0);
    pen.setCosmetic(true);
    setPen(pen);
    setBrush(QColor(0, 120, 215));
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

void SelectionHandleItem::setFillColor(const QColor& color) {
    setBrush(color);
}

} // namespace manga::ui::manga
