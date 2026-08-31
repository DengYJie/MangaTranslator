#include "ui/manga/SelectionHandleItem.h"

#include "ui/manga/CanvasZOrder.h"

#include <QBrush>
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

} // namespace manga::ui::manga
