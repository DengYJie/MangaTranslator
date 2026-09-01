#include "ui/manga/RegionOverlayItem.h"

#include "ui/manga/CanvasZOrder.h"

#include <QBrush>
#include <QPainter>
#include <QPen>

namespace manga::ui::manga {

static constexpr canvas::HandlePosition kHandleOrder[] = {canvas::HandlePosition::NorthWest,
                                                          canvas::HandlePosition::North,
                                                          canvas::HandlePosition::NorthEast,
                                                          canvas::HandlePosition::East,
                                                          canvas::HandlePosition::SouthEast,
                                                          canvas::HandlePosition::South,
                                                          canvas::HandlePosition::SouthWest,
                                                          canvas::HandlePosition::West};

RegionOverlayItem::RegionOverlayItem(::manga::domain::RegionId id, QGraphicsItem* parent)
    : QGraphicsRectItem(parent), m_id(id) {
    setZValue(canvas::kZRegionNormal);

    QPen pen(QColor(0, 120, 215), 0);
    pen.setCosmetic(true);
    setPen(pen);
    setBrush(Qt::NoBrush);

    for (int i = 0; i < 8; ++i) {
        m_handles[i] = new SelectionHandleItem(kHandleOrder[i], this);
        m_handles[i]->setVisible(false);
    }
}

void RegionOverlayItem::updateFromDisplay(const canvas::RegionDisplay& display) {
    setRect(display.geometry.x, display.geometry.y, display.geometry.width, display.geometry.height);
    setSelected(display.selected);
    layoutHandles();
}

void RegionOverlayItem::setTransientGeometry(const ::manga::domain::RectGeometry& geometry) {
    setRect(geometry.x, geometry.y, geometry.width, geometry.height);
    layoutHandles();
}

::manga::domain::RectGeometry RegionOverlayItem::currentGeometry() const noexcept {
    const QRectF r = rect();
    return {.x = r.x(), .y = r.y(), .width = r.width(), .height = r.height()};
}

QRectF RegionOverlayItem::boundingRect() const {
    return QGraphicsRectItem::boundingRect().adjusted(-2.0, -2.0, 2.0, 2.0);
}

void RegionOverlayItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->save();
    painter->setBrush(Qt::NoBrush);

    QPen halo(QColor(0, 0, 0, 220), 3.0);
    halo.setCosmetic(true);
    halo.setJoinStyle(Qt::MiterJoin);
    painter->setPen(halo);
    painter->drawRect(rect());

    const QColor foreground = m_selected ? m_selectedStroke : QColor(Qt::white);
    QPen outline(foreground, 1.0);
    outline.setCosmetic(true);
    outline.setJoinStyle(Qt::MiterJoin);
    painter->setPen(outline);
    painter->drawRect(rect());
    painter->restore();
}

void RegionOverlayItem::layoutHandles() {
    const QRectF r = rect();
    const double left = r.left();
    const double top = r.top();
    const double right = r.right();
    const double bottom = r.bottom();
    const double cx = r.center().x();
    const double cy = r.center().y();

    const QPointF positions[] = {
        {left, top}, {cx, top}, {right, top}, {right, cy}, {right, bottom}, {cx, bottom}, {left, bottom}, {left, cy}};

    for (int i = 0; i < 8; ++i) {
        m_handles[i]->setPos(positions[i]);
    }
}

void RegionOverlayItem::setOrder(int order) {
    m_order = order;
    QString name = QStringLiteral("Region %1").arg(order);
    if (m_selected) {
        name += QStringLiteral(", selected");
    }
    setData(Qt::AccessibleDescriptionRole, name);
}

void RegionOverlayItem::setSelected(bool selected) {
    m_selected = selected;
    for (auto* handle : m_handles) {
        handle->setVisible(selected);
    }

    if (selected) {
        QPen pen(m_selectedStroke, 0);
        pen.setCosmetic(true);
        setPen(pen);
    } else {
        QPen pen(m_unselectedStroke, 0);
        pen.setCosmetic(true);
        setPen(pen);
    }

    if (m_order > 0) {
        QString name = QStringLiteral("Region %1").arg(m_order);
        if (selected) {
            name += QStringLiteral(", selected");
        }
        setData(Qt::AccessibleDescriptionRole, name);
    }
}

void RegionOverlayItem::setColors(const QColor& selectedStroke,
                                  const QColor& unselectedStroke,
                                  const QColor& handleFill) {
    m_selectedStroke = selectedStroke;
    m_unselectedStroke = unselectedStroke;
    m_handleFill = handleFill;
    setSelected(m_selected);
    for (auto* handle : m_handles) {
        handle->setFillColor(handleFill);
    }
}

} // namespace manga::ui::manga
